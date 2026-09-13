#pragma once

// Canonical public include path; legacy header remains supported for now.
#include "common/tooling.h"
#include "vpp/compiler/ir.h"
#include "vpp/frontend/ast.h"

namespace vietvm::tooling {

// Tạo bản dump toàn bộ AST gồm thống kê chương trình, cây câu lệnh, biểu thức và lambda để kiểm tra kết quả parser.
std::string dumpAst(const vietvm::frontend::AstProgram &program);
// Tạo bản dump IR sau lowering/optimization, gồm giá trị, lambda và cây lệnh để kiểm tra semantic binding và direct IR.
std::string dumpIr(const vietvm::compiler::IrProgram &program);

} // namespace vietvm::tooling
