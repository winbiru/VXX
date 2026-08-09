// Created by nx_thang on 10/20/2025.
// Updated: bổ sung hỗ trợ comment, escape trong string, cải thiện nhận diện identifier UTF-8,
// and ổn định post-processing multi-word keywords.
// Updated: thêm error handling và exception cho các trường hợp ngoài mong đợi.

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <variant>
#include <string>
#include <unordered_map>

#include "../../include/frontend/lexer.h"
#include "vpp/core/text.h"

namespace vietvm::compiler {
    using Value = std::variant<int, double, std::string>;
    std::vector<Value> vars; // indexed by varId

    // ==================== Helper Functions Implementation ====================

    std::pair<size_t, size_t> getLineAndColumn(const std::string& src, size_t index) {
        size_t line = 1;
        size_t col = 1;
        for (size_t i = 0; i < index && i < src.size(); ++i) {
            if (src[i] == '\n') {
                ++line;
                col = 1;
            } else {
                ++col;
            }
        }
        return {line, col};
    }

    std::string getErrorContext(const std::string& src, size_t index, size_t contextLen) {
        size_t start = (index > contextLen) ? index - contextLen : 0;
        size_t end = std::min(index + contextLen, src.size());

        // Tìm đầu dòng
        while (start > 0 && src[start - 1] != '\n') --start;
        // Tìm cuối dòng
        while (end < src.size() && src[end] != '\n') ++end;

        std::string context = src.substr(start, end - start);
        // Thay thế các ký tự điều khiển
        for (char& c : context) {
            if (c == '\t') c = ' ';
            else if (c < 32 && c != '\n') c = '?';
        }
        return context;
    }

    // ==================== Core Lexer Functions ====================

    bool isNumber(const std::string &s) noexcept {
        if (s.empty()) return false;
        size_t start = (s[0] == '-') ? 1 : 0;
        if (start >= s.size()) return false;
        return std::all_of(s.begin() + start, s.end(), [](unsigned char c) {
            return std::isdigit(c);
        });
    }

    bool isFloat(const std::string &s) noexcept {
        if (s.empty()) return false;
        size_t start = (s[0] == '-') ? 1 : 0;
        if (start >= s.size()) return false;
        bool hasDot = false;
        for (size_t i = start; i < s.size(); ++i) {
            if (s[i] == '.') {
                if (hasDot) return false;  // two dots → not a float
                hasDot = true;
            } else if (!std::isdigit(static_cast<unsigned char>(s[i]))) {
                return false;
            }
        }
        return hasDot;  // must have exactly one dot
    }

    const std::unordered_map<std::string,int>& operatorPrecedenceMap() noexcept {
        static const std::unordered_map<std::string,int> ops = {
            {"=",0},{"||",1},{"&&",2},{"==",3},{"!=",3},{"<",3},{">",3},{"<=",3},{">=",3},
            {"+",4},{"-",4},{"*",5},{"/",5},{"%",5}, {"!",6},
            {"++",7},{"--",7},
            {"+=",0},{"-=",0},{"*=",0},{"/=",0},{"%=",0}
        };
        return ops;
    }

    bool isOperator(const std::string &tok) noexcept {
        const auto &m = operatorPrecedenceMap();
        return m.find(tok) != m.end();
    }

