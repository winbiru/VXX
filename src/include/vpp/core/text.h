#pragma once

#include <cstddef>
#include <optional>
#include <string>

namespace vietvm::core {

// Loại bỏ khoảng trắng ở đầu và cuối chuỗi; hàm tìm biên trái/phải đầu tiên không phải whitespace rồi trả lát cắt tương ứng.
std::string trim(const std::string &value);

// Chuyển lower ascii; hàm chuyển giá trị đầu vào sang kiểu/biểu diễn đích và trả kết quả đã chuẩn hóa.
std::string toLowerAscii(const std::string &value);

// Đếm số code point UTF-8 trong chuỗi; hàm tiến qua dữ liệu theo độ dài từng sequence thay vì đếm trực tiếp số byte.
std::size_t utf8CodePointCount(const std::string &value);
// Kiểm tra toàn bộ chuỗi có phải UTF-8 hợp lệ hay không, bao gồm overlong,
// surrogate và code point vượt U+10FFFF.
bool isValidUtf8(const std::string &value) noexcept;
// Lấy code point UTF-8 theo chỉ số logic. Dữ liệu malformed giữ policy tương
// thích: byte lỗi được coi là một đơn vị thô thay vì bị bỏ hoặc thay thế.
std::optional<std::string> utf8CodePointAt(const std::string &value,
                                           std::size_t index);
// Cắt chuỗi theo chỉ số code point UTF-8. `start` và `count` đều tính theo
// code point; nếu `count` vượt phần còn lại thì kết quả được cắt tới cuối chuỗi.
std::string utf8CodePointSlice(const std::string &value,
                               std::size_t start,
                               std::size_t count);

} // namespace vietvm::core
