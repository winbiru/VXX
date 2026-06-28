#ifndef VM_H
#define VM_H

#include <vector>
#include <stack>
#include <unordered_map>
#include <string>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

#include "instruction.h"
#include "../common/vm_callframe.h"
#include "../common/vm_utils.h"

class VM {
public:
    explicit VM(const std::vector<Instruction>& code);
    VM() = default;
    void run();
    VM(const std::vector<Instruction>& code, const std::vector<std::string>& pool);

    std::unordered_map<int, std::vector<Instruction>> hamBytecodeMap;
    // nameIndex → hamId mapping for function name lookup (shared with child VMs for recursion)
    std::unordered_map<int, int> functionTableByNameIndex;

private:
    std::vector<Instruction> bytecode;              // Mã bytecode
    std::vector<std::string> stringPool;

    std::vector<StackValue> stack;                  // data stack (values)

    std::unordered_map<int, StackValue> variables;  // fallback global var store

    // Call stack for function calls
    std::vector<CallFrame> callStack;

    // helper stacks for control-flow
    std::vector<size_t> loopStartStack;
    std::vector<size_t> ifElseStack;
    std::vector<size_t> blockStack;

    size_t pc = 0;                                  // Program counter
    bool running = true;
    int vi_tri_dieu_kien = -1;

    struct SwitchFrame {
        std::optional<StackValue> switchValue;
        bool skippingCase{};
        bool caseMatched{};
        size_t blockDepthAtStart{};
    };
    std::vector<SwitchFrame> switchStack;
    int blockDepth = 0;

    // Exception handling: try stack
    struct TryFrame {
        int catchAddr;        // PC of OP_BAT_LOI
        int stackDepth;       // stack size when try started
        int errVarId;         // variable id to bind error (-1 = none)
    };
    std::vector<TryFrame> tryStack;

    // Các hàm phụ trợ
    void execute(const Instruction& inst);
    int popInt();                // helper pop int from stack (or throw)
    void pushInt(int value);
    StackValue popValue();
    void pushValue(const StackValue &v);

    // CallFrame helpers
    StackValue getArgFromCurrentFrame(int argIndex) const;
    void setLocalInCurrentFrame(int localId, const StackValue& value);

    // Function call helpers
    void enterFunctionFrame(const std::vector<StackValue>& args, int returnPc);
    void leaveCurrentFrame();

    // Runtime optimization/maintenance (MVP)
    bool runJitCompiledLinear();
    void collectGarbage();
};

#endif // VM_H