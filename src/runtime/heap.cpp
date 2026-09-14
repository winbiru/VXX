#include "vpp/runtime/heap.h"

#include <algorithm>
#include <unordered_set>

namespace vietvm::runtime {
namespace {

thread_local RuntimeHeap *activeHeap = nullptr;

template <typename T>
void trackWeak(std::vector<std::weak_ptr<T>> &registry,
               const std::shared_ptr<T> &value) {
    if (value == nullptr) return;
    for (auto it = registry.begin(); it != registry.end();) {
        const std::shared_ptr<T> live = it->lock();
        if (live == nullptr) {
            it = registry.erase(it);
            continue;
        }
        if (live.get() == value.get()) return;
        ++it;
    }
    registry.push_back(value);
}

template <typename T>
void pruneExpired(std::vector<std::weak_ptr<T>> &registry) {
    registry.erase(
        std::remove_if(registry.begin(), registry.end(),
                       [](const std::weak_ptr<T> &entry) { return entry.expired(); }),
        registry.end());
}

template <typename T>
std::size_t liveCount(const std::vector<std::weak_ptr<T>> &registry) {
    return static_cast<std::size_t>(std::count_if(
        registry.begin(), registry.end(),
        [](const std::weak_ptr<T> &entry) { return !entry.expired(); }));
}

// Đánh dấu toàn bộ object graph reachable từ một StackValue bằng worklist lặp;
// cách này tránh dùng native C++ recursion khi list/map/instance graph rất sâu.
void markValue(const StackValue &value,
               std::unordered_set<const void *> &marked) {
    std::vector<StackValue> pending;
    pending.push_back(value);

    while (!pending.empty()) {
        StackValue current = std::move(pending.back());
        pending.pop_back();

        if (std::holds_alternative<MapHandle>(current)) {
            const MapHandle &map = std::get<MapHandle>(current);
            if (map == nullptr || !marked.insert(map.get()).second) continue;
            for (const auto &entry : map->entries) pending.push_back(entry.second);
            continue;
        }
        if (std::holds_alternative<ListHandle>(current)) {
            const ListHandle &list = std::get<ListHandle>(current);
            if (list == nullptr || !marked.insert(list.get()).second) continue;
            for (const StackValue &element : list->elements) pending.push_back(element);
            continue;
        }
        if (std::holds_alternative<TupleHandle>(current)) {
            const TupleHandle &tuple = std::get<TupleHandle>(current);
            if (tuple == nullptr || !marked.insert(tuple.get()).second) continue;
            for (const StackValue &element : tuple->elements) pending.push_back(element);
            continue;
        }
        if (std::holds_alternative<ClassHandle>(current)) {
            ClassHandle klass = std::get<ClassHandle>(current);
            while (klass != nullptr && marked.insert(klass.get()).second) {
                klass = klass->superclass;
            }
            continue;
        }
        if (std::holds_alternative<InstanceHandle>(current)) {
            const InstanceHandle &instance = std::get<InstanceHandle>(current);
            if (instance == nullptr || !marked.insert(instance.get()).second) continue;
            if (instance->klass != nullptr) pending.emplace_back(instance->klass);
            for (const auto &field : instance->fields) pending.push_back(field.second);
            continue;
        }
        if (!std::holds_alternative<ClosureHandle>(current)) continue;

        const ClosureHandle &closure = std::get<ClosureHandle>(current);
        if (closure == nullptr || !marked.insert(closure.get()).second) continue;
        for (const auto &capture : closure->captures) {
            if (capture.second != nullptr) pending.push_back(capture.second->value);
        }
    }
}

template <typename T, typename Clear>
std::size_t sweepRegistry(std::vector<std::weak_ptr<T>> &registry,
                          const std::unordered_set<const void *> &marked,
                          Clear clearEdges) {
    // Giữ strong handle cho toàn bộ object sắp sweep trước khi cắt cạnh. Nếu chỉ
    // lock từng object rồi clear ngay, một graph sâu dạng linked-cycle có thể bị
    // hủy dây chuyền qua shared_ptr destructor và làm tràn native stack.
    std::vector<std::shared_ptr<T>> garbage;
    garbage.reserve(registry.size());
    for (const std::weak_ptr<T> &entry : registry) {
        const std::shared_ptr<T> value = entry.lock();
        if (value == nullptr || marked.count(value.get()) != 0) continue;
        garbage.push_back(value);
    }
    for (const std::shared_ptr<T> &value : garbage) {
        clearEdges(*value);
    }
    const std::size_t swept = garbage.size();
    garbage.clear();
    pruneExpired(registry);
    return swept;
}

} // namespace

RuntimeHeap::~RuntimeHeap() {
    (void)collect({});
}

void RuntimeHeap::track(const MapHandle &value) { trackWeak(maps_, value); }
void RuntimeHeap::track(const ListHandle &value) { trackWeak(lists_, value); }
void RuntimeHeap::track(const TupleHandle &value) { trackWeak(tuples_, value); }
void RuntimeHeap::track(const ClassHandle &value) { trackWeak(classes_, value); }
void RuntimeHeap::track(const InstanceHandle &value) { trackWeak(instances_, value); }
void RuntimeHeap::track(const ClosureHandle &value) { trackWeak(closures_, value); }

// Đăng ký toàn bộ object graph bên dưới một StackValue bằng worklist lặp để test,
// embedding và giá trị tạo ngoài active heap scope không làm tràn native stack.
void RuntimeHeap::trackValue(const StackValue &value) {
    std::unordered_set<const void *> visited;
    std::vector<StackValue> pending;
    pending.push_back(value);

    while (!pending.empty()) {
        StackValue current = std::move(pending.back());
        pending.pop_back();

        if (std::holds_alternative<MapHandle>(current)) {
            const MapHandle &map = std::get<MapHandle>(current);
            if (map == nullptr || !visited.insert(map.get()).second) continue;
            track(map);
            for (const auto &entry : map->entries) pending.push_back(entry.second);
            continue;
        }
        if (std::holds_alternative<ListHandle>(current)) {
            const ListHandle &list = std::get<ListHandle>(current);
            if (list == nullptr || !visited.insert(list.get()).second) continue;
            track(list);
            for (const StackValue &element : list->elements) pending.push_back(element);
            continue;
        }
        if (std::holds_alternative<TupleHandle>(current)) {
            const TupleHandle &tuple = std::get<TupleHandle>(current);
            if (tuple == nullptr || !visited.insert(tuple.get()).second) continue;
            track(tuple);
            for (const StackValue &element : tuple->elements) pending.push_back(element);
            continue;
        }
        if (std::holds_alternative<ClassHandle>(current)) {
            ClassHandle klass = std::get<ClassHandle>(current);
            while (klass != nullptr && visited.insert(klass.get()).second) {
                track(klass);
                klass = klass->superclass;
            }
            continue;
        }
        if (std::holds_alternative<InstanceHandle>(current)) {
            const InstanceHandle &instance = std::get<InstanceHandle>(current);
            if (instance == nullptr || !visited.insert(instance.get()).second) continue;
            track(instance);
            if (instance->klass != nullptr) pending.emplace_back(instance->klass);
            for (const auto &field : instance->fields) pending.push_back(field.second);
            continue;
        }
        if (!std::holds_alternative<ClosureHandle>(current)) continue;

        const ClosureHandle &closure = std::get<ClosureHandle>(current);
        if (closure == nullptr || !visited.insert(closure.get()).second) continue;
        track(closure);
        for (const auto &capture : closure->captures) {
            if (capture.second != nullptr) pending.push_back(capture.second->value);
        }
    }
}

RuntimeHeapStats RuntimeHeap::collect(const std::vector<StackValue> &roots) {
    pruneExpired(maps_);
    pruneExpired(lists_);
    pruneExpired(tuples_);
    pruneExpired(classes_);
    pruneExpired(instances_);
    pruneExpired(closures_);

    RuntimeHeapStats stats;
    stats.trackedBefore = maps_.size() + lists_.size() + tuples_.size() +
                          classes_.size() + instances_.size() + closures_.size();

    std::unordered_set<const void *> marked;
    for (const StackValue &root : roots) markValue(root, marked);
    stats.marked = marked.size();

    stats.swept += sweepRegistry(
        maps_, marked, [](MapValue &value) { value.entries.clear(); });
    stats.swept += sweepRegistry(
        lists_, marked, [](ListValue &value) { value.elements.clear(); });
    stats.swept += sweepRegistry(
        tuples_, marked, [](TupleValue &value) { value.elements.clear(); });
    stats.swept += sweepRegistry(
        instances_, marked, [](RuntimeInstance &value) {
            value.fields.clear();
            value.klass.reset();
        });
    stats.swept += sweepRegistry(
        classes_, marked, [](RuntimeClass &value) {
            value.superclass.reset();
            value.methods.clear();
        });
    stats.swept += sweepRegistry(
        closures_, marked, [](RuntimeClosure &value) { value.captures.clear(); });

    pruneExpired(maps_);
    pruneExpired(lists_);
    pruneExpired(tuples_);
    pruneExpired(classes_);
    pruneExpired(instances_);
    pruneExpired(closures_);
    stats.trackedAfter = maps_.size() + lists_.size() + tuples_.size() +
                         classes_.size() + instances_.size() + closures_.size();
    return stats;
}

std::size_t RuntimeHeap::trackedObjectCount() {
    pruneExpired(maps_);
    pruneExpired(lists_);
    pruneExpired(tuples_);
    pruneExpired(classes_);
    pruneExpired(instances_);
    pruneExpired(closures_);
    return liveCount(maps_) + liveCount(lists_) + liveCount(tuples_) +
           liveCount(classes_) + liveCount(instances_) + liveCount(closures_);
}

RuntimeHeapScope::RuntimeHeapScope(RuntimeHeap &heap) noexcept
    : previous_(activeHeap) {
    activeHeap = &heap;
}

RuntimeHeapScope::~RuntimeHeapScope() { activeHeap = previous_; }

void trackRuntimeAllocation(const MapHandle &value) {
    if (activeHeap != nullptr) activeHeap->track(value);
}

void trackRuntimeAllocation(const ListHandle &value) {
    if (activeHeap != nullptr) activeHeap->track(value);
}

void trackRuntimeAllocation(const TupleHandle &value) {
    if (activeHeap != nullptr) activeHeap->track(value);
}

void trackRuntimeAllocation(const ClassHandle &value) {
    if (activeHeap != nullptr) activeHeap->track(value);
}

void trackRuntimeAllocation(const InstanceHandle &value) {
    if (activeHeap != nullptr) activeHeap->track(value);
}

void trackRuntimeAllocation(const ClosureHandle &value) {
    if (activeHeap != nullptr) activeHeap->track(value);
}

} // namespace vietvm::runtime
