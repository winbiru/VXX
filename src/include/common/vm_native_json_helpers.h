#pragma once

#include <string>

#include "vpp/runtime/value.h"

namespace vietvm::helpers {

// Escape nội dung chuỗi theo JSON; hàm thay dấu nháy, backslash và ký tự điều khiển bằng escape sequence hợp lệ trước khi serialize.
std::string escapeJsonString(const std::string &input);
// Phân tích JSON; hàm duyệt đầu vào theo ngữ pháp/định dạng quy định, tạo cấu trúc kết quả và báo lỗi khi dữ liệu không hợp lệ.
bool parseJson(const std::string &input, StackValue &result, std::string &err);
// Tuần tự hóa `StackValue` thành JSON; hàm đi đệ quy qua scalar/list/map và escape khóa/chuỗi theo chuẩn JSON.
bool stringifyJson(const StackValue &value, std::string &result, std::string &err);

} // namespace vietvm::helpers
