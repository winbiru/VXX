#include "common/expression.h"

#include <iostream>

#include "frontend/lexer.h"
#include <stack>
#include <stdexcept>
#include <sstream>


namespace vietvm::compiler {

int precedence_op(const std::string& op) {
    const auto &m = operatorPrecedenceMap();
    auto it = m.find(op);
    if (it == m.end()) return -1;
    return it->second;
}

char associativity_op(const std::string &op) { return (op == "=") ? 'r' : 'l'; }

// Enhanced convertToPostfix: supports function calls.
// Function calls are emitted as a single token in postfix with format: CALL::name::argc
std::vector<std::string> convertToPostfix(const std::vector<std::string>& infix_tokens) {
    std::vector<std::string> output;
    std::stack<std::string> ops;
    std::vector<int> argCountStack;          // parallel stack to count args for current function
    std::vector<bool> argExpectingStack;     // whether we are expecting a new arg (true at start of arg list, or after comma)

    if (infix_tokens.empty()) return {};
    auto isFuncMarker = [](const std::string &s)->bool {
        return s.rfind("FUNC:", 0) == 0;
    };
    auto isIdentifierLikeToken = [](const std::string &tk)->bool {
        if (tk.empty()) return false;
        if (tk == "đúng" || tk == "sai" || tk == "rỗng") return false;
        return isVariable(tk);
    };
    auto isCallableNamePiece = [](const std::string &tk) {
        if (isVariable(tk)) return true;
        if (tk.find(' ') == std::string::npos) return false;
        std::stringstream ss(tk);
        std::string part;
        while (std::getline(ss, part, ' ')) {
            if (part.empty()) continue;
            if (!isVariable(part)) return false;
        }
        return true;
    };
    auto joinNameTokens = [](const std::vector<std::string> &tokens, size_t begin, size_t end) {
        std::string out;
        for (size_t j = begin; j < end; ++j) {
            if (!out.empty()) out.push_back(' ');
            out += tokens[j];
        }
        return out;
    };

    for (size_t i = 0; i < infix_tokens.size(); ++i) {
        const std::string &token = infix_tokens[i];
        if (token.empty()) continue;

        if (token == "{" || token == "}" || token == ";") {
            continue;
        }
        if (isCallableNamePiece(token)) {
            size_t j = i;
            while (j < infix_tokens.size() && isCallableNamePiece(infix_tokens[j])) {
                ++j;
            }
            if (j < infix_tokens.size() && infix_tokens[j] == "(") {
                std::string fname = joinNameTokens(infix_tokens, i, j);
                ops.push(std::string("FUNC:") + fname);
                i = j - 1;
                continue; // do not output function name as operand
            }
        }

        bool isSpacedIdentifier = false;
        if (token.find(' ') != std::string::npos) {
            std::stringstream ss(token);
            std::string part;
            bool sawPart = false;
            isSpacedIdentifier = true;
            while (std::getline(ss, part, ' ')) {
                if (part.empty()) continue;
                sawPart = true;
                if (!isIdentifierLikeToken(part)) {
                    isSpacedIdentifier = false;
                    break;
                }
            }
            if (!sawPart) isSpacedIdentifier = false;
        }

        if (isNumber(token) || isFloat(token) || isStringLiteral(token) || token == "rỗng" || isVariable(token) || isSpacedIdentifier) {
            output.push_back(token);
            // If we are inside a function argument list, and expecting a new arg, count it
            if (!argCountStack.empty() && argExpectingStack.back()) {
                argCountStack.back() += 1;
                argExpectingStack.back() = false;
            }
        } else if (token == ",") {
            // function arg separator: pop operators until '('
            while (!ops.empty() && ops.top() != "(") {
                output.push_back(ops.top());
                ops.pop();
            }
            if (argExpectingStack.empty()) {
                // comma outside function parens - treat as error
                throw std::runtime_error("convertToPostfix: unexpected ',' outside function call");
            }
            // next argument expected
            argExpectingStack.back() = true;
        } else if (isOperator(token)) {
            while (!ops.empty() && isOperator(ops.top())) {
                const std::string &top = ops.top();
                if ((precedence_op(top) > precedence_op(token)) ||
                    (precedence_op(top) == precedence_op(token) && associativity_op(token) == 'l')) {
                    output.push_back(top);
                    ops.pop();
                } else break;
            }
            ops.push(token);
        } else if (token == "(") {
            // If top of ops is FUNC:..., then we are entering function arg list
            if (!ops.empty() && isFuncMarker(ops.top())) {
                // push '(' as marker; start arg count
                ops.push("(");
                argCountStack.push_back(0);
                argExpectingStack.push_back(true); // ready to accept first arg (or empty)
            } else {
                ops.push("(");
            }
        } else if (token == ")") {
            // pop until '('
            bool foundLeft = false;
            while (!ops.empty()) {
                std::string top = ops.top();
                ops.pop();
                if (top == "(") {
                    foundLeft = true;
                    break;
                }
                output.push_back(top);
            }
            if (!foundLeft) throw std::runtime_error("convertToPostfix: mismatched parens");
            // if function marker exists just below, pop it and emit CALL token
            if (!ops.empty() && isFuncMarker(ops.top())) {
                std::string funcMarker = ops.top(); ops.pop();
                std::string fname = funcMarker.substr(5); // after "FUNC:"
                int argc = 0;
                if (!argCountStack.empty()) {
                    argc = argCountStack.back();
                    argCountStack.pop_back();
                    argExpectingStack.pop_back();
                }
                // Emit a CALL token in postfix with name and argc
                std::string callTok = std::string("CALL::") + fname + std::string("::") + std::to_string(argc);
                output.push_back(callTok);
                // The CALL result is a value, so count it as an arg if we're inside another function's arglist
                if (!argExpectingStack.empty() && argExpectingStack.back()) {
                    argCountStack.back() += 1;
                    argExpectingStack.back() = false;
                }
            }
        } else if (token == "(" || token == ")") {
            // handled
        } else {
            throw std::runtime_error("convertToPostfix: unknown token '" + token + "'");
        }
    }

    while (!ops.empty()) {
        if (ops.top() == "(" || ops.top() == ")")
            throw std::runtime_error("convertToPostfix: mismatched parens");
        output.push_back(ops.top());
        ops.pop();
    }

    return output;
}

} // namespace vietvm::compiler