    bool isStringLiteral(const std::string &tk) noexcept {
        if (tk.size() < 2) return false;
        // Accept both double quotes ("...") and single quotes ('...')
        return (tk.front() == '"' && tk.back() == '"') ||
               (tk.front() == '\'' && tk.back() == '\'');
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
            if (uc == '_' || uc == '.') continue;
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
    // Bổ sung: error handling cho các trường hợp ngoài mong đợi
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
                    size_t commentStart = i;
                    i += 2;
                    bool closed = false;
                    while (i + 1 < n) {
                        if (src[i] == '*' && src[i+1] == '/') {
                            i += 2;
                            closed = true;
                            break;
                        }
                        ++i;
                    }
                    // Kiểm tra comment không đóng
                    if (!closed) {
                        auto [line, col] = getLineAndColumn(src, commentStart);
                        throw UnclosedCommentError(line, col);
                    }
                    continue;
                }
            }

            // String literal with escape handling (keep quotes in token)
            if (ch == '"' || ch == '\'') {
                char quote = static_cast<char>(ch);
                size_t stringStart = i;
                size_t j = i + 1;
                std::ostringstream oss;
                bool closed = false;
                while (j < n) {
                    char c = src[j];
                    if (c == '\\' && j + 1 < n) {
                        // escape sequence - validate and keep
                        char esc = src[j+1];
                        // Kiểm tra escape sequence hợp lệ (permissive mode - chỉ cảnh báo, không throw)
                        // Nếu muốn strict mode, bỏ comment dòng dưới:
                        // if (esc != 'n' && esc != 'r' && esc != 't' && esc != '\\' &&
                        //     esc != '\'' && esc != '"' && esc != '0') {
                        //     auto [line, col] = getLineAndColumn(src, j);
                        //     throw InvalidEscapeSequenceError(esc, line, col);
                        // }
                        oss.put('\\');
                        oss.put(esc);
                        j += 2;
                        continue;
                    }
                    if (c == quote) {
                        // include closing quote
                        ++j;
                        closed = true;
                        break;
                    }
                    // Kiểm tra newline không được phép trong chuỗi (trừ khi escaped)
                    if (c == '\n') {
                        auto [line, col] = getLineAndColumn(src, stringStart);
                        std::string partial = src.substr(stringStart + 1, std::min(j - stringStart - 1, (size_t)20));
                        throw UnclosedStringError(line, col, partial);
                    }
                    oss.put(c);
                    ++j;
                }
                // Kiểm tra chuỗi không đóng
                if (!closed) {
                    auto [line, col] = getLineAndColumn(src, stringStart);
                    std::string partial = src.substr(stringStart + 1, std::min(n - stringStart - 1, (size_t)20));
                    throw UnclosedStringError(line, col, partial);
                }
                std::string raw = src.substr(i, std::min(j, n) - i);
                tokens.push_back(raw);
                i = j;
                continue;
            }

            // two-char operators
            if (i + 1 < n) {
                std::string two = src.substr(i, 2);
                if (two == "==" || two == "!=" || two == "<=" || two == ">=" ||
                    two == "&&" || two == "||" || two == "++" || two == "--" ||
                    two == "+=" || two == "-=" || two == "*=" || two == "/=" || two == "%=") {
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
                c == '<' || c == '>' || c == '=' || c == '!' || c == ':'
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
                    cj == '<' || cj == '>' || cj == '=' || cj == '!' || cj == ':') break;
                ++j;
            }

            // Kiểm tra token rỗng (không nên xảy ra, nhưng phòng ngừa)
            if (j == i) {
                auto [line, col] = getLineAndColumn(src, i);
                throw InvalidCharacterError(src[i], line, col);
            }

            tokens.push_back(src.substr(i, j - i));
            i = j;
        }
        return tokens;
    }

    std::string normalizeTokenForCompare(const std::string& s) {
        std::string t = vietvm::core::trim(s);
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
            t = vietvm::core::trim(t);
            if (t.empty()) return t;
        }
        // Lowercase ASCII for comparison (keep non-ascii unchanged)
        return toLowerAscii(t);
    }

    std::vector<std::string> postProcessTokens(const std::vector<std::string>& tokens) {
        std::vector<std::string> result;
        result.reserve(tokens.size());

        auto throwMissingAccent = [](const std::string& kw) {
            throw std::runtime_error("Từ khóa phải có dấu tiếng Việt: '" + kw + "'");
        };

        // Danh sách các multi-word keywords (bằng token gốc, lower-case ASCII)
        // Nếu muốn mở rộng, thêm vào cả dạng có dấu và không dấu khi cần.
        const std::vector<std::vector<std::string>> multiKeywords = {
            {"mặc", "định"},
            {"mặc", "định:"},
            {"nếu", "không"},
            {"nếu", "không:"},
            {"trường", "hợp"},
            {"trường", "hợp:"},
            {"trả", "về"},
            {"bỏ", "qua"},
            {"khởi", "tạo"},
            {"điều", "kiện"},
            {"cập", "nhật"},
            {"kiểm", "tra", "sau"},
            {"bắt", "lỗi"},
            {"công", "khai"},
            {"riêng", "tư"},
            {"bảo", "vệ"}
        };

        for (size_t i = 0; i < tokens.size(); ++i) {
            // Chuẩn bị phiên bản để so sánh (normalize) cho token hiện tại
            std::string a_norm = normalizeTokenForCompare(tokens[i]);

            // Enforce Vietnamese diacritics for multi-word keywords
            if (i + 1 < tokens.size()) {
                std::string b_norm = normalizeTokenForCompare(tokens[i + 1]);
                if ((a_norm == "neu" && b_norm == "khong") ||
                    (a_norm == "tra" && b_norm == "ve") ||
                    (a_norm == "khoi" && b_norm == "tao") ||
                    (a_norm == "dieu" && b_norm == "kien") ||
                    (a_norm == "cap" && b_norm == "nhat") ||
                    (a_norm == "kiem" && b_norm == "tra") ||
                    (a_norm == "truong" && b_norm == "hop") ||
                    (a_norm == "mac" && b_norm == "dinh") ||
                    (a_norm == "bo" && b_norm == "qua") ||
                    (a_norm == "cong" && b_norm == "khai") ||
                    (a_norm == "rieng" && b_norm == "tu")) {
                    throwMissingAccent(a_norm + " " + b_norm);
                }

                if (i + 2 < tokens.size()) {
                    std::string c_norm = normalizeTokenForCompare(tokens[i + 2]);
                    if (a_norm == "duoc" && b_norm == "bao" && c_norm == "ve") {
                        throwMissingAccent(a_norm + " " + b_norm + " " + c_norm);
                    }
                }
            }

            // Enforce Vietnamese diacritics for single-word keywords
            if (a_norm == "neu" || a_norm == "hoac" || a_norm == "lap" ||
                a_norm == "ham" || a_norm == "goi" || a_norm == "bien" ||
                a_norm == "thoat" || a_norm == "chon" ||
                a_norm == "chuyen" || a_norm == "nem" || a_norm == "thu" ||
                a_norm == "lop") {
                throwMissingAccent(a_norm);
            }

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
                        case '0': oss.put('\0'); break;
                        default:
                            // Escape không được nhận diện - cảnh báo hoặc giữ nguyên
                            // Có thể throw InvalidEscapeSequenceError nếu muốn strict mode
                            oss.put(esc);
                            break;
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
            throw std::runtime_error("getVarValueInt: varId ngoài phạm vi (id=" + std::to_string(varId) +
                                     ", size=" + std::to_string(vars.size()) + ")");
        }
        const Value& v = vars[varId];
        if (std::holds_alternative<int>(v)) {
            return std::get<int>(v);
        }
        // nếu là chuỗi, thử chuyển sang int; nếu không parse được thì lỗi
        if (std::holds_alternative<std::string>(v)) {
            const std::string& strVal = std::get<std::string>(v);
            try {
                return std::stoi(strVal);
            } catch (const std::invalid_argument&) {
                throw std::runtime_error("getVarValueInt: giá trị biến không phải số hợp lệ: '" + strVal + "'");
            } catch (const std::out_of_range&) {
                throw std::runtime_error("getVarValueInt: giá trị số quá lớn: '" + strVal + "'");
            }
        }
        throw std::runtime_error("getVarValueInt: kiểu giá trị không hỗ trợ");
    }

    // ==================== Validation Functions Implementation ====================

    void validateNumber(const std::string& token, size_t line, size_t column) {
        if (token.empty()) {
            throw InvalidNumberError(token, line, column);
        }

        size_t start = 0;
        if (token[0] == '-' || token[0] == '+') {
            start = 1;
            if (token.size() == 1) {
                throw InvalidNumberError(token, line, column);
            }
        }

        bool hasDecimal = false;
        for (size_t i = start; i < token.size(); ++i) {
            if (token[i] == '.') {
                if (hasDecimal) {
                    throw InvalidNumberError(token, line, column);
                }
                hasDecimal = true;
            } else if (!std::isdigit(static_cast<unsigned char>(token[i]))) {
                throw InvalidNumberError(token, line, column);
            }
        }
    }

    void validateIdentifier(const std::string& token, size_t line, size_t column) {
        if (token.empty()) {
            throw LexerError("Tên định danh không được rỗng", line, column, "");
        }

        unsigned char first = static_cast<unsigned char>(token[0]);
        // Phải bắt đầu bằng chữ cái, underscore, hoặc ký tự UTF-8
        if (std::isdigit(first)) {
            throw InvalidIdentifierError(token, line, column);
        }
    }

    void validateStringLiteral(const std::string& token, size_t line, size_t column) {
        if (token.size() < 2) {
            throw LexerError("Chuỗi không hợp lệ: '" + token + "'", line, column, "");
        }

        char openQuote = token.front();
        char closeQuote = token.back();

        if ((openQuote != '"' && openQuote != '\'') || openQuote != closeQuote) {
            throw LexerError("Chuỗi không được đóng đúng cách: '" + token + "'", line, column, "");
        }
    }

} // namespace vietvm::compiler
