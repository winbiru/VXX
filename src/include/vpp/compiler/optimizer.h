#pragma once

#include <cstddef>

#include "vpp/compiler/ir.h"

namespace vietvm::compiler {

// Ghi thống kê thay đổi của optimizer, chẳng hạn số no-op bị loại; caller dùng report để kiểm thử và quan sát tác động tối ưu.
struct OptimizationReport {
    std::size_t removedNoOps = 0;
};

// Tối ưu `IrProgram` tại chỗ; hiện optimizer loại no-op và tính lại metadata vùng direct IR để artifact sau tối ưu vẫn nhất quán.
OptimizationReport optimizeIr(IrProgram &program);

} // namespace vietvm::compiler
