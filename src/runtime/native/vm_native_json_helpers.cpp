#include "common/vm_native_json_helpers.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace vietvm::helpers {

namespace {

// Nối thêm UTF-8; hàm đưa dữ liệu mới vào cuối cấu trúc đích theo đúng thứ tự hiện có.
void appendUtf8(std::string &out, std::uint32_t codePoint) {
    if (codePoint <= 0x7fu) {
        out.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7ffu) {
        out.push_back(static_cast<char>(0xc0u | (codePoint >> 6u)));
        out.push_back(static_cast<char>(0x80u | (codePoint & 0x3fu)));
    } else if (codePoint <= 0xffffu) {
        out.push_back(static_cast<char>(0xe0u | (codePoint >> 12u)));
        out.push_back(static_cast<char>(0x80u | ((codePoint >> 6u) & 0x3fu)));
        out.push_back(static_cast<char>(0x80u | (codePoint & 0x3fu)));
    } else {
        out.push_back(static_cast<char>(0xf0u | (codePoint >> 18u)));
        out.push_back(static_cast<char>(0x80u | ((codePoint >> 12u) & 0x3fu)));
        out.push_back(static_cast<char>(0x80u | ((codePoint >> 6u) & 0x3fu)));
        out.push_back(static_cast<char>(0x80u | (codePoint & 0x3fu)));
    }
}

// Chuyển một ký tự hex thành giá trị 0–15; JSON parser dùng kết quả để giải `\uXXXX` và trả sentinel khi ký tự không hợp lệ.
int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
    return -1;
}

// Điều phối quá trình phân tích của `JsonParser`.
class JsonParser {
public:
    // Khởi tạo `JsonParser` từ các tham số đầu vào; constructor lưu trạng thái ban đầu cần thiết để các phương thức của đối tượng hoạt động nhất quán.
    explicit JsonParser(const std::string &input) : input_(input) {}

    // Phân tích parse; hàm duyệt đầu vào theo ngữ pháp/định dạng quy định, tạo cấu trúc kết quả và báo lỗi khi dữ liệu không hợp lệ.
    bool parse(StackValue &result, std::string &err) {
        skipWhitespace();
        if (!parseValue(result)) {
            err = error_;
            return false;
        }
        skipWhitespace();
        if (position_ != input_.size()) {
            err = "json phân tích: còn dữ liệu sau giá trị JSON";
            return false;
        }
        return true;
    }

private:
    const std::string &input_;
    std::size_t position_ = 0;
    std::string error_;

    // Tiến con trỏ JSON parser qua các ký tự whitespace; hàm dừng ở byte đầu tiên thuộc token thực tiếp theo.
    void skipWhitespace() {
        while (position_ < input_.size()) {
            const char c = input_[position_];
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
            ++position_;
        }
    }

    // Đánh dấu thao tác/module thất bại; hàm lưu trạng thái lỗi để caller không xem thực thể là đã khởi tạo thành công.
    bool fail(const std::string &message) {
        if (error_.empty()) {
            error_ = "json phân tích: " + message + " tại vị trí " + std::to_string(position_);
        }
        return false;
    }

    // Tiêu thụ một ký tự JSON nếu nó đúng ký tự mong đợi; hàm cập nhật vị trí parser và trả `false` khi không khớp.
    bool consume(char expected) {
        if (position_ >= input_.size() || input_[position_] != expected) return false;
        ++position_;
        return true;
    }

    // Tiêu thụ literal JSON cố định như `true`, `false`, `null`; hàm so sánh substring tại vị trí hiện tại rồi tiến con trỏ khi khớp.
    bool consumeLiteral(const char *literal) {
        const std::string text(literal);
        if (input_.compare(position_, text.size(), text) != 0) return false;
        position_ += text.size();
        return true;
    }

    // Phân tích giá trị; hàm duyệt đầu vào theo ngữ pháp/định dạng quy định, tạo cấu trúc kết quả và báo lỗi khi dữ liệu không hợp lệ.
    bool parseValue(StackValue &out) {
        skipWhitespace();
        if (position_ >= input_.size()) return fail("thiếu giá trị");
        const char c = input_[position_];
        if (c == '"') {
            std::string text;
            if (!parseString(text)) return false;
            out = make_string_value(text);
            return true;
        }
        if (c == '{') return parseObject(out);
        if (c == '[') return parseArray(out);
        if (c == 't' && consumeLiteral("true")) {
            out = make_int_value(1);
            return true;
        }
        if (c == 'f' && consumeLiteral("false")) {
            out = make_int_value(0);
            return true;
        }
        if (c == 'n' && consumeLiteral("null")) {
            out = make_null_value();
            return true;
        }
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber(out);
        return fail("giá trị không hợp lệ");
    }

