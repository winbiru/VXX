#pragma once

#include <cstdio>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "vpp/bytecode/foreign.h"
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
        int descriptor = -1;
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

} // namespace vietvm::runtime
