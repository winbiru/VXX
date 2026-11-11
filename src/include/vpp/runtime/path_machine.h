#pragma once
#include <string>
#include <unordered_set>
#include <vector>
#include "vpp/bytecode/instruction.h"
#include "vpp/runtime/value.h"

namespace vietvm::runtime {
// Return legacy POSIX st_mode/S_IFMT >> 12 kind (4=dir, 8=file, 10=link)
// or Windows kind (0=other/reparse, 1=file, 2=dir), -1 if missing/error.
// PATH_STAT returns ["present", size_bytes, mtime_seconds, mtime_nanoseconds],
// ["missing"], or ["error", OS_error_code]. The V++ wrapper throws on error.
// Metadata fields are strings for regular files; non-file paths are missing.
// Other path operations return ["ok", value] or ["error", OS_error_code].
// Changing cwd additionally requires machine.path.chdir. OS errors are data
// so the V++ library can turn them into catchable language exceptions.
bool isPathMachinePrimitive(int opcode) noexcept;
StackValue executePathMachinePrimitive(Opcode opcode,
    const std::vector<StackValue> &arguments,
    const std::unordered_set<std::string> &capabilities);
}
