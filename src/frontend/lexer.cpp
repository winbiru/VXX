// Created by nx_thang on 10/20/2025.
// Updated: bổ sung hỗ trợ comment, escape trong string, cải thiện nhận diện identifier UTF-8,
// and ổn định post-processing multi-word keywords.

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <variant>
#include <string>
#include <unordered_map>

#include "../../include/frontend/lexer.h"
#include "common/utility.h"

namespace vietvm::compiler {
    using Value = std::variant<int, std::string>;
    std::vector<Value> vars; // indexed by varId

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
            {"+",4},{"-",4},{"*",5},{"/",5},{"%",5}, {"!",6},
            {"++",7}
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

    // Heuristic identifier check that tolerates UTF-8 bytes (simple & pragmatic)
    bool isVariable(const std::string &tok) noexcept {
        if (tok.empty()) return false;
        if (isStringLiteral(tok)) return false;
        if (isNumber(tok) || isOperator(tok)) return false;
        if (tok == "(" || tok == ")" || tok == "{" || tok == "}" || tok == ";" || tok == ",") return false;

        unsigned char first = static_cast<unsigned char>(tok[0]);
        // allow ascii letter or underscore or non-ascii (start of UTF-8)
        if (!std::isalpha(first) && tok[0] != '_' && first < 0x80) return false;

        for (unsigned char uc : tok) {
            if (uc == '_' ) continue;
            if (uc < 0x80) {
                if (!std::isalnum(uc)) return false;
            } else {
                // non-ascii byte: accept (assume valid UTF-8 sequence),
                // this is a permissive heuristic; a stricter implementation would validate UTF-8.
                continue;
            }
        }

        return true;
    }

    // Helper: lowercase ASCII characters (keep non-ascii unchanged)
    std::string toLowerAscii(const std::string &s) {
        std::string out;
        out.reserve(s.size());
        for (unsigned char c : s) {
            if (c < 0x80) out.push_back(static_cast<char>(std::tolower(c)));
            else out.push_back(static_cast<char>(c));
        }
        return out;
    }

