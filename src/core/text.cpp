#include "vpp/core/text.h"

namespace vietvm::core {

namespace {

// Xác định số byte của một code point UTF-8 từ byte đầu tiên. Lead overlong
// (C0/C1), continuation đứng riêng và byte > F4 đều không hợp lệ.
std::size_t utf8SequenceLength(unsigned char lead) {
    if ((lead & 0x80u) == 0) return 1;
    if (lead >= 0xc2u && lead <= 0xdfu) return 2;
    if (lead >= 0xe0u && lead <= 0xefu) return 3;
    if (lead >= 0xf0u && lead <= 0xf4u) return 4;
    return 0;
}

bool isContinuation(unsigned char byte) {
    return (byte & 0xc0u) == 0x80u;
}

// Kiểm tra một đơn vị UTF-8 hợp lệ, gồm cả ràng buộc chống overlong,
// surrogate UTF-16 và code point vượt U+10FFFF.
bool validUtf8Unit(const std::string &value, std::size_t offset, std::size_t length) {
    if (length == 1) return true;
    if (length == 0) return false;
    if (offset + length > value.size()) return false;
    const unsigned char lead = static_cast<unsigned char>(value[offset]);
    const unsigned char second = static_cast<unsigned char>(value[offset + 1]);
    if (!isContinuation(second)) return false;
    if (length == 2) return true;

    const unsigned char third = static_cast<unsigned char>(value[offset + 2]);
    if (!isContinuation(third)) return false;
    if (lead == 0xe0u && second < 0xa0u) return false;
    if (lead == 0xedu && second > 0x9fu) return false;
    if (length == 3) return true;

    const unsigned char fourth = static_cast<unsigned char>(value[offset + 3]);
    if (!isContinuation(fourth)) return false;
    if (lead == 0xf0u && second < 0x90u) return false;
    if (lead == 0xf4u && second > 0x8fu) return false;
    return true;
}

// Trả số byte cần tiêu thụ tại offset. Một byte malformed được giữ làm một
// đơn vị thô để các helper hiện hữu không làm mất dữ liệu đầu vào.
std::size_t utf8UnitLengthOrRawByte(const std::string &value, std::size_t offset) {
    const std::size_t length =
        utf8SequenceLength(static_cast<unsigned char>(value[offset]));
    if (length == 0 || !validUtf8Unit(value, offset, length)) {
        return 1;
    }
    return length;
}

} // namespace

// Loại bỏ khoảng trắng ở đầu và cuối chuỗi; hàm tìm biên trái/phải đầu tiên không phải whitespace rồi trả lát cắt tương ứng.
std::string trim(const std::string &value) {
    const size_t start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    const size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

// Chuyển lower ascii; hàm chuyển giá trị đầu vào sang kiểu/biểu diễn đích và trả kết quả đã chuẩn hóa.
std::string toLowerAscii(const std::string &value) {
    std::string result;
    result.reserve(value.size());
    for (unsigned char c : value) {
        if (c >= static_cast<unsigned char>('A') && c <= static_cast<unsigned char>('Z')) {
            result.push_back(static_cast<char>(c + ('a' - 'A')));
        } else {
            result.push_back(static_cast<char>(c));
        }
    }
    return result;
}

// Đếm số code point UTF-8 trong chuỗi; hàm tiến qua dữ liệu theo độ dài từng sequence thay vì đếm trực tiếp số byte.
std::size_t utf8CodePointCount(const std::string &value) {
    std::size_t count = 0;
    for (std::size_t offset = 0; offset < value.size();) {
        const std::size_t length = utf8UnitLengthOrRawByte(value, offset);
        offset += length;
        ++count;
    }
    return count;
}

bool isValidUtf8(const std::string &value) noexcept {
    for (std::size_t offset = 0; offset < value.size();) {
        const std::size_t length =
            utf8SequenceLength(static_cast<unsigned char>(value[offset]));
        if (length == 0 || !validUtf8Unit(value, offset, length)) return false;
        offset += length;
    }
    return true;
}

std::optional<std::string> utf8CodePointAt(const std::string &value,
                                           std::size_t index) {
    std::size_t logicalIndex = 0;
    for (std::size_t offset = 0; offset < value.size();) {
        const std::size_t length = utf8UnitLengthOrRawByte(value, offset);
        if (logicalIndex == index) return value.substr(offset, length);
        offset += length;
        ++logicalIndex;
    }
    return std::nullopt;
}

std::string utf8CodePointSlice(const std::string &value,
                               std::size_t start,
                               std::size_t count) {
    if (count == 0 || value.empty()) return "";

    std::size_t logicalIndex = 0;
    std::size_t byteStart = value.size();
    std::size_t byteEnd = value.size();
    for (std::size_t offset = 0; offset < value.size();) {
        if (logicalIndex == start) byteStart = offset;
        if (logicalIndex == start + count) {
            byteEnd = offset;
            break;
        }
        offset += utf8UnitLengthOrRawByte(value, offset);
        ++logicalIndex;
    }

    if (byteStart == value.size()) return "";
    return value.substr(byteStart, byteEnd - byteStart);
}

} // namespace vietvm::core
