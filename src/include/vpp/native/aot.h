#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "vm/instruction.h"
#include "vpp/native/aot_runtime.h"

namespace vietvm::native::aot {

struct FunctionCode {
    int nameIndex = -1;
    std::vector<Instruction> instructions;
};
struct Snapshot {
    std::vector<std::string> strings;
    std::vector<Instruction> entry;
    std::map<int, FunctionCode> functions;
    std::vector<std::vector<Instruction>> initializers;
    std::vector<bytecode::ForeignFunctionDescriptor> foreignFunctions;
};

// Bytecode is only an input IR. Output is host assembly with native functions,
// branches and relocations; executable code never dispatches input bytecode.
std::string emitAssembly(const Snapshot &, bool compiler = false);
Snapshot snapshotFromWire(const std::vector<Value> &wire);
void compileExecutable(const Snapshot &, const std::filesystem::path &output,
                       const std::filesystem::path &installation, bool compiler = false);
struct FunctionSpec { int name, required, maximum, reserved; Function function; };
struct StringSpec { const char *data; std::size_t size; };
struct ExtentSpec { std::size_t parameter; int lengthParameter; int reserved; std::size_t fixed; };
struct ForeignSpec {
    int id, result;
    const char *library, *symbol, *abi, *capability;
    std::size_t parameterCount;
    const int *parameters;
    std::size_t extentCount;
    const ExtentSpec *extents;
};
struct ProgramSpec {
    std::size_t stringCount;
    const StringSpec *strings;
    std::size_t functionCount;
    const FunctionSpec *functions;
    std::size_t initializerCount;
    const Function *initializers;
    Function entry;
    int compiler;
    int reserved;
    std::size_t foreignCount;
    const ForeignSpec *foreignFunctions;
};

} // namespace vietvm::native::aot
