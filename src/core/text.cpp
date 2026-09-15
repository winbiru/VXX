#include "vpp/core/text.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>
#include <utility>

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

struct VietnameseCasePair {
    char32_t lower;
    char32_t upper;
};

// Các cặp case dựng sẵn thuộc bảng chữ cái tiếng Việt. ASCII được xử lý bằng
// range riêng để bảng chỉ chứa code point ngoài ASCII.
constexpr std::array<VietnameseCasePair, 67> kVietnameseCasePairs = {{
    {0x0103u, 0x0102u}, {0x00E2u, 0x00C2u}, {0x00EAu, 0x00CAu},
    {0x00F4u, 0x00D4u}, {0x01A1u, 0x01A0u}, {0x01B0u, 0x01AFu},
    {0x0111u, 0x0110u}, {0x00E0u, 0x00C0u}, {0x00E1u, 0x00C1u},
    {0x1EA3u, 0x1EA2u}, {0x00E3u, 0x00C3u}, {0x1EA1u, 0x1EA0u},
    {0x1EB1u, 0x1EB0u}, {0x1EAFu, 0x1EAEu}, {0x1EB3u, 0x1EB2u},
    {0x1EB5u, 0x1EB4u}, {0x1EB7u, 0x1EB6u}, {0x1EA7u, 0x1EA6u},
    {0x1EA5u, 0x1EA4u}, {0x1EA9u, 0x1EA8u}, {0x1EABu, 0x1EAAu},
    {0x1EADu, 0x1EACu}, {0x00E8u, 0x00C8u}, {0x00E9u, 0x00C9u},
    {0x1EBBu, 0x1EBAu}, {0x1EBDu, 0x1EBCu}, {0x1EB9u, 0x1EB8u},
    {0x1EC1u, 0x1EC0u}, {0x1EBFu, 0x1EBEu}, {0x1EC3u, 0x1EC2u},
    {0x1EC5u, 0x1EC4u}, {0x1EC7u, 0x1EC6u}, {0x00ECu, 0x00CCu},
    {0x00EDu, 0x00CDu}, {0x1EC9u, 0x1EC8u}, {0x0129u, 0x0128u},
    {0x1ECBu, 0x1ECAu}, {0x00F2u, 0x00D2u}, {0x00F3u, 0x00D3u},
    {0x1ECFu, 0x1ECEu}, {0x00F5u, 0x00D5u}, {0x1ECDu, 0x1ECCu},
    {0x1ED3u, 0x1ED2u}, {0x1ED1u, 0x1ED0u}, {0x1ED5u, 0x1ED4u},
    {0x1ED7u, 0x1ED6u}, {0x1ED9u, 0x1ED8u}, {0x1EDDu, 0x1EDCu},
    {0x1EDBu, 0x1EDAu}, {0x1EDFu, 0x1EDEu}, {0x1EE1u, 0x1EE0u},
    {0x1EE3u, 0x1EE2u}, {0x00F9u, 0x00D9u}, {0x00FAu, 0x00DAu},
    {0x1EE7u, 0x1EE6u}, {0x0169u, 0x0168u}, {0x1EE5u, 0x1EE4u},
    {0x1EEBu, 0x1EEAu}, {0x1EE9u, 0x1EE8u}, {0x1EEDu, 0x1EECu},
    {0x1EEFu, 0x1EEEu}, {0x1EF1u, 0x1EF0u}, {0x1EF3u, 0x1EF2u},
    {0x00FDu, 0x00DDu}, {0x1EF7u, 0x1EF6u}, {0x1EF9u, 0x1EF8u},
    {0x1EF5u, 0x1EF4u},
}};

constexpr std::array<char32_t, 5> kVietnameseToneMarks = {{
    0x0300u, // huyền
    0x0301u, // sắc
    0x0309u, // hỏi
    0x0303u, // ngã
    0x0323u, // nặng
}};

struct VietnameseVowelRow {
    char32_t starterLower;
    char32_t starterUpper;
    char32_t shapeMark;
    char32_t lowerPlain;
    char32_t upperPlain;
    std::array<char32_t, 5> lowerTone;
    std::array<char32_t, 5> upperTone;
};

