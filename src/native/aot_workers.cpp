#include "vpp/native/aot_runtime.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

#include "vpp/bytecode/opcode.h"
#include "vpp/runtime/error.h"

namespace vietvm::native::aot {

namespace {

// Cancellation is a control signal, so V++ try/catch must not consume it.
struct WorkerCancelled final {};

enum class WorkerStatus : int { Running = 0, Done = 1, Cancelled = 2, Failed = 3 };

[[noreturn]] void workerError(const std::string &message) {
    throw runtime::LanguageException(make_string_value(message));
}

// Copy the entire value graph across OS-thread boundaries, preserving cycles,
// shared references and closure cell identities. No references to the source
// Program heap are retained by the destination.
class WorkerValueCloner {
public:
    Value clone(const Value &value) {
        Value result = shallowValue(value);
        drain();
        return result;
    }

    runtime::ClassHandle copyClass(const runtime::ClassHandle &source) {
        auto result = shallowClass(source);
        drain();
        return result;
    }

    runtime::CellHandle copyCell(const runtime::CellHandle &source) {
        auto result = shallowCell(source);
        drain();
        return result;
    }

private:
    // Enqueue graph edges instead of recursively visiting them. This also
    // handles a closure whose captured cell points back to that closure.
    std::vector<std::function<void()>> pending_;
    std::unordered_map<const runtime::MapValue *, runtime::MapHandle> maps_;
    std::unordered_map<const runtime::ListValue *, runtime::ListHandle> lists_;
    std::unordered_map<const runtime::TupleValue *, runtime::TupleHandle> tuples_;
    std::unordered_map<const runtime::RuntimeClass *, runtime::ClassHandle> classes_;
    std::unordered_map<const runtime::RuntimeInstance *, runtime::InstanceHandle> instances_;
    std::unordered_map<const runtime::RuntimeClosure *, runtime::ClosureHandle> closures_;
    std::unordered_map<const runtime::RuntimeCell *, runtime::CellHandle> cells_;

    void drain() {
        while (!pending_.empty()) {
            auto expand = std::move(pending_.back());
            pending_.pop_back();
            expand();
        }
    }

    Value shallowValue(const Value &value) {
        if (const auto *object = std::get_if<runtime::MapHandle>(&value))
            return shallowMap(*object);
        if (const auto *object = std::get_if<runtime::ListHandle>(&value))
            return shallowList(*object);
        if (const auto *object = std::get_if<runtime::TupleHandle>(&value))
            return shallowTuple(*object);
        if (const auto *object = std::get_if<runtime::ClassHandle>(&value))
            return shallowClass(*object);
        if (const auto *object = std::get_if<runtime::InstanceHandle>(&value))
            return shallowInstance(*object);
        if (const auto *object = std::get_if<runtime::ClosureHandle>(&value))
            return shallowClosure(*object);
        return value;
    }

    runtime::ClassHandle shallowClass(const runtime::ClassHandle &source) {
        if (!source) return {};
        const auto found = classes_.find(source.get());
        if (found != classes_.end()) return found->second;
        auto copy = std::make_shared<runtime::RuntimeClass>();
        classes_.emplace(source.get(), copy);
        pending_.emplace_back([this, source, copy] {
            copy->name = source->name;
            copy->methods = source->methods;
            copy->superclass = shallowClass(source->superclass);
        });
        return copy;
    }

    runtime::CellHandle shallowCell(const runtime::CellHandle &source) {
        if (!source) return {};
        const auto found = cells_.find(source.get());
        if (found != cells_.end()) return found->second;
        auto copy = std::make_shared<runtime::RuntimeCell>();
        cells_.emplace(source.get(), copy);
        pending_.emplace_back([this, source, copy] {
            copy->value = shallowValue(source->value);
        });
        return copy;
    }

    runtime::MapHandle shallowMap(const runtime::MapHandle &source) {
        if (!source) return {};
        const auto it = maps_.find(source.get());
        if (it != maps_.end()) return it->second;
        auto copy = std::make_shared<runtime::MapValue>();
        maps_.emplace(source.get(), copy);
        pending_.emplace_back([this, source, copy] {
            for (const auto &[key, value] : source->entries)
                copy->entries.emplace(key, shallowValue(value));
        });
        return copy;
    }

