#pragma once

#include <functional>
#include <unordered_map>
#include <vector>

#include "vpp/runtime/foreign.h"
#include "vpp/runtime/heap.h"
#include "vpp/runtime/object.h"

namespace vietvm::native::aot {

using Value = runtime::StackValue;
struct Program;
struct Frame;
using Function = int (*)(int);

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
    Value load(int slot);
    void store(int slot, Value value);
    void initialize(int slot);
    void assign();
    void parameter(int slot, int argument, int defaultIndex = -1);
    void increment(int delta);
    void binary(int operation);
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