// Mỗi hàng biểu diễn một nguyên âm tiếng Việt ở dạng không dấu thanh và năm
// dấu thanh. `shapeMark` là breve/circumflex/horn trong canonical decomposition.
constexpr std::array<VietnameseVowelRow, 12> kVietnameseVowels = {{
    {U'a', U'A', 0, U'a', U'A',
     {{0x00E0u, 0x00E1u, 0x1EA3u, 0x00E3u, 0x1EA1u}},
     {{0x00C0u, 0x00C1u, 0x1EA2u, 0x00C3u, 0x1EA0u}}},
    {U'a', U'A', 0x0306u, 0x0103u, 0x0102u,
     {{0x1EB1u, 0x1EAFu, 0x1EB3u, 0x1EB5u, 0x1EB7u}},
     {{0x1EB0u, 0x1EAEu, 0x1EB2u, 0x1EB4u, 0x1EB6u}}},
    {U'a', U'A', 0x0302u, 0x00E2u, 0x00C2u,
     {{0x1EA7u, 0x1EA5u, 0x1EA9u, 0x1EABu, 0x1EADu}},
     {{0x1EA6u, 0x1EA4u, 0x1EA8u, 0x1EAAu, 0x1EACu}}},
    {U'e', U'E', 0, U'e', U'E',
     {{0x00E8u, 0x00E9u, 0x1EBBu, 0x1EBDu, 0x1EB9u}},
     {{0x00C8u, 0x00C9u, 0x1EBAu, 0x1EBCu, 0x1EB8u}}},
    {U'e', U'E', 0x0302u, 0x00EAu, 0x00CAu,
     {{0x1EC1u, 0x1EBFu, 0x1EC3u, 0x1EC5u, 0x1EC7u}},
     {{0x1EC0u, 0x1EBEu, 0x1EC2u, 0x1EC4u, 0x1EC6u}}},
    {U'i', U'I', 0, U'i', U'I',
     {{0x00ECu, 0x00EDu, 0x1EC9u, 0x0129u, 0x1ECBu}},
     {{0x00CCu, 0x00CDu, 0x1EC8u, 0x0128u, 0x1ECAu}}},
    {U'o', U'O', 0, U'o', U'O',
     {{0x00F2u, 0x00F3u, 0x1ECFu, 0x00F5u, 0x1ECDu}},
     {{0x00D2u, 0x00D3u, 0x1ECEu, 0x00D5u, 0x1ECCu}}},
    {U'o', U'O', 0x0302u, 0x00F4u, 0x00D4u,
     {{0x1ED3u, 0x1ED1u, 0x1ED5u, 0x1ED7u, 0x1ED9u}},
     {{0x1ED2u, 0x1ED0u, 0x1ED4u, 0x1ED6u, 0x1ED8u}}},
    {U'o', U'O', 0x031Bu, 0x01A1u, 0x01A0u,
     {{0x1EDDu, 0x1EDBu, 0x1EDFu, 0x1EE1u, 0x1EE3u}},
     {{0x1EDCu, 0x1EDAu, 0x1EDEu, 0x1EE0u, 0x1EE2u}}},
    {U'u', U'U', 0, U'u', U'U',
     {{0x00F9u, 0x00FAu, 0x1EE7u, 0x0169u, 0x1EE5u}},
     {{0x00D9u, 0x00DAu, 0x1EE6u, 0x0168u, 0x1EE4u}}},
    {U'u', U'U', 0x031Bu, 0x01B0u, 0x01AFu,
     {{0x1EEBu, 0x1EE9u, 0x1EEDu, 0x1EEFu, 0x1EF1u}},
     {{0x1EEAu, 0x1EE8u, 0x1EECu, 0x1EEEu, 0x1EF0u}}},
    {U'y', U'Y', 0, U'y', U'Y',
     {{0x1EF3u, 0x00FDu, 0x1EF7u, 0x1EF9u, 0x1EF5u}},
     {{0x1EF2u, 0x00DDu, 0x1EF6u, 0x1EF8u, 0x1EF4u}}},
}};

char32_t decodeUtf8CodePoint(const std::string &value,
                             std::size_t offset,
                             std::size_t length) {
    const auto byte = [&](std::size_t index) {
        return static_cast<unsigned char>(value[offset + index]);
    };
    if (length == 1) return byte(0);
    if (length == 2) {
        return static_cast<char32_t>(((byte(0) & 0x1fu) << 6) |
                                     (byte(1) & 0x3fu));
    }
    if (length == 3) {
        return static_cast<char32_t>(((byte(0) & 0x0fu) << 12) |
                                     ((byte(1) & 0x3fu) << 6) |
                                     (byte(2) & 0x3fu));
    }
    return static_cast<char32_t>(((byte(0) & 0x07u) << 18) |
                                 ((byte(1) & 0x3fu) << 12) |
                                 ((byte(2) & 0x3fu) << 6) |
                                 (byte(3) & 0x3fu));
}

void appendUtf8CodePoint(std::string &output, char32_t codePoint) {
    if (codePoint <= 0x7fu) {
        output.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7ffu) {
        output.push_back(static_cast<char>(0xc0u | (codePoint >> 6)));
        output.push_back(static_cast<char>(0x80u | (codePoint & 0x3fu)));
    } else if (codePoint <= 0xffffu) {
        output.push_back(static_cast<char>(0xe0u | (codePoint >> 12)));
        output.push_back(static_cast<char>(0x80u | ((codePoint >> 6) & 0x3fu)));
        output.push_back(static_cast<char>(0x80u | (codePoint & 0x3fu)));
    } else {
        output.push_back(static_cast<char>(0xf0u | (codePoint >> 18)));
        output.push_back(static_cast<char>(0x80u | ((codePoint >> 12) & 0x3fu)));
        output.push_back(static_cast<char>(0x80u | ((codePoint >> 6) & 0x3fu)));
        output.push_back(static_cast<char>(0x80u | (codePoint & 0x3fu)));
    }
}