    runtime::ListHandle shallowList(const runtime::ListHandle &source) {
        if (!source) return {};
        const auto it = lists_.find(source.get());
        if (it != lists_.end()) return it->second;
        auto copy = std::make_shared<runtime::ListValue>();
        lists_.emplace(source.get(), copy);
        pending_.emplace_back([this, source, copy] {
            copy->elements.reserve(source->elements.size());
            for (const auto &value : source->elements)
                copy->elements.push_back(shallowValue(value));
        });
        return copy;
    }

    runtime::TupleHandle shallowTuple(const runtime::TupleHandle &source) {
        if (!source) return {};
        const auto it = tuples_.find(source.get());
        if (it != tuples_.end()) return it->second;
        auto copy = std::make_shared<runtime::TupleValue>();
        tuples_.emplace(source.get(), copy);
        pending_.emplace_back([this, source, copy] {
            copy->elements.reserve(source->elements.size());
            for (const auto &value : source->elements)
                copy->elements.push_back(shallowValue(value));
        });
        return copy;
    }

    runtime::InstanceHandle shallowInstance(const runtime::InstanceHandle &source) {
        if (!source) return {};
        const auto it = instances_.find(source.get());
        if (it != instances_.end()) return it->second;
        auto copy = std::make_shared<runtime::RuntimeInstance>();
        instances_.emplace(source.get(), copy);
        pending_.emplace_back([this, source, copy] {
            copy->klass = shallowClass(source->klass);
            for (const auto &[key, value] : source->fields)
                copy->fields.emplace(key, shallowValue(value));
        });
        return copy;
    }

    runtime::ClosureHandle shallowClosure(const runtime::ClosureHandle &source) {
        if (!source) return {};
        const auto it = closures_.find(source.get());
        if (it != closures_.end()) return it->second;
        auto copy = std::make_shared<runtime::RuntimeClosure>();
        closures_.emplace(source.get(), copy);
        pending_.emplace_back([this, source, copy] {
            copy->functionId = source->functionId;
            copy->methodOwnerClass = shallowClass(source->methodOwnerClass);
            for (const auto &[slot, cell] : source->captures)
                copy->captures.emplace(slot, shallowCell(cell));
        });
        return copy;
    }
};

std::string workerExceptionText(const runtime::LanguageException &error) {
    if (const auto *text = std::get_if<std::string>(&error.value()))
        return *text;
    return sv_to_string(error.value());
}

} // namespace

struct WorkerTask {
    std::mutex mutex;
    // Serialize reaping between concurrent waiters and root shutdown.
    std::mutex joinMutex;
    std::condition_variable cv;
    std::atomic<bool> cancelRequested{false};
    std::thread thread;
    WorkerStatus status = WorkerStatus::Running;
    // Accessed under WorkerState::mutex. Used to reject wait cycles and to
    // wake a worker blocked on another worker when it is cancelled.
    std::weak_ptr<WorkerTask> waitingOn;
    // A completed result can contain cycles. Its detached snapshot must be
    // traced until the task is discarded; otherwise a cyclic result that was
    // never waited on leaks when the worker registry shuts down.
    runtime::RuntimeHeap resultHeap;
    Value result = make_null_value();
    std::string error;
};

struct WorkerState {
    std::mutex mutex;
    std::unordered_map<int, std::shared_ptr<WorkerTask>> tasks;
    int nextId = 1;
    bool stopping = false;
};

namespace {

void reapWorker(const std::shared_ptr<WorkerTask> &task) {
    std::lock_guard<std::mutex> lock(task->joinMutex);
    if (task->thread.joinable() &&
        task->thread.get_id() != std::this_thread::get_id())
        task->thread.join();
}

// A cancellation accepted while an exception is propagating must win the
// terminal-state race, just as it does for a successful return. Serialize
// both outcomes with requestWorkerCancellation() under task->mutex.
void publishWorkerFailure(const std::shared_ptr<WorkerTask> &task,
                          std::string message) {
    std::lock_guard<std::mutex> lock(task->mutex);
    if (task->cancelRequested.load(std::memory_order_acquire)) {
        task->status = WorkerStatus::Cancelled;
    } else {
        task->error = std::move(message);
        task->status = WorkerStatus::Failed;
    }
}

// Cancellation can interrupt both park(task->cv) and wait(target->cv).
// Publish the flag while holding the registry lock so the waitingOn edge
// cannot change between observing it and requesting cancellation. Acquire
// each condition variable's own mutex before notifying: without that lock,
// the flag can change between the waiter's predicate check and the atomic
// unlock-and-wait, losing the only wakeup for an infinite wait.
bool requestWorkerCancellation(const std::shared_ptr<WorkerState> &state,
                               const std::shared_ptr<WorkerTask> &task) {
    std::shared_ptr<WorkerTask> awaited;
    {
        std::lock_guard<std::mutex> registryLock(state->mutex);
        std::lock_guard<std::mutex> taskLock(task->mutex);
        if (task->status != WorkerStatus::Running) return false;
        task->cancelRequested.store(true, std::memory_order_release);
        awaited = task->waitingOn.lock();
    }
    {
        std::lock_guard<std::mutex> taskLock(task->mutex);
        task->cv.notify_all();
    }
    if (awaited && awaited != task) {
        std::lock_guard<std::mutex> targetLock(awaited->mutex);
        awaited->cv.notify_all();
    }
    return true;
}

// A worker only advertises one outstanding wait, and all modifications to
// this graph are guarded by the registry lock. The guard clears that edge on
// success, timeout, cancellation and error alike.
struct WorkerWaitEdge {
    std::shared_ptr<WorkerState> state;
    std::shared_ptr<WorkerTask> current;

