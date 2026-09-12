#include "common/utility.h"
#include "vpp/core/message_constants.h"
#include "vpp/core/text.h"
#include "frontend/lexer.h"
#include <sstream>
#include <stdexcept>
#include <vector>

namespace vietvm::compiler {

std::string trim(const std::string &s) {
    return vietvm::core::trim(s);
}

std::string joinNameTokens(const std::vector<std::string>& tokens, size_t begin, size_t end) {
    std::string out;
    for (size_t i = begin; i < end; ++i) {
        if (!out.empty()) out.push_back(' ');
        out += tokens[i];
    }
    return out;
}

bool isIdentifierLikeToken(const std::string &token) {
    if (token.empty()) return false;
    if (token == "đúng" || token == "sai" || token == "rỗng") return false;
    return isVariable(token);
}

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

std::vector<std::string> splitTopLevelArguments(const std::string &text) {
    return splitTopLevelFields(text, ',');
}

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

std::pair<std::string, size_t> extractExpressionUntilSemicolon(const std::vector<std::string>& tokens, size_t start) {
    std::ostringstream oss;
    size_t i = start;
    int parenDepth = 0;
    int braceDepth = 0;
    int bracketDepth = 0;

    while (i < tokens.size()) {
        const std::string &tk = tokens[i];

        if (tk == "(") ++parenDepth;
        else if (tk == ")" && parenDepth > 0) --parenDepth;
        else if (tk == "{") ++braceDepth;
        else if (tk == "}" && braceDepth > 0) --braceDepth;
        else if (tk == "[") ++bracketDepth;
        else if (tk == "]" && bracketDepth > 0) --bracketDepth;

        if (tk == ";" && parenDepth == 0 && braceDepth == 0 && bracketDepth == 0) {
            break;
        }

        oss << tokens[i] << ' ';
        ++i;
    }
    if (i < tokens.size() && tokens[i] == ";") ++i;
    return {trim(oss.str()), i};
}

std::string extractAssignedVar(const std::string& expr) {
    size_t eqPos = expr.find('=');
    if (eqPos == std::string::npos) {
        throw std::runtime_error(vietvm::messages::formatMessage(
            vietvm::messages::kSyntaxMissingAssignmentOperator));
    }
    std::string left = expr.substr(0, eqPos);
    return trim(left);
}

} // namespace vietvm::Compiler