    // Phân tích hex4; hàm duyệt đầu vào theo ngữ pháp/định dạng quy định, tạo cấu trúc kết quả và báo lỗi khi dữ liệu không hợp lệ.
    bool parseHex4(std::uint32_t &value) {
        if (position_ + 4 > input_.size()) return fail("escape unicode chưa đủ 4 chữ số");
        value = 0;
        for (int i = 0; i < 4; ++i) {
            const int digit = hexValue(input_[position_++]);
            if (digit < 0) return fail("escape unicode không hợp lệ");
            value = (value << 4u) | static_cast<std::uint32_t>(digit);
        }
        return true;
    }

    // Phân tích chuỗi; hàm duyệt đầu vào theo ngữ pháp/định dạng quy định, tạo cấu trúc kết quả và báo lỗi khi dữ liệu không hợp lệ.
    bool parseString(std::string &out) {
        if (!consume('"')) return fail("chuỗi phải bắt đầu bằng dấu nháy");
        out.clear();
        while (position_ < input_.size()) {
            const unsigned char byte = static_cast<unsigned char>(input_[position_++]);
            if (byte == '"') return true;
            if (byte < 0x20u) return fail("chuỗi chứa ký tự điều khiển chưa escape");
            if (byte != '\\') {
                out.push_back(static_cast<char>(byte));
                continue;
            }
            if (position_ >= input_.size()) return fail("escape chuỗi bị thiếu");
            const char escaped = input_[position_++];
            switch (escaped) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    std::uint32_t first = 0;
                    if (!parseHex4(first)) return false;
                    if (first >= 0xd800u && first <= 0xdbffu) {
                        if (position_ + 2 > input_.size() || input_[position_] != '\\' ||
                            input_[position_ + 1] != 'u') {
                            return fail("surrogate unicode cao thiếu cặp thấp");
                        }
                        position_ += 2;
                        std::uint32_t second = 0;
                        if (!parseHex4(second)) return false;
                        if (second < 0xdc00u || second > 0xdfffu) {
                            return fail("surrogate unicode thấp không hợp lệ");
                        }
                        const std::uint32_t codePoint = 0x10000u +
                            ((first - 0xd800u) << 10u) + (second - 0xdc00u);
                        appendUtf8(out, codePoint);
                    } else if (first >= 0xdc00u && first <= 0xdfffu) {
                        return fail("surrogate unicode thấp không có cặp cao");
                    } else {
                        appendUtf8(out, first);
                    }
                    break;
                }
                default:
                    return fail("escape chuỗi không hợp lệ");
            }
        }
        return fail("chuỗi chưa đóng dấu nháy");
    }

    // Phân tích số; hàm duyệt đầu vào theo ngữ pháp/định dạng quy định, tạo cấu trúc kết quả và báo lỗi khi dữ liệu không hợp lệ.
    bool parseNumber(StackValue &out) {
        const std::size_t start = position_;
        if (input_[position_] == '-') ++position_;
        if (position_ >= input_.size()) return fail("số bị thiếu chữ số");
        if (input_[position_] == '0') {
            ++position_;
        } else {
            if (input_[position_] < '1' || input_[position_] > '9') return fail("số không hợp lệ");
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') {
                ++position_;
            }
        }
        bool floating = false;
        if (position_ < input_.size() && input_[position_] == '.') {
            floating = true;
            ++position_;
            const std::size_t fractionStart = position_;
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') {
                ++position_;
            }
            if (fractionStart == position_) return fail("phần thập phân bị thiếu");
        }
        if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
            floating = true;
            ++position_;
            if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-')) ++position_;
            const std::size_t exponentStart = position_;
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') {
                ++position_;
            }
            if (exponentStart == position_) return fail("số mũ bị thiếu");
        }
        const std::string text = input_.substr(start, position_ - start);
        try {
            if (!floating) {
                std::size_t consumed = 0;
                const long long integer = std::stoll(text, &consumed);
                if (consumed == text.size() &&
                    integer >= std::numeric_limits<int>::min() &&
                    integer <= std::numeric_limits<int>::max()) {
                    out = make_int_value(static_cast<int>(integer));
                    return true;
                }
            }
            std::size_t consumed = 0;
            const double number = std::stod(text, &consumed);
            if (consumed != text.size() || !std::isfinite(number)) return fail("số vượt phạm vi");
            out = make_float_value(number);
            return true;
        } catch (...) {
            return fail("số không thể chuyển đổi");
        }
    }

    // Phân tích array; hàm duyệt đầu vào theo ngữ pháp/định dạng quy định, tạo cấu trúc kết quả và báo lỗi khi dữ liệu không hợp lệ.
    bool parseArray(StackValue &out) {
        consume('[');
        skipWhitespace();
        std::vector<StackValue> elements;
        if (consume(']')) {
            out = make_list_value(std::move(elements));
            return true;
        }
        while (true) {
            StackValue value;
            if (!parseValue(value)) return false;
            elements.push_back(std::move(value));
            skipWhitespace();
            if (consume(']')) break;
            if (!consume(',')) return fail("mảng cần dấu phẩy hoặc dấu ]");
            skipWhitespace();
        }
        out = make_list_value(std::move(elements));
        return true;
    }

    // Phân tích đối tượng; hàm duyệt đầu vào theo ngữ pháp/định dạng quy định, tạo cấu trúc kết quả và báo lỗi khi dữ liệu không hợp lệ.
    bool parseObject(StackValue &out) {
        consume('{');
        skipWhitespace();
        MapValue map;
        if (consume('}')) {
            out = make_map_value(std::move(map));
            return true;
        }
        while (true) {
            std::string key;
            if (!parseString(key)) return false;
            skipWhitespace();
            if (!consume(':')) return fail("object cần dấu : sau khóa");
            StackValue value;
            if (!parseValue(value)) return false;
            map.entries[key] = std::move(value);
            skipWhitespace();
            if (consume('}')) break;
            if (!consume(',')) return fail("object cần dấu phẩy hoặc dấu }");
            skipWhitespace();
        }
        out = make_map_value(std::move(map));
        return true;
    }
};

