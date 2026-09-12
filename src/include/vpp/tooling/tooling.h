#pragma once

// Canonical public include path; legacy header remains supported for now.
#include "common/tooling.h"
#include "vpp/compiler/ir.h"
#include "vpp/frontend/ast.h"

namespace vietvm::tooling {

// Human-readable, deterministic views of the compiler's structural stages.
// They are intentionally diagnostic output rather than a stable serialized
// interchange format.
std::string dumpAst(const vietvm::frontend::AstProgram &program);
std::string dumpIr(const vietvm::compiler::IrProgram &program);

} // namespace vietvm::tooling
