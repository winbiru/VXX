#include "common/Utility.h"
#include <sstream>
#include <stdexcept>
#include <vector>

namespace vietvm::compiler {

std::string trim(const std::string &s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::pair<std::string, size_t> extractParens(const std::vector<std::string>& tokens, size_t start) {
    if (start >= tokens.size() || tokens[start] != "(")
        throw std::runtime_error("extractParens: expected '('");
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
    if (depth != 0) throw std::runtime_error("extractParens: unbalanced parentheses");
    return {trim(oss.str()), i};
}

std::pair<std::string, size_t> extractBlock(const std::vector<std::string>& tokens, size_t start) {
    if (start >= tokens.size() || tokens[start] != "{")
        throw std::runtime_error("extractBlock: expected '{'");
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
    if (depth != 0) throw std::runtime_error("extractBlock: unbalanced braces");
    return {trim(oss.str()), i};
}

std::pair<std::string, size_t> extractExpressionUntilSemicolon(const std::vector<std::string>& tokens, size_t start) {
    std::ostringstream oss;
    size_t i = start;
    while (i < tokens.size() && tokens[i] != ";") {
        oss << tokens[i] << ' ';
        ++i;
    }
    if (i < tokens.size() && tokens[i] == ";") ++i;
    return {trim(oss.str()), i};
}

std::string extractAssignedVar(const std::string& expr) {
    size_t eqPos = expr.find('=');
    if (eqPos == std::string::npos) {
        throw std::runtime_error("extractAssignedVar: no '=' found");
    }
    std::string left = expr.substr(0, eqPos);
    return trim(left);
}

} // namespace vietvm::compiler