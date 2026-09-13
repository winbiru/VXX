#pragma once

#include <string>
#include <vector>

#include "vpp/runtime/value.h"

namespace vietvm::helpers {

// Dispatch nhóm hàm native nền tảng/thư viện chuẩn; handler kiểm tra tên hàm và thực hiện filesystem, time, environment hoặc utility tương ứng.
bool handleNativeFoundationFunction(const std::string &fn,
                                    const std::vector<StackValue> &args,
                                    StackValue &result,
                                    std::string &err);

} // namespace vietvm::helpers
