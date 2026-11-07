#include "common/utility.h"
#include "vpp/core/message_constants.h"
#include "vpp/core/text.h"
#include "frontend/lexer.h"
#include <sstream>
#include <stdexcept>
#include <vector>

namespace vietvm::compiler {

// Legacy postfix/parser helpers have no callers since the AST/IR migration.
// Keep their implementation for reference, but exclude them from the build.
#if 0
// Loại bỏ khoảng trắng ở đầu và cuối chuỗi; hàm tìm biên trái/phải đầu tiên không phải whitespace rồi trả lát cắt tương ứng.
std::string trim(const std::string &s) {
    return vietvm::core::trim(s);
}

// Ghép tên token; hàm nối các phần tử theo thứ tự bằng dấu phân cách quy định để tạo kết quả duy nhất.
std::string joinNameTokens(const std::vector<std::string>& tokens, size_t begin, size_t end) {
    std::string out;
    for (size_t i = begin; i < end; ++i) {
        if (!out.empty()) out.push_back(' ');
        out += tokens[i];
    }
    return out;
}

// Kiểm tra điều kiện của `isIdentifierLikeToken`.
bool isIdentifierLikeToken(const std::string &token) {
    if (token.empty()) return false;
    if (token == "đúng" || token == "sai" || token == "rỗng") return false;
    return isVariable(token);
}
#endif

// Kiểm tra điều kiện của `isCallableNamePiece`.
bool isCallableNamePiece(const std::string &token) {
    if (isVariable(token)) return true;
    if (token.find(' ') == std::string::npos) return false;
    std::stringstream ss(token);
    std::string part;
    while (std::getline(ss, part, ' ')) {
        if (part.empty()) continue;
        if (!isVariable(part)) return false;
    }
    return true;
}

// These shunting-yard-era parsing helpers have no in-tree callers.
#if 0
// Tách top level fields; hàm chia dữ liệu đầu vào theo quy tắc phân cách trong khi tôn trọng cấu trúc lồng nhau nếu có.
std::vector<std::string> splitTopLevelFields(const std::string &text,
                                             char delimiter) {
    std::vector<std::string> fields;
    std::string current;
    int parenthesisDepth = 0;
    char quote = '\0';
    bool escaped = false;

    const auto appendCurrent = [&]() {
        const std::size_t begin = current.find_first_not_of(" \t\n\r");
        const std::size_t end = current.find_last_not_of(" \t\n\r");
        if (begin == std::string::npos) fields.emplace_back();
        else fields.push_back(current.substr(begin, end - begin + 1));
        current.clear();
    };

    for (char ch : text) {
        if (quote != '\0') {
            current.push_back(ch);
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == quote) {
                quote = '\0';
            }
            continue;
        }

        if (ch == '"' || ch == '\'') {
            quote = ch;
            current.push_back(ch);
            continue;
        }

        if (ch == '(') {
            ++parenthesisDepth;
            current.push_back(ch);
        } else if (ch == ')') {
            --parenthesisDepth;
            current.push_back(ch);
        } else if (ch == delimiter && parenthesisDepth == 0) {
            appendCurrent();
        } else {
            current.push_back(ch);
        }
    }

    if (!current.empty()) appendCurrent();
    return fields;
}

// Trích xuất parens; hàm tìm phần dữ liệu cần thiết trong đầu vào và trả về lát cắt đã được chuẩn hóa.
std::pair<std::string, size_t> extractParens(const std::vector<std::string>& tokens, size_t start) {
    if (start >= tokens.size() || tokens[start] != "(")
        throw std::runtime_error(vietvm::messages::formatMessage(
            vietvm::messages::kSyntaxExpectedOpeningParen));
    std::ostringstream oss;
    int depth = 1;
    size_t i = start + 1;
    while (i < tokens.size() && depth > 0) {
        if (tokens[i] == "(") { ++depth; }
        else if (tokens[i] == ")") { --depth; if (depth == 0) { ++i; break; } }
        if (depth > 0) {
            oss << tokens[i] << ' ';
        }
        ++i;
    }
    if (depth != 0) throw std::runtime_error(vietvm::messages::formatMessage(
        vietvm::messages::kSyntaxUnbalancedParens));
    return {trim(oss.str()), i};
}

// Trích xuất khối; hàm tìm phần dữ liệu cần thiết trong đầu vào và trả về lát cắt đã được chuẩn hóa.
std::pair<std::string, size_t> extractBlock(const std::vector<std::string>& tokens, size_t start) {
    if (start >= tokens.size() || tokens[start] != "{")
        throw std::runtime_error(vietvm::messages::formatMessage(
            vietvm::messages::kSyntaxExpectedOpeningBlock));
    std::ostringstream oss;
    int depth = 1;
    size_t i = start + 1;
    while (i < tokens.size() && depth > 0) {
        if (tokens[i] == "{") { ++depth; }
        else if (tokens[i] == "}") { --depth; if (depth == 0) { ++i; break; } }
        if (depth > 0) {
            oss << tokens[i] << ' ';
        }
        ++i;
    }
    if (depth != 0) throw std::runtime_error(vietvm::messages::formatMessage(
        vietvm::messages::kSyntaxUnbalancedBraces));
    return {trim(oss.str()), i};
}

// Trích xuất assigned var; hàm tìm phần dữ liệu cần thiết trong đầu vào và trả về lát cắt đã được chuẩn hóa.
std::string extractAssignedVar(const std::string& expr) {
    size_t eqPos = expr.find('=');
    if (eqPos == std::string::npos) {
        throw std::runtime_error(vietvm::messages::formatMessage(
            vietvm::messages::kSyntaxMissingAssignmentOperator));
    }
    std::string left = expr.substr(0, eqPos);
    return trim(left);
}
#endif

} // namespace vietvm::Compiler
