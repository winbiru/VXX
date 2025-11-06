//
// Created by nx_thang on 10/20/2025.
//

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <variant>

#include "common/lexer.h"
#include "common/Utility.h"

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
    std::string normalizeTokenForCompare(const std::string& s) {
    std::string t = trim(s);
    if (t.empty()) return t;
    // Nếu token là chuỗi nguyên (bắt đầu và kết thúc bằng " hoặc '), trả về nguyên bản (để không phá chuỗi)
    if ( (t.size() >= 2 && t.front() == '"' && t.back() == '"') ||
         (t.size() >= 2 && t.front() == '\'' && t.back() == '\'') ) {
        return t;
    }
    // Loại bỏ dấu câu cuối nếu là :, ; , hoặc .
    char last = t.back();
    if (last == ':' || last == ';' || last == ',' || last == '.') {
        t.pop_back();
        t = trim(t); // loại bỏ khoảng trắng dư nếu có
    }
    return t;
}

       // (Đoạn hàm postProcessTokens được cập nhật để hỗ trợ ghép từ khóa nhiều từ)
    std::vector<std::string> postProcessTokens(const std::vector<std::string>& tokens) {
        std::vector<std::string> result;
        result.reserve(tokens.size());

        // Danh sách các multi-word keywords (bằng token gốc, lower-case)
        // Nếu sau này thêm multi-word keyword, cập nhật danh sách này.
        const std::vector<std::vector<std::string>> multiKeywords = {
            {"mặc", "định"},
            {"nếu", "không"},
            {"nếu", "không:"}, // trường hợp có dấu hai chấm gắn luôn
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
                    if (tk_norm != phrase[k]) { ok = false; break; }
                }
                if (ok) {
                    // Tạo token ghép (giữ dạng không dấu/chuẩn: nối bằng space)
                    std::ostringstream oss;
                    for (size_t k = 0; k < len; ++k) {
                        if (k) oss << ' ';
                        oss << phrase[k];
                    }
                    result.push_back(oss.str());
                    i += len - 1; // nhảy qua các token đã ghép
                    matchedMulti = true;
                    break;
                }
            }

            if (matchedMulti) continue;

            // Trường hợp 1: token hiện tại đã là "mặc định" hoặc "mặc định:" (với dấu)
            if (!a_norm.empty()) {
                if (a_norm == "mặc định") {
                    result.push_back("mặc định");
                    continue;
                }
            }

            // Các xử lý khác (giữ nguyên logic cũ)
            // (copy phần xử lý token gốc ở đây, ví dụ xử lý dấu ':' nối, số, chuỗi, etc.)
            // Nếu không có xử lý đặc biệt, đẩy token gốc vào result:
            result.push_back(tokens[i]);
        }

        return result;
    }
    std::string stripQuotes(const std::string& input) {
        if (input.length() >= 2 &&
            ((input.front() == '"' && input.back() == '"') ||
             (input.front() == '\'' && input.back() == '\''))) {
            return input.substr(1, input.length() - 2);
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
} // namespace vietvm::Compiler