char32_t mapVietnameseCase(char32_t codePoint, bool upper) {
    if (upper && codePoint >= U'a' && codePoint <= U'z') {
        return codePoint - (U'a' - U'A');
    }
    if (!upper && codePoint >= U'A' && codePoint <= U'Z') {
        return codePoint + (U'a' - U'A');
    }
    for (const VietnameseCasePair &pair : kVietnameseCasePairs) {
        if (upper && codePoint == pair.lower) return pair.upper;
        if (!upper && codePoint == pair.upper) return pair.lower;
    }
    return codePoint;
}

std::string transformVietnameseCase(const std::string &value, bool upper) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t offset = 0; offset < value.size();) {
        const std::size_t expected =
            utf8SequenceLength(static_cast<unsigned char>(value[offset]));
        if (expected == 0 || !validUtf8Unit(value, offset, expected)) {
            result.push_back(value[offset]);
            ++offset;
            continue;
        }
        appendUtf8CodePoint(result, mapVietnameseCase(
            decodeUtf8CodePoint(value, offset, expected), upper));
        offset += expected;
    }
    return result;
}

int vietnameseCombiningClass(char32_t codePoint) {
    if (codePoint == 0x031Bu) return 216; // horn
    if (codePoint == 0x0323u) return 220; // dot below
    if (codePoint == 0x0300u || codePoint == 0x0301u ||
        codePoint == 0x0302u || codePoint == 0x0303u ||
        codePoint == 0x0306u || codePoint == 0x0309u) {
        return 230;
    }
    return 0;
}

void appendCanonicalMarks(std::vector<char32_t> &target,
                          char32_t shapeMark,
                          char32_t toneMark) {
    std::vector<char32_t> marks;
    if (shapeMark != 0) marks.push_back(shapeMark);
    if (toneMark != 0) marks.push_back(toneMark);
    std::stable_sort(marks.begin(), marks.end(), [](char32_t left, char32_t right) {
        return vietnameseCombiningClass(left) < vietnameseCombiningClass(right);
    });
    target.insert(target.end(), marks.begin(), marks.end());
}

bool decomposeVietnameseCodePoint(char32_t codePoint,
                                  std::vector<char32_t> &decomposed) {
    for (const VietnameseVowelRow &row : kVietnameseVowels) {
        const bool lowerPlain = codePoint == row.lowerPlain;
        const bool upperPlain = codePoint == row.upperPlain;
        if (lowerPlain || upperPlain) {
            decomposed.push_back(lowerPlain ? row.starterLower : row.starterUpper);
            appendCanonicalMarks(decomposed, row.shapeMark, 0);
            return true;
        }
        for (std::size_t index = 0; index < kVietnameseToneMarks.size(); ++index) {
            const bool lowerTone = codePoint == row.lowerTone[index];
            const bool upperTone = codePoint == row.upperTone[index];
            if (!lowerTone && !upperTone) continue;
            decomposed.push_back(lowerTone ? row.starterLower : row.starterUpper);
            appendCanonicalMarks(
                decomposed, row.shapeMark, kVietnameseToneMarks[index]);
            return true;
        }
    }
    return false;
}

std::vector<char32_t> expectedVietnameseMarks(char32_t shapeMark,
                                               char32_t toneMark) {
    std::vector<char32_t> marks;
    appendCanonicalMarks(marks, shapeMark, toneMark);
    return marks;
}

