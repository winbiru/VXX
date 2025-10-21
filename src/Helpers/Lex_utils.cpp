//
// Created by nx_thang on 10/20/2025.
//

#include "../include/common/Lex_utils.h"
#include <algorithm>
#include <cctype>

namespace vietvm::compiler {

    bool isNumber(const std::string &s) noexcept {
        if (s.empty()) return false;
        size_t start = (s[0] == '-') ? 1 : 0;
        return std::all_of(s.begin() + start, s.end(), [](unsigned char c) {
            return std::isdigit(c);
        });
    }

    const std::unordered_map<std::string,int>& operatorPrecedenceMap() noexcept {
        static const std::unordered_map<std::string,int> ops = {
            {"=",0},{"||",1},{"&&",2},{"==",3},{"!=",3},{"<",3},{">",3},{"<=",3},{">=",3},
            {"+",4},{"-",4},{"*",5},{"/",5},{"%",5}, {"!",6}
        };
        return ops;
    }

    bool isOperator(const std::string &tok) noexcept {
        const auto &m = operatorPrecedenceMap();
        return m.find(tok) != m.end();
    }

    bool isStringLiteral(const std::string &tk) noexcept {
        return tk.size() >= 2 && tk.front() == '"' && tk.back() == '"';
    }

    bool isVariable(const std::string &tok) noexcept {
        if (tok.empty()) return false;
        if (isStringLiteral(tok)) return false;
        if (isNumber(tok) || isOperator(tok)) return false;
        if (tok == "(" || tok == ")" || tok == "{" || tok == "}" || tok == ";" || tok == ",") return false;

        unsigned char first = static_cast<unsigned char>(tok[0]);
        if (!std::isalpha(first) && tok[0] != '_') return false;

        for (unsigned char uc : tok) {
            if (!std::isalnum(uc) && uc != '_') return false;
        }

        return true;
    }
    std::vector<std::string> tokenize(const std::string &src) {
        std::vector<std::string> tokens;
        size_t i = 0;
        while (i < src.size()) {
            auto ch = static_cast<unsigned char>(src[i]);
            if (std::isspace(ch)) { ++i; continue; }
            if (ch == '"') {
                size_t j = i + 1;
                while (j < src.size() && src[j] != '"') ++j;
                if (j < src.size()) {
                    ++j;
                    tokens.push_back(src.substr(i, j - i));
                    i = j;
                } else {
                    tokens.push_back(src.substr(i));
                    i = src.size();
                }
                continue;
            }
            if (i + 1 < src.size()) {
                std::string two = src.substr(i, 2);
                if (two == "==" || two == "!=" || two == "<=" || two == ">=" ||
                    two == "&&" || two == "||" || two == "++" || two == "--") {
                    tokens.push_back(two);
                    i += 2;
                    continue;
                    }
            }
            char c = src[i];
            if (c == '+' || c == '-' || c == '*' || c == '/' ||
                c == '(' || c == ')' || c == '{' || c == '}' ||
                c == '[' || c == ']' || c == ';' || c == ',' ||
                c == '<' || c == '>' || c == '=' || c == '!') {
                tokens.emplace_back(1, c);
                ++i;
                continue;
                }
            size_t j = i;
            while (j < src.size()) {
                auto cj = static_cast<unsigned char>(src[j]);
                if (std::isspace(cj)) break;
                if (cj == '"' || cj == '+' || cj == '-' || cj == '*' || cj == '/' ||
                    cj == '(' || cj == ')' || cj == '{' || cj == '}' ||
                    cj == '[' || cj == ']' || cj == ';' || cj == ',' ||
                    cj == '<' || cj == '>' || cj == '=' || cj == '!') break;
                ++j;
            }
            tokens.push_back(src.substr(i, j - i));
            i = j;
        }
        return tokens;
    }
} // namespace vietvm::Compiler