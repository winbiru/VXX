#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "vpp/bytecode/instruction.h"
#include "vpp/runtime/value.h"

namespace vietvm::runtime {

struct DirectoryMachineState {
    struct Directory {
#if defined(__linux__)
        std::intptr_t handle = -1;
        std::array<char, 4096> buffer{};
        std::size_t nextOffset = 0;
        std::size_t bytesUsed = 0;
#else
        std::intptr_t handle = 0;
#endif
#if defined(_WIN32)
        bool pending = false;
        std::wstring firstName;
#endif
    };
    std::unordered_map<int, Directory> directories;
    DirectoryMachineState() = default;
    DirectoryMachineState(const DirectoryMachineState &) = delete;
    DirectoryMachineState &operator=(const DirectoryMachineState &) = delete;
    ~DirectoryMachineState();
    void closeAll() noexcept;
};

bool isDirectoryMachinePrimitive(int opcode) noexcept;
// mkdir/remove -> [rc, portable error kind, native errno]; kinds: 0=ok,
// 1=already exists, 2=missing, 3=other. open -> [token, errno],
// next -> [1,name] or [0,""] on EOF or [-errno,""] on error;
// close -> [rc, errno]. Tokens belong to one VM instance.
StackValue executeDirectoryMachinePrimitive(
    Opcode opcode, const std::vector<StackValue> &arguments,
    const std::unordered_set<std::string> &capabilities,
    DirectoryMachineState &state);

} // namespace vietvm::runtime
