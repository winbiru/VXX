#pragma once

#include <string>
#include <vector>

#include "vpp/runtime/value.h"
#include "vpp/bytecode/instruction.h"

namespace vietvm::helpers {

// Primitive native M3 còn lại: bridge byte UTF-8, socket và DNS.
// Chuẩn hóa/chuyển đổi bảng mã và locale policy được triển khai trong V++.
bool handleNativeM3LibraryFunction(Opcode opcode,
                                   const std::vector<StackValue> &args,
                                   StackValue &result,
                                   std::string &err);

} // namespace vietvm::helpers
