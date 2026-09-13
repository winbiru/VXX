#include "vpp/core/text.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

namespace vietvm::core {

namespace {

// Xác định số byte của một code point UTF-8 từ byte đầu tiên; hàm kiểm tra các bit tiền tố để trả độ dài 1–4 byte hoặc 0 nếu byte mở đầu không hợp lệ.
std::size_t utf8SequenceLength(unsigned char lead) {
    if ((lead & 0x80u) == 0) return 1;
    if ((lead & 0xe0u) == 0xc0u) return 2;
    if ((lead & 0xf0u) == 0xe0u) return 3;
    if ((lead & 0xf8u) == 0xf0u) return 4;
    return 1;
}

// Kiểm tra một chuỗi byte có tạo thành đúng một đơn vị UTF-8 hợp lệ hay không; hàm xác định độ dài mong đợi rồi xác minh từng byte tiếp diễn có tiền tố `10`.
bool validUtf8Unit(const std::string &value, std::size_t offset, std::size_t length) {
    if (length == 1) return true;
    if (offset + length > value.size()) return false;
    for (std::size_t i = 1; i < length; ++i) {
        const unsigned char byte = static_cast<unsigned char>(value[offset + i]);
        if ((byte & 0xc0u) != 0x80u) return false;
    }
    return true;
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

// Chuyển upper ascii; hàm chuyển giá trị đầu vào sang kiểu/biểu diễn đích và trả kết quả đã chuẩn hóa.
std::string toUpperAscii(const std::string &value) {
    std::string result;
    result.reserve(value.size());
    for (unsigned char c : value) {
        if (c >= static_cast<unsigned char>('a') && c <= static_cast<unsigned char>('z')) {
            result.push_back(static_cast<char>(c - ('a' - 'A')));
        } else {
            result.push_back(static_cast<char>(c));
        }
    }
    return result;
}

// Tách ascii words; hàm chia dữ liệu đầu vào theo quy tắc phân cách trong khi tôn trọng cấu trúc lồng nhau nếu có.
std::vector<std::string> splitAsciiWords(const std::string &value) {
    std::vector<std::string> words;
    std::string current;
    for (unsigned char byte : value) {
        if (std::isspace(byte)) {
            if (!current.empty()) {
                words.push_back(std::move(current));
                current.clear();
            }
        } else {
            current.push_back(static_cast<char>(byte));
        }
    }
    if (!current.empty()) words.push_back(std::move(current));
    return words;
}

// Ghép with spaces; hàm nối các phần tử theo thứ tự bằng dấu phân cách quy định để tạo kết quả duy nhất.
std::string joinWithSpaces(const std::vector<std::string> &words) {
    std::ostringstream output;
    for (std::size_t index = 0; index < words.size(); ++index) {
        if (index != 0) output << ' ';
        output << words[index];
    }
    return output.str();
}

// Đếm số code point UTF-8 trong chuỗi; hàm tiến qua dữ liệu theo độ dài từng sequence thay vì đếm trực tiếp số byte.
std::size_t utf8CodePointCount(const std::string &value) {
    std::size_t count = 0;
    for (std::size_t offset = 0; offset < value.size();) {
        std::size_t length = utf8SequenceLength(static_cast<unsigned char>(value[offset]));
        if (!validUtf8Unit(value, offset, length)) length = 1;
        offset += length;
        ++count;
    }
    return count;
}

// Đảo thứ tự code point UTF-8 mà không làm vỡ byte đa phần; hàm tách ranh giới từng code point rồi ghép chúng theo thứ tự ngược.
std::string reverseUtf8CodePoints(const std::string &value) {
    std::vector<std::string> units;
    units.reserve(value.size());
    for (std::size_t offset = 0; offset < value.size();) {
        std::size_t length = utf8SequenceLength(static_cast<unsigned char>(value[offset]));
        if (!validUtf8Unit(value, offset, length)) length = 1;
        units.emplace_back(value.substr(offset, length));
        offset += length;
    }
    std::reverse(units.begin(), units.end());
    std::string result;
    result.reserve(value.size());
    for (const std::string &unit : units) result += unit;
    return result;
}

// Đếm số lần chuỗi con xuất hiện; hàm lặp `find` từ vị trí kế tiếp cho tới khi không còn kết quả.
std::size_t countSubstring(const std::string &value, const std::string &needle) {
    if (needle.empty()) return 0;
    std::size_t count = 0;
    std::size_t position = 0;
    while ((position = value.find(needle, position)) != std::string::npos) {
        ++count;
        position += needle.size();
    }
    return count;
}

// Thay mọi lần xuất hiện của chuỗi nguồn bằng chuỗi đích; hàm tìm tuần tự và cập nhật vị trí sau mỗi lần thay để tránh lặp vô hạn.
std::string replaceAll(const std::string &value,
                       const std::string &from,
                       const std::string &to) {
    if (from.empty()) return value;
    std::string result = value;
    std::size_t position = 0;
    while ((position = result.find(from, position)) != std::string::npos) {
        result.replace(position, from.size(), to);
        position += to.size();
    }
    return result;
}

// Tìm từ ASCII dài nhất trong chuỗi; hàm tách theo ranh giới từ rồi giữ phần tử có độ dài lớn nhất.
std::string longestAsciiWord(const std::string &value) {
    const std::vector<std::string> words = splitAsciiWords(value);
    std::string longest;
    for (const std::string &word : words) {
        if (utf8CodePointCount(word) > utf8CodePointCount(longest)) longest = word;
    }
    return longest;
}

// Viết hoa ký tự đầu của từng từ ASCII; hàm theo dõi ranh giới từ và chỉ đổi chữ cái đầu tiên sau mỗi khoảng phân cách.
std::string titleAsciiWords(const std::string &value) {
    std::vector<std::string> words = splitAsciiWords(value);
    for (std::string &word : words) {
        word = toLowerAscii(word);
        if (!word.empty()) {
            word.replace(0, 1, toUpperAscii(word.substr(0, 1)));
        }
    }
    return joinWithSpaces(words);
}

// Kiểm tra điều kiện của `isAsciiCaseInsensitivePalindrome`.
bool isAsciiCaseInsensitivePalindrome(const std::string &value) {
    const std::string normalized = toLowerAscii(value);
    return normalized == reverseUtf8CodePoints(normalized);
}

// Kiểm tra hai chuỗi có phải hoán vị ký tự ASCII của nhau; hàm chuẩn hóa rồi so sánh tần suất ký tự của hai phía.
bool areAsciiAnagrams(const std::string &left, const std::string &right) {
    auto normalize = [](const std::string &value) {
        std::string normalized;
        for (unsigned char byte : toLowerAscii(value)) {
            if (!std::isspace(byte)) normalized.push_back(static_cast<char>(byte));
        }
        std::sort(normalized.begin(), normalized.end());
        return normalized;
    };
    return normalize(left) == normalize(right);
}

// Dịch chữ cái ASCII theo khóa Caesar; hàm xoay ký tự trong miền A–Z/a–z và giữ nguyên ký tự ngoài bảng chữ cái.
std::string caesarAscii(const std::string &value, int shift) {
    const int normalized = ((shift % 26) + 26) % 26;
    std::string result = value;
    for (char &byte : result) {
        if (byte >= 'a' && byte <= 'z') {
            byte = static_cast<char>('a' + (byte - 'a' + normalized) % 26);
        } else if (byte >= 'A' && byte <= 'Z') {
            byte = static_cast<char>('A' + (byte - 'A' + normalized) % 26);
        }
    }
    return result;
}

} // namespace vietvm::core
