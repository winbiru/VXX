#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "vpp/runtime/value.h"
#include "vpp/bytecode/instruction.h"

namespace vietvm::runtime {
struct FileMachineState {
    struct File { std::intptr_t descriptor = -1; int mode = 0; };
    std::unordered_map<int, File> files;
    int lastError = 0;
    FileMachineState() = default;
    FileMachineState(const FileMachineState &) = delete;
    FileMachineState &operator=(const FileMachineState &) = delete;
    ~FileMachineState();
    void closeAll() noexcept;
};

bool isFileMachinePrimitive(int opcode) noexcept;
StackValue executeFileMachinePrimitive(Opcode opcode,
    const std::vector<StackValue> &arguments,
    const std::unordered_set<std::string> &capabilities,
    FileMachineState &state);
}
