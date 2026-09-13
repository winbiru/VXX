#pragma once

#include <string>
#include <vector>

#include "vpp/runtime/value.h"

namespace vietvm::helpers {

// Dispatch các hàm native xử lý chuỗi/văn bản; handler lấy đối số từ stack, gọi tiện ích text tương ứng rồi trả `StackValue` kết quả.
bool handleNativeTextFunction(const std::string &fn,
                              const std::vector<StackValue> &args,
                              StackValue &result,
                              std::string &err);

} // namespace vietvm::helpers

