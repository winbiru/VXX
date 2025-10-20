//
// Created by nx_thang on 10/20/2025.
//

#include "common/Expression.h"
#include "common/Lex_utils.h"   // isNumber, isOperator, isStringLiteral, isVariable
#include <stack>
#include <stdexcept>

#include "common/Lex_utils.h"

namespace vietvm::compiler {

int precedence_op(const std::string& op) {
    const auto &m = operatorPrecedenceMap();
    auto it = m.find(op);
    if (it == m.end()) return -1;
    return it->second;
}



char associativity_op(const std::string &op) { return (op == "=") ? 'r' : 'l'; }

std::vector<std::string> convertToPostfix(const std::vector<std::string>& infix_tokens) {
    std::vector<std::string> output;
    std::stack<std::string> ops;

    for (const auto &token : infix_tokens) {
        if (token.empty()) continue;

        if (isNumber(token) || isStringLiteral(token) || isVariable(token)) {
            output.push_back(token);
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
            ops.push(token);
        } else if (token == ")") {
            while (!ops.empty() && ops.top() != "(") {
                output.push_back(ops.top());
                ops.pop();
            }
            if (ops.empty()) throw std::runtime_error("convertToPostfix: mismatched parens");
            ops.pop();
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