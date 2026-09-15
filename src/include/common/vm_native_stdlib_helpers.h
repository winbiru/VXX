#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "vpp/runtime/value.h"

namespace vietvm::helpers {

// Chuyển đối số đường dẫn UTF-8 của V++ thành filesystem path và từ chối
// byte UTF-8 lỗi ngay tại native boundary.
bool nativeUtf8Path(const StackValue &value,
                    const std::string &operation,
                    std::filesystem::path &path,
                    std::string &err);

// Dispatch nhóm hàm native nền tảng/thư viện chuẩn; handler kiểm tra tên hàm và thực hiện filesystem, time, environment hoặc utility tương ứng.
bool handleNativeFoundationFunction(const std::string &fn,
                                    const std::vector<StackValue> &args,
                                    StackValue &result,
                                    std::string &err);

} // namespace vietvm::helpers