// Mã hóa JSON; hàm chuyển cấu trúc dữ liệu sang dạng chuỗi/wire ổn định để lưu trong bytecode hoặc truyền qua ranh giới module.
bool encodeJson(const StackValue &value,
                std::string &out,
                std::unordered_set<const void *> &active,
                std::string &err) {
    if (std::holds_alternative<int>(value)) {
        out += std::to_string(std::get<int>(value));
        return true;
    }
    if (std::holds_alternative<double>(value)) {
        const double number = std::get<double>(value);
        if (!std::isfinite(number)) {
            err = "json tạo: JSON không hỗ trợ NaN hoặc vô cực";
            return false;
        }
        out += formatRuntimeFloat(number);
        return true;
    }
    if (std::holds_alternative<std::string>(value)) {
        out += '"';
        out += escapeJsonString(std::get<std::string>(value));
        out += '"';
        return true;
    }
    if (std::holds_alternative<std::monostate>(value)) {
        out += "null";
        return true;
    }
    if (std::holds_alternative<ClassHandle>(value) ||
        std::holds_alternative<InstanceHandle>(value)) {
        err = "json tạo: không hỗ trợ lớp hoặc đối tượng runtime";
        return false;
    }

    const void *identity = nullptr;
    if (std::holds_alternative<ListHandle>(value)) identity = std::get<ListHandle>(value).get();
    else if (std::holds_alternative<TupleHandle>(value)) identity = std::get<TupleHandle>(value).get();
    else identity = std::get<MapHandle>(value).get();
    if (identity != nullptr && !active.insert(identity).second) {
        err = "json tạo: phát hiện collection tự tham chiếu";
        return false;
    }

    if (std::holds_alternative<ListHandle>(value) || std::holds_alternative<TupleHandle>(value)) {
        const std::vector<StackValue> *elements = nullptr;
        if (std::holds_alternative<ListHandle>(value)) {
            const ListHandle &list = std::get<ListHandle>(value);
            static const std::vector<StackValue> empty;
            elements = list ? &list->elements : &empty;
        } else {
            const TupleHandle &tuple = std::get<TupleHandle>(value);
            static const std::vector<StackValue> empty;
            elements = tuple ? &tuple->elements : &empty;
        }
        out += '[';
        for (std::size_t i = 0; i < elements->size(); ++i) {
            if (i != 0) out += ',';
            if (!encodeJson((*elements)[i], out, active, err)) return false;
        }
        out += ']';
    } else {
        const MapHandle &map = std::get<MapHandle>(value);
        out += '{';
        bool first = true;
        if (map) {
            for (const auto &entry : map->entries) {
                if (!first) out += ',';
                first = false;
                out += '"';
                out += escapeJsonString(entry.first);
                out += "\":";
                if (!encodeJson(entry.second, out, active, err)) return false;
            }
        }
        out += '}';
    }

    if (identity != nullptr) active.erase(identity);
    return true;
}

} // namespace

// Escape nội dung chuỗi theo JSON; hàm thay dấu nháy, backslash và ký tự điều khiển bằng escape sequence hợp lệ trước khi serialize.
std::string escapeJsonString(const std::string &input) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string output;
    output.reserve(input.size() + 16);
    for (unsigned char c : input) {
        switch (c) {
            case '"': output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\b': output += "\\b"; break;
            case '\f': output += "\\f"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:
                if (c < 0x20u) {
                    output += "\\u00";
                    output.push_back(hex[(c >> 4u) & 0x0fu]);
                    output.push_back(hex[c & 0x0fu]);
                } else {
                    output.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    return output;
}

// Phân tích JSON; hàm duyệt đầu vào theo ngữ pháp/định dạng quy định, tạo cấu trúc kết quả và báo lỗi khi dữ liệu không hợp lệ.
bool parseJson(const std::string &input, StackValue &result, std::string &err) {
    return JsonParser(input).parse(result, err);
}

// Tuần tự hóa `StackValue` thành JSON; hàm đi đệ quy qua scalar/list/map và escape khóa/chuỗi theo chuẩn JSON.
bool stringifyJson(const StackValue &value, std::string &result, std::string &err) {
    result.clear();
    std::unordered_set<const void *> active;
    return encodeJson(value, result, active, err);
}

} // namespace vietvm::helpers
