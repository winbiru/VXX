#pragma once

#include <cstdio>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "vpp/bytecode/foreign.h"
#include "vpp/bytecode/instruction.h"
#include "vpp/runtime/value.h"

namespace vietvm::runtime {

// Capture the POSIX error on the calling thread immediately after ffi_call.
// The VM stores this with its own execution state, never in a shared global.
struct ForeignCallResult {
    StackValue value;
    int posixError = 0;
};

// FILE* and DIR* never leave the VM. V++ receives distinct opaque, per-VM
// integer tokens. All still-open resources are reclaimed on reset/destruction.
struct ForeignFileState {
    struct Resolver {
        void *head = nullptr;
        void *cursor = nullptr;
    };
    std::unordered_map<int, std::FILE *> files;
    std::unordered_map<int, void *> directories;
    std::unordered_map<int, Resolver> resolvers;
    struct Socket {
        // SOCKET is pointer-sized on Win64; a POSIX file descriptor is int.
        std::intptr_t descriptor = -1;
        bool datagram = false;
        bool listener = false;
        void *tlsSession = nullptr; // POSIX provider-owned; close via TLS facade.
    };
    std::unordered_map<int, Socket> sockets;
    // A child process and both output pipes belong to the VM that created it.
    // The language drives argv/env construction, polling, reading and reaping.
    struct Process {
        std::string program;
        std::vector<std::string> arguments;
        std::unordered_map<std::string, std::string> environment;
        std::string error;
        int pid = -1;
        int stdoutFd = -1;
        int stderrFd = -1;
#if defined(_WIN32)
        std::string windowsCommandLine; // Already quoted by the V++ process library.
        void *windowsProcess = nullptr;
        void *windowsJob = nullptr;
        void *windowsStdout = nullptr;
        void *windowsStderr = nullptr;
#endif
        bool startAttempted = false;
    };
    std::unordered_map<int, Process> processes;
    ForeignFileState() = default;
    ForeignFileState(const ForeignFileState &) = delete;
    ForeignFileState &operator=(const ForeignFileState &) = delete;
    ~ForeignFileState();
    void closeAll() noexcept;
};

ForeignCallResult executeForeignCall(
    const vietvm::bytecode::ForeignFunctionDescriptor &descriptor,
    const std::vector<StackValue> &arguments,
    const std::unordered_set<std::string> &grantedCapabilities,
    int previousPosixError,
    ForeignFileState *fileState = nullptr);

// Socket machine intrinsic reuses the VM-owned registry during migration.
// The raw socket descriptor is never exposed to V++ bytecode.
bool isSocketMachinePrimitive(int opcode) noexcept;
StackValue executeSocketMachinePrimitive(Opcode opcode,
    const std::vector<StackValue> &arguments,
    const std::unordered_set<std::string> &capabilities,
    ForeignFileState &state);

// Process machine intrinsics share the existing OS adapter and per-VM ownership.
// They require exactly the same granular system.process.* grants as legacy FFI.
bool isProcessMachinePrimitive(int opcode) noexcept;
StackValue executeProcessMachinePrimitive(Opcode opcode,
    const std::vector<StackValue> &arguments,
    const std::unordered_set<std::string> &capabilities,
    ForeignFileState &state);

// Native resolver compatibility ABI: opaque results belong to one VM.
bool isResolverMachinePrimitive(int opcode) noexcept;
StackValue executeResolverMachinePrimitive(Opcode opcode,
    const std::vector<StackValue> &arguments,
    const std::unordered_set<std::string> &capabilities,
    ForeignFileState &state);

// TLS session, certificate checks and transport are managed by the OS provider.
bool isTlsMachinePrimitive(int opcode) noexcept;
StackValue executeTlsMachinePrimitive(Opcode opcode,
    const std::vector<StackValue> &arguments,
    const std::unordered_set<std::string> &capabilities,
    ForeignFileState &state);

} // namespace vietvm::runtime