    // Tokenize, with comment and improved string handling
    std::vector<std::string> tokenize(const std::string &src) {
        std::vector<std::string> tokens;
        size_t i = 0;
        const size_t n = src.size();
        while (i < n) {
            auto ch = static_cast<unsigned char>(src[i]);
            // whitespace
            if (std::isspace(ch)) { ++i; continue; }

            // comments: // line or /* block */
            if (ch == '/' && i + 1 < n) {
                unsigned char n1 = static_cast<unsigned char>(src[i+1]);
                if (n1 == '/') {
                    // skip until newline
                    i += 2;
                    while (i < n && src[i] != '\n') ++i;
                    continue;
                } else if (n1 == '*') {
                    // skip block comment
                    i += 2;
                    while (i + 1 < n) {
                        if (src[i] == '*' && src[i+1] == '/') { i += 2; break; }
                        ++i;
                    }
                    continue;
                }
            }

            // String literal with escape handling (keep quotes in token)
            if (ch == '"' || ch == '\'') {
                char quote = static_cast<char>(ch);
                size_t j = i + 1;
                std::ostringstream oss;
                while (j < n) {
                    char c = src[j];
                    if (c == '\\' && j + 1 < n) {
                        // escape sequence - keep escaped char so token includes raw content
                        char esc = src[j+1];
                        // we will store as raw text including escapes (strip later if needed)
                        oss.put('\\');
                        oss.put(esc);
                        j += 2;
                        continue;
                    }
                    if (c == quote) {
                        // include closing quote
                        ++j;
                        break;
                    }
                    oss.put(c);
                    ++j;
                }
                // if closing quote not found j may reach end; keep whatever is found
                std::string raw = src.substr(i, std::min(j, n) - i);
                tokens.push_back(raw);
                i = j;
                continue;
            }

            // two-char operators
            if (i + 1 < n) {
                std::string two = src.substr(i, 2);
                if (two == "==" || two == "!=" || two == "<=" || two == ">=" ||
                    two == "&&" || two == "||" || two == "++" || two == "--") {
                    tokens.push_back(two);
                    i += 2;
                    continue;
                }
            }

            // single-char punctuation/operators
            char c = src[i];
            if (c == '+' || c == '-' || c == '*' || c == '/' ||
                c == '(' || c == ')' || c == '{' || c == '}' ||
                c == '[' || c == ']' || c == ';' || c == ',' ||
                c == '<' || c == '>' || c == '=' || c == '!'
            ) {
                tokens.emplace_back(1, c);
                ++i;
                continue;
            }

            // identifier/number/word tokenization (UTF-8 friendly: read until separator)
            size_t j = i;
            while (j < n) {
                auto cj = static_cast<unsigned char>(src[j]);
                // stop at whitespace or any delimiter/punctuations we treat separately
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

    std::string normalizeTokenForCompare(const std::string& s) {
        std::string t = trim(s);
        if (t.empty()) return t;
        // If token is a quoted string, return as-is (do not modify inner content)
        if ( (t.size() >= 2 && t.front() == '"' && t.back() == '"') ||
             (t.size() >= 2 && t.front() == '\'' && t.back() == '\'') ) {
            return t;
        }
        // Remove trailing punctuation commonly attached to tokens
        char last = t.back();
        if (last == ':' || last == ';' || last == ',' || last == '.') {
            t.pop_back();
            t = trim(t);
            if (t.empty()) return t;
        }
        // Lowercase ASCII for comparison (keep non-ascii unchanged)
        return toLowerAscii(t);
    }

    // (Đoạn hàm postProcessTokens được cập nhật để hỗ trợ ghép từ khóa nhiều từ)
    std::vector<std::string> postProcessTokens(const std::vector<std::string>& tokens) {
        std::vector<std::string> result;
        result.reserve(tokens.size());

        // Danh sách các multi-word keywords (bằng token gốc, lower-case ASCII)
        // Nếu muốn mở rộng, thêm vào cả dạng có dấu và không dấu khi cần.
        const std::vector<std::vector<std::string>> multiKeywords = {
            {"mặc", "định"},
            {"mặc", "định:"},
            {"nếu", "không"},
            {"nếu", "không:"},
            {"trường", "hợp"},
            {"trường", "hợp:"}
            // thêm các cụm khác nếu cần
        };

        for (size_t i = 0; i < tokens.size(); ++i) {
            // Chuẩn bị phiên bản để so sánh (normalize) cho token hiện tại
            std::string a_norm = normalizeTokenForCompare(tokens[i]);

            bool matchedMulti = false;

            // Thử khớp các multi-word keywords (ưu tiên các cụm dài hơn nếu thêm)
            for (const auto &phrase : multiKeywords) {
                size_t len = phrase.size();
                if (i + len - 1 >= tokens.size()) continue;

                bool ok = true;
                for (size_t k = 0; k < len; ++k) {
                    std::string tk_norm = normalizeTokenForCompare(tokens[i + k]);
                    // so sánh chính xác với từng phần của phrase
                    // Note: phrase entries are expected to be already lower-cased ASCII-ish; we compare normalized forms
                    if (tk_norm != toLowerAscii(phrase[k])) { ok = false; break; }
                }
                if (ok) {
                    // Tạo token ghép (giữ dạng nối bằng space để tương thích)
                    std::ostringstream oss;
                    for (size_t k = 0; k < len; ++k) {
                        if (k) oss << ' ';
                        oss << phrase[k];
                    }
                    result.push_back(oss.str());
                    i += len - 1; // skip matched tokens
                    matchedMulti = true;
                    break;
                }
            }

            if (matchedMulti) continue;

            // Trường hợp token hiện tại đã là "mặc định" hoặc tương tự (đã normalize)
            if (!a_norm.empty()) {
                if (a_norm == "mặc định" || a_norm == "mặc định:") {
                    result.push_back("mặc định");
                    continue;
                }
            }

            // Giữ nguyên token gốc nếu không có xử lý đặc biệt
            result.push_back(tokens[i]);
        }

        return result;
    }

    std::string stripQuotes(const std::string& input) {
        if (input.length() >= 2 &&
            ((input.front() == '"' && input.back() == '"') ||
             (input.front() == '\'' && input.back() == '\''))) {
            // Process escape sequences inside when returning stripped content
            std::string inner = input.substr(1, input.length() - 2);
            std::ostringstream oss;
            for (size_t i = 0; i < inner.size(); ++i) {
                if (inner[i] == '\\' && i + 1 < inner.size()) {
                    char esc = inner[i+1];
                    switch (esc) {
                        case 'n': oss.put('\n'); break;
                        case 'r': oss.put('\r'); break;
                        case 't': oss.put('\t'); break;
                        case '\\': oss.put('\\'); break;
                        case '\'': oss.put('\''); break;
                        case '"': oss.put('"'); break;
                        default: oss.put(esc); break;
                    }
                    ++i; // skip escape char
                } else {
                    oss.put(inner[i]);
                }
            }
            return oss.str();
        }
        return input;
    }

    int getVarValueInt(int varId) {
        if (varId < 0 || static_cast<size_t>(varId) >= vars.size()) {
            throw std::runtime_error("getVarValueInt: varId ngoài phạm vi");
        }
        const Value& v = vars[varId];
        if (std::holds_alternative<int>(v)) {
            return std::get<int>(v);
        }
        // nếu là chuỗi, thử chuyển sang int; nếu không parse được thì lỗi
        if (std::holds_alternative<std::string>(v)) {
            try {
                return std::stoi(std::get<std::string>(v));
            } catch (...) {
                throw std::runtime_error("getVarValueInt: giá trị biến không phải số");
            }
        }
        throw std::runtime_error("getVarValueInt: kiểu giá trị không hỗ trợ");
    }

} // namespace vietvm::compiler