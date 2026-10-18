#pragma once

#include <filesystem>
#include <utility>

#include "vpp/native/raw_codegen.h"

namespace vietvm::native {

// Parse CPU/OS/ABI triples in the V++ target registry; the C++ registry is
// retained as an independent parity oracle and for host platform discovery.
TargetParseResult parseTargetVpp(std::string_view triple,
                                 const std::filesystem::path &sourceBase);

// Bootstrap bridge: execute the V++ Machine IR encoder inside the CLI VM.
// The C++ IR is serialized with semantic bindings; V++ owns Machine IR lowering.
// The existing C++ lowerer is retained as an independent test oracle.
MachineFunctionLowering lowerMachineFunctionVpp(const compiler::IrProgram &ir,
                                               std::string_view functionName,
                                               const std::filesystem::path &sourceBase);

// Classify constant/add/sub i32 leaf functions in V++, including overflow
// checks and semantic parameter IDs, for all ISA-specific V++ leaf encoders.
RawLeafLowering lowerRawLeafVpp(const compiler::IrProgram &ir,
                                std::string_view functionName,
                                const std::filesystem::path &sourceBase);

// Bootstrap bridge: execute the V++ Machine IR encoder inside the CLI VM.
MachineEmission emitMachineFunctionVpp(const Target &target,
                                      const MachineFunction &function,
                                      const std::filesystem::path &sourceBase);

// Encode the legacy i32 leaf ABI through the self-hosted ISA modules for all
// 16 supported architectures. Keep the C++ emitter as the reference oracle.
RawLeafResult emitRawLeafVpp(const Target &target, const RawLeaf &leaf,
                             const std::filesystem::path &sourceBase);

// Production relocatable object writer, implemented by gói/biên dịch/đối tượng.vi.
// The C++ object writer remains available as an independent parity oracle.
RawObjectResult emitRawObjectBytesVpp(const Target &target,
                                      const std::vector<std::uint8_t> &code,
                                      std::string_view symbol,
                                      const std::vector<MachineRelocation> &relocations,
                                      const std::filesystem::path &sourceBase);

// Production path: V++ lowers source IR and emits ISA bytes in one VM run.
// The individual lowering/encoding entrypoints above remain parity oracles.
struct VppCompiledLeaf {
    RawLeaf leaf;
    RawLeafResult emission;
    RawObjectResult object; // Present when --emit-obj is requested.
    bool lowered = false; // true even when the encoder subsequently fails
    std::string error;
    explicit operator bool() const noexcept { return error.empty() && lowered && emission; }
};

struct VppCompiledMachine {
    MachineFunction function;
    MachineEmission emission;
    RawObjectResult object; // Present when --emit-obj is requested.
    std::string error;
    explicit operator bool() const noexcept { return error.empty() && emission; }
};

VppCompiledLeaf compileRawLeafVpp(const Target &target, const compiler::IrProgram &ir,
                                  std::string_view functionName,
                                  const std::filesystem::path &sourceBase,
                                  bool emitObject = false);
VppCompiledMachine compileMachineFunctionVpp(const Target &target,
                                             const compiler::IrProgram &ir,
                                             std::string_view functionName,
                                             const std::filesystem::path &sourceBase,
                                             bool emitObject = false);

// Self-host subset: chuyển source .vi vào V++ để parse/resolve/hạ IR,
// rồi phát ISA và object ngay trong một VM run, không dựng C++ IrProgram.
VppCompiledMachine compileSourceMachineFunctionVpp(const Target &target,
                                                   std::string_view source,
                                                   std::string_view functionName,
                                                   const std::filesystem::path &sourceBase);

// Compile every top-level function in one V++ frontend/IR pass. Objects are
// independently relocatable and can be passed to a host linker in source order.
struct VppCompiledProgram {
    std::vector<std::pair<std::string, VppCompiledMachine>> functions;
    std::string error;
    explicit operator bool() const noexcept { return error.empty() && !functions.empty(); }
};

VppCompiledProgram compileSourceProgramVpp(const Target &target,
                                          std::string_view source,
                                          const std::filesystem::path &sourceBase);

} // namespace vietvm::native
