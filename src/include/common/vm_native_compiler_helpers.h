#pragma once

#include <string>
#include <vector>

#include "vpp/runtime/value.h"

namespace vietvm::helpers {

// Cầu nối runtime -> compiler pipeline cho vpp.compiler và tooling M3.
// Không spawn process; mọi kết quả lấy trực tiếp từ frontend/compiler/VM SDK.
bool handleNativeCompilerLibraryFunction(const std::string &fn,
                                         const std::vector<StackValue> &args,
                                         StackValue &result,
                                         std::string &err);

} // namespace vietvm::helpers