    WorkerWaitEdge(std::shared_ptr<WorkerState> registry,
                   std::shared_ptr<WorkerTask> caller,
                   const std::shared_ptr<WorkerTask> &target)
        : state(std::move(registry)), current(std::move(caller)) {
        if (!current) return;
        std::lock_guard<std::mutex> lock(state->mutex);
        for (auto node = target; node; node = node->waitingOn.lock()) {
            if (node == current)
                workerError("thread_vm_wait: phát hiện vòng đợi giữa các worker");
        }
        current->waitingOn = target;
    }

    ~WorkerWaitEdge() {
        if (!current) return;
        std::lock_guard<std::mutex> lock(state->mutex);
        current->waitingOn.reset();
    }
};

} // namespace

void Program::initializeWorkerRuntime() {
    workerState = std::make_shared<WorkerState>();
}

Program::~Program() {
    if (ownsWorkerState) shutdownWorkerRuntime();
}

void Program::shutdownWorkerRuntime() noexcept {
    if (!workerState) return;
    std::vector<std::shared_ptr<WorkerTask>> tasks;
    {
        std::lock_guard<std::mutex> lock(workerState->mutex);
        workerState->stopping = true;
        tasks.reserve(workerState->tasks.size());
        for (const auto &entry : workerState->tasks) tasks.push_back(entry.second);
    }
    for (const auto &task : tasks)
        (void)requestWorkerCancellation(workerState, task);
    // Join every OS thread before the owner Program and its callbacks die.
    // A concurrent waiter may already be joining the same task.
    for (const auto &task : tasks) {
        try { reapWorker(task); }
        catch (...) { /* Destructors must not throw. */ }
    }
    std::lock_guard<std::mutex> lock(workerState->mutex);
    workerState->tasks.clear();
}

void Program::workerSafepoint() {
    if (currentWorker && currentWorker->cancelRequested.load(std::memory_order_acquire))
        throw WorkerCancelled{};
}

Value Program::threadIntrinsic(int operation, const std::vector<Value> &args) {
    using namespace std::chrono;
    if (operation == OP_VM_THREAD_PARK) {
        const auto *delay = args.size() == 1 ? std::get_if<int>(&args[0]) : nullptr;
        if (!delay) workerError("thread_vm_park: thời gian chờ phải là số nguyên");
        if (*delay < 0 || *delay > 86400000)
            workerError("thread_vm_park: thời gian chờ phải trong 0..86400000 ms");
        if (!currentWorker)
            workerError("thread_vm_park: chỉ được dùng bên trong worker V++");
        std::unique_lock<std::mutex> lock(currentWorker->mutex);
        if (currentWorker->cancelRequested.load(std::memory_order_acquire) ||
            currentWorker->cv.wait_for(lock, milliseconds(*delay), [&] {
                return currentWorker->cancelRequested.load(std::memory_order_acquire);
            }))
            throw WorkerCancelled{};
        return make_null_value();
    }

    if (operation == OP_VM_THREAD_SPAWN) {
        if (args.size() != 2) workerError("thread_vm_spawn: cần 2 đối số");
        const auto *list = std::get_if<runtime::ListHandle>(&args[1]);
        if (!list || !*list) workerError("thread_vm_spawn: đối số phải là danh sách");
        const auto *closure = std::get_if<runtime::ClosureHandle>(&args[0]);
        const auto *name = std::get_if<std::string>(&args[0]);
        const auto *idValue = std::get_if<int>(&args[0]);
        if ((!closure || !*closure) && !name && !idValue)
            workerError("thread_vm_spawn: công việc phải là hàm hoặc closure");

        // Resolve the entry and its arity before acquiring any shared locks.
        int functionId = -1;
        if (closure && *closure) functionId = (*closure)->functionId;
        else if (idValue) functionId = *idValue;
        else {
            for (std::size_t index = 0; index < functions.size(); ++index) {
                if (functions[index].nameIndex >= 0 &&
                    string(functions[index].nameIndex) == *name) {
                    functionId = static_cast<int>(index);
                    break;
                }
            }
        }
        if (functionId < 0 || static_cast<std::size_t>(functionId) >= functions.size() ||
            !functions[functionId].function)
            workerError("thread_vm_spawn: không tìm thấy hàm");
        const auto &entry = functions[functionId];
        if ((*list)->elements.size() < static_cast<std::size_t>(entry.required) ||
            (*list)->elements.size() > static_cast<std::size_t>(entry.maximum))
            workerError("thread_vm_spawn: số lượng đối số không hợp lệ");

        // Snapshot once with a shared identity map: a capture and a global
        // referencing the same object must also alias inside the worker.
        WorkerValueCloner cloner;
        Value callable = cloner.clone(args[0]);
        std::vector<Value> callArgs;
        callArgs.reserve((*list)->elements.size());
        for (const auto &value : (*list)->elements) callArgs.push_back(cloner.clone(value));
        std::unordered_map<int, runtime::CellHandle> workerGlobals;
        workerGlobals.reserve(globals.size());
        for (const auto &[slot, cell] : globals)
            workerGlobals.emplace(slot, cloner.copyCell(cell));
        std::unordered_map<std::string, runtime::ClassHandle> workerClasses;
        for (const auto &[className, klass] : classes)
            workerClasses.emplace(className, cloner.copyClass(klass));

        auto worker = std::make_unique<Program>();
        worker->ownsWorkerState = false;
        worker->workerState = workerState;
        worker->strings = strings;
        worker->functions = functions;
        worker->i32Functions = i32Functions;
        worker->initializers = initializers;
        worker->entry = entry.function;
        worker->foreignFunctions = foreignFunctions;
        worker->globals = std::move(workerGlobals);
        worker->classes = std::move(workerClasses);
        worker->capabilities = capabilities;
        worker->output = output;
        // Both callbacks run against this Program or have no captured state.
        worker->executeArtifact = executeArtifact;
        worker->executeSource = executeSource;

        auto task = std::make_shared<WorkerTask>();
        worker->currentWorker = task;
        int id = 0;
        {
            std::lock_guard<std::mutex> lock(workerState->mutex);
            if (workerState->stopping)
                workerError("thread_vm_spawn: runtime luồng đang dừng");
            if (workerState->nextId == std::numeric_limits<int>::max())
                workerError("thread_vm_spawn: đã hết mã luồng");
            id = workerState->nextId++;
            workerState->tasks.emplace(id, task);
            try {
                task->thread = std::thread(
                    [task, worker = std::move(worker), callable = std::move(callable),
                     callArgs = std::move(callArgs), functionId]() mutable {
                        try {
                            runtime::RuntimeHeapScope scope(worker->heap);
                            for (const auto &[slot, cell] : worker->globals)
                                if (cell) worker->heap.trackValue(cell->value);
                            for (const auto &[className, klass] : worker->classes)
                                worker->heap.trackValue(Value(klass));
                            worker->heap.trackValue(callable);
                            for (const auto &arg : callArgs) worker->heap.trackValue(arg);
                            worker->workerSafepoint();
                            Value response;
                            if (const auto *captured =
                                    std::get_if<runtime::ClosureHandle>(&callable))
                                response = worker->invoke(functionId, std::move(callArgs),
                                                          {}, {}, *captured);
                            else response = worker->invoke(functionId, std::move(callArgs));
                            worker->workerSafepoint();
                            WorkerValueCloner cloner;
                            Value transferred = cloner.clone(response);
                            // Retain the cloned graph in a tracing heap even
                            // if cancellation wins before result publication.
                            // Otherwise a cyclic discarded result leaks.
                            task->resultHeap.trackValue(transferred);
                            {
                                std::lock_guard<std::mutex> lock(task->mutex);
                                // A cancellation accepted during a long
                                // result snapshot must win over completion.
                                // Check under the same mutex as cancel(), so
                                // cancel() cannot return true for a task that
                                // subsequently publishes status Done.
                                if (task->cancelRequested.load(std::memory_order_acquire)) {
                                    task->status = WorkerStatus::Cancelled;
                                } else {
                                    task->result = std::move(transferred);
                                    task->status = WorkerStatus::Done;
                                }
                            }
                        } catch (const WorkerCancelled &) {
                            std::lock_guard<std::mutex> lock(task->mutex);
                            task->status = WorkerStatus::Cancelled;
                        } catch (const runtime::LanguageException &exception) {
                            publishWorkerFailure(task, workerExceptionText(exception));
                        } catch (const std::exception &exception) {
                            publishWorkerFailure(task, exception.what());
                        } catch (...) {
                            publishWorkerFailure(task, "lỗi worker không xác định");
                        }
                        task->cv.notify_all();
                    });
            } catch (const std::system_error &exception) {
                workerState->tasks.erase(id);
                workerError(std::string("thread_vm_spawn: không tạo được OS thread: ") +
                            exception.what());
            }
        }
        return make_int_value(id);
    }

    const auto *idValue = args.empty() ? nullptr : std::get_if<int>(&args[0]);
    if (!idValue || *idValue <= 0)
        workerError("thread_vm: mã luồng phải là số nguyên dương");
    std::shared_ptr<WorkerTask> task;
    {
        std::lock_guard<std::mutex> lock(workerState->mutex);
        const auto found = workerState->tasks.find(*idValue);
        if (found == workerState->tasks.end())
            workerError("thread_vm: không tìm thấy luồng");
        task = found->second;
    }
    if (operation == OP_VM_THREAD_STATUS) {
        std::lock_guard<std::mutex> lock(task->mutex);
        return make_int_value(static_cast<int>(task->status));
    }
    if (operation == OP_VM_THREAD_CANCEL) {
        return make_int_value(requestWorkerCancellation(workerState, task) ? 1 : 0);
    }
    if (operation != OP_VM_THREAD_WAIT || args.size() != 2 ||
        !std::holds_alternative<int>(args[1]))
        workerError("thread_vm_wait: thời gian chờ phải là số nguyên");
    const int timeout = std::get<int>(args[1]);
    if (timeout < -1)
        workerError("thread_vm_wait: thời gian chờ phải là -1 hoặc không âm");
    if (currentWorker == task)
        workerError("thread_vm_wait: worker không thể đợi chính mình");

    workerSafepoint();
    WorkerWaitEdge waitEdge(workerState, currentWorker, task);

    WorkerStatus status;
    Value transferred;
    std::string error;
    {
        std::unique_lock<std::mutex> lock(task->mutex);
        auto interrupted = [&] {
            return currentWorker &&
                   currentWorker->cancelRequested.load(std::memory_order_acquire);
        };
        auto done = [&] { return task->status != WorkerStatus::Running || interrupted(); };
        if (timeout < 0) task->cv.wait(lock, done);
        else if (timeout > 0) (void)task->cv.wait_for(lock, milliseconds(timeout), done);
        if (interrupted()) throw WorkerCancelled{};
        status = task->status;
        transferred = task->result;
        error = task->error;
    }
    if (status != WorkerStatus::Running) reapWorker(task);
    Value result = make_null_value();
    if (status == WorkerStatus::Done) {
        WorkerValueCloner cloner;
        result = cloner.clone(transferred);
        heap.trackValue(result);
    }
    return make_list_value({make_int_value(static_cast<int>(status)),
                            std::move(result), make_string_value(std::move(error))});
}

} // namespace vietvm::native::aot