void appendNormalizedVietnameseSegment(const std::vector<char32_t> &segment,
                                       std::string &output) {
    if (segment.empty()) return;
    const char32_t starter = segment.front();
    std::vector<char32_t> marks(segment.begin() + 1, segment.end());
    std::stable_sort(marks.begin(), marks.end(), [](char32_t left, char32_t right) {
        return vietnameseCombiningClass(left) < vietnameseCombiningClass(right);
    });

    for (const VietnameseVowelRow &row : kVietnameseVowels) {
        const bool lower = starter == row.starterLower;
        const bool upper = starter == row.starterUpper;
        if (!lower && !upper) continue;

        if (marks == expectedVietnameseMarks(row.shapeMark, 0)) {
            appendUtf8CodePoint(output, lower ? row.lowerPlain : row.upperPlain);
            return;
        }
        for (std::size_t index = 0; index < kVietnameseToneMarks.size(); ++index) {
            if (marks != expectedVietnameseMarks(
                             row.shapeMark, kVietnameseToneMarks[index])) {
                continue;
            }
            appendUtf8CodePoint(
                output, lower ? row.lowerTone[index] : row.upperTone[index]);
            return;
        }
    }

    appendUtf8CodePoint(output, starter);
    for (char32_t mark : marks) appendUtf8CodePoint(output, mark);
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

std::string toLowerUtf8Vietnamese(const std::string &value) {
    return transformVietnameseCase(value, false);
}

std::string toUpperUtf8Vietnamese(const std::string &value) {
    return transformVietnameseCase(value, true);
}

std::string normalizeUtf8VietnameseNfc(const std::string &value) {
    std::string output;
    output.reserve(value.size());
    std::vector<char32_t> segment;

    const auto flush = [&]() {
        appendNormalizedVietnameseSegment(segment, output);
        segment.clear();
    };

    for (std::size_t offset = 0; offset < value.size();) {
        const std::size_t expected =
            utf8SequenceLength(static_cast<unsigned char>(value[offset]));
        if (expected == 0 || !validUtf8Unit(value, offset, expected)) {
            flush();
            output.push_back(value[offset]);
            ++offset;
            continue;
        }

        const char32_t codePoint = decodeUtf8CodePoint(value, offset, expected);
        offset += expected;
        if (vietnameseCombiningClass(codePoint) != 0) {
            if (segment.empty()) {
                appendUtf8CodePoint(output, codePoint);
            } else {
                segment.push_back(codePoint);
            }
            continue;
        }

        flush();
        if (!decomposeVietnameseCodePoint(codePoint, segment)) {
            segment.push_back(codePoint);
        }
    }
    flush();
    return output;
}

std::size_t countVietnameseNormalizedSubstring(const std::string &value,
                                               const std::string &needle) {
    return countSubstring(normalizeUtf8VietnameseNfc(value),
                          normalizeUtf8VietnameseNfc(needle));
}

bool containsVietnameseNormalizedSubstring(const std::string &value,
                                           const std::string &needle) {
    const std::string normalizedValue = normalizeUtf8VietnameseNfc(value);
    const std::string normalizedNeedle = normalizeUtf8VietnameseNfc(needle);
    return normalizedValue.find(normalizedNeedle) != std::string::npos;
}

std::string replaceVietnameseNormalizedAll(const std::string &value,
                                           const std::string &from,
                                           const std::string &to) {
    return replaceAll(normalizeUtf8VietnameseNfc(value),
                      normalizeUtf8VietnameseNfc(from),
                      normalizeUtf8VietnameseNfc(to));
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

// Đảo thứ tự code point UTF-8 mà không làm vỡ byte đa phần; hàm tách ranh giới từng code point rồi ghép chúng theo thứ tự ngược.
std::string reverseUtf8CodePoints(const std::string &value) {
    std::vector<std::string> units;
    units.reserve(value.size());
    for (std::size_t offset = 0; offset < value.size();) {
        const std::size_t length = utf8UnitLengthOrRawByte(value, offset);
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

// Viết hoa đầu từ theo bảng chữ cái tiếng Việt. Chuẩn hóa NFC trước khi đổi
// case giúp input dựng sẵn và input tách dấu có cùng kết quả.
std::string titleVietnameseWords(const std::string &value) {
    std::vector<std::string> words = splitAsciiWords(value);
    for (std::string &word : words) {
        word = toLowerUtf8Vietnamese(normalizeUtf8VietnameseNfc(word));
        if (word.empty()) continue;
        const std::optional<std::string> first = utf8CodePointAt(word, 0);
        if (!first.has_value()) continue;
        const std::string rest = utf8CodePointSlice(
            word, 1, utf8CodePointCount(word));
        word = toUpperUtf8Vietnamese(*first) + rest;
    }
    return joinWithSpaces(words);
}

bool isVietnameseCaseInsensitivePalindrome(const std::string &value) {
    const std::string normalized = toLowerUtf8Vietnamese(
        normalizeUtf8VietnameseNfc(value));
    return normalized == reverseUtf8CodePoints(normalized);
}

bool areVietnameseAnagrams(const std::string &left, const std::string &right) {
    auto normalize = [](const std::string &value) {
        const std::string folded = toLowerUtf8Vietnamese(
            normalizeUtf8VietnameseNfc(value));
        std::vector<std::string> units;
        for (std::size_t offset = 0; offset < folded.size();) {
            const std::size_t length = utf8UnitLengthOrRawByte(folded, offset);
            if (length == 1 && std::isspace(
                                   static_cast<unsigned char>(folded[offset]))) {
                offset += length;
                continue;
            }
            units.emplace_back(folded.substr(offset, length));
            offset += length;
        }
        std::sort(units.begin(), units.end());
        return units;
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
