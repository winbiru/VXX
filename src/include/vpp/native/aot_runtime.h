#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

#include "vpp/runtime/foreign.h"
#include "vpp/runtime/file_runtime.h"
#include "vpp/runtime/directory_machine.h"
#include "vpp/runtime/heap.h"
#include "vpp/runtime/object.h"

namespace vietvm::native::aot {

using Value = runtime::StackValue;
struct Program;
struct Frame;
using Function = int (*)(int);

// Prints per-thread CPU samples when VPP_AOT_CPU_PROFILE=1. The VM's vm_alloc
// primitive belongs to its separate .vi runtime, not this C++ AOT runtime.
void dumpAotCpuProfile();

// Generated functions contain native control flow. This runtime owns values,
// objects and captured cells; it contains no bytecode execution loop.
struct FunctionEntry {
    int nameIndex = -1;
    int required = 0;
    int maximum = 0;
    Function function = nullptr;
};

struct Frame {
    Program &program;
    bool global = false;
    std::vector<Value> stack;
    std::vector<Value> arguments;
    std::unordered_map<int, runtime::CellHandle> locals;
    // Cache the last few resolved bindings. RuntimeCell addresses survive
    // unordered_map rehashes; parameter/initialize must invalidate entries
    // when they replace a binding.
    struct CachedBinding { int slot = 0; runtime::RuntimeCell *cell = nullptr; };
    std::array<CachedBinding, 8> bindingCache{};
    runtime::InstanceHandle receiver;
    runtime::ClassHandle owner;
    std::vector<std::pair<Value, bool>> switches;
    struct Handler { int target; std::size_t stack; std::size_t switches; };
    std::vector<Handler> handlers;
    Value result = make_null_value();
    int nativeId = -1;

    Frame(Program &, bool global = false);
    ~Frame();
    Value pop();
    void push(Value value) { stack.push_back(std::move(value)); }
    std::vector<Value> take(int count);
    runtime::CellHandle cell(int slot, bool create = true);
    void invalidateBinding(int slot);
    void rememberBinding(int slot, runtime::RuntimeCell *cell);
    // Stable captured-cell access, avoiding copies on scalar hot paths.
    Value &valueAt(int slot);
    Value load(int slot);
    void store(int slot, Value value);
    void initialize(int slot);
    void assign();
    void parameter(int slot, int argument, int defaultIndex = -1);
    void increment(int delta);
    void incrementSlot(int slot, int delta);
    void binary(int operation);
    // Fused typed loop update: consume a slot and an immediate without
    // constructing intermediate stack values or performing five ABI calls.
    void updateIntegerSlot(int source, int immediate, int operation, int target);
    // Fuse two variable reads, a numeric operation and an assignment into a
    // single ABI entry; retain dynamic fallback for reassigned variables.
    void updateNumericSlots(int left, int right, int operation, int target);
    // Fused load/immediate/compare/condition; returns a boolean. The ABI
    // encodes a pending exception separately so a false comparison is safe.
    bool compareIntegerSlot(int source, int immediate, int operation);
    bool compareNumericSlots(int left, int right, int operation);
    // Execute a guarded, side-effect-free counted loop with unboxed CPU
    // scalars. Returning false asks the generated AOT code to run its
    // original dynamic bytecode path instead.
    bool runCountedScalarLoop(int counter, int bound, int value,
                              int step, int threshold);
    // Simple counted loops can execute all scalar iterations in CPU registers
    // with a single type check and a single write-back to the live cells.
    bool runSimpleScalarLoop(int counter, int bound, int value,
                             int operand, int operation);
    // A guarded loop for result = left + right. Both sources are primitive
    // numeric cells, with induction values read from an unboxed counter.
    bool runNumericAddLoop(int counter, int bound, int left,
                           int right, int result);
    void index();
    void setIndex();
    void list(int count, int encoded);
    void map(int count, int encoded);
    void closure(int function, int count);
    void call(int target, int count, int nameIndex, bool indirect = false);
    void intrinsic(int operation);
    void foreign(int descriptor, int count);
    void defineClass(int name, int superclass);
    void defineMethod(int klass, int name, int function);
    void construct(int name, int count);
    void property(int name, bool write);
    void method(int name, int count, bool super);
    bool caseMatches(int operand, int index);
    bool defaultMatches();
    void print();
    void safepoint();
};

struct Program {
    std::vector<std::string> strings;
    std::vector<FunctionEntry> functions;
    std::vector<Function> initializers;
    Function entry = nullptr;
    std::vector<bytecode::ForeignFunctionDescriptor> foreignFunctions;
    std::unordered_map<int, runtime::CellHandle> globals;
    std::unordered_map<std::string, runtime::ClassHandle> classes;
    std::vector<Frame *> frames;
    runtime::RuntimeHeap heap;
    runtime::ForeignFileState files;
    runtime::FileMachineState machineFiles;
    runtime::DirectoryMachineState machineDirectories;
    std::unordered_set<std::string> capabilities;
    int lastForeignError = 0;
    std::function<void(const std::string &)> output;
    std::function<Value(const std::vector<Value> &)> executeArtifact;
    std::function<Value(const std::vector<Value> &)> executeSource;
    std::exception_ptr pendingError;

    Program();
    const std::string &string(int index) const;
    Value invoke(int function, std::vector<Value> arguments = {},
                 runtime::InstanceHandle receiver = {}, runtime::ClassHandle owner = {},
                 runtime::ClosureHandle closure = {});
    Value invokeName(const std::string &, std::vector<Value> arguments = {});
    void initialize();
    Value run();
    void collect();
};

} // namespace vietvm::native::aot
