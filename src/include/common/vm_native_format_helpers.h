#pragma once

#include <string>
#include <vector>

#include "vpp/runtime/value.h"

namespace vietvm::helpers {

// Dispatch formatter lõi. Handler luôn trả map {"giá trị", "lỗi"} để wrapper
// V++ có thể chuyển mã FORMAT_* thành language exception bắt được bằng `thử`.
bool handleNativeFormatFunction(const std::string &fn,
                                const std::vector<StackValue> &args,
                                StackValue &result,
                                std::string &err);

} // namespace vietvm::helpers
