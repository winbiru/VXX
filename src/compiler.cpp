// compiler.cpp
#include "../include/compiler.h"              // Include file header cho compiler
#include "../include/common/Utility.h"   // để dùng Utility::trim(...)
#include <algorithm>                         // Thư viện cho các hàm như all_of, find_first_not_of
#include <sstream>                          // Để sử dụng istringstream tách dòng
#include <stdexcept>                        // Để ném lỗi runtime_error
#include <vector>                          // Dùng vector chứa token, bytecode
#include <cctype>                          // Dùng các hàm kiểm tra ký tự như isspace, isdigit
#include <iostream>                        // Dùng để debug xuất ra cerr
#include <stack>
#include <unordered_map>                   // Dùng bảng hash ánh xạ tên biến, opcode
#include "../include/instruction.h"        // Include định nghĩa Instruction, Opcode
#include "common/LoopUltil.h"

// Hàm loại bỏ khoảng trắng đầu/cuối chuỗi
static std::string trim(const std::string &s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Bảng ký hiệu toàn cục lưu tên biến -> id số nguyên
std::unordered_map<std::string, int> symbolTable;
// Biến đếm id tiếp theo
int nextSymbolIndex = 0;

/// --- 1. Helper: lấy hoặc tạo mới biến ---
static int getOrCreate(std::unordered_map<std::string,int>& symTab,
                       const std::string& name,
                       int& nextId) {
    auto it = symTab.find(name);
    if (it == symTab.end()) {
        symTab[name] = nextId;
        return nextId++;
    }
    return it->second;
}

// Hàm tách dòng code thành các token cơ bản
static std::vector<std::string> tokenize(const std::string &line) {
    std::vector<std::string> tokens;
    size_t i = 0;

    while (i < line.size()) {
        if (isspace(static_cast<unsigned char>(line[i]))) { ++i; continue; }

        if (i + 1 < line.size()) {
            std::string two = line.substr(i, 2);
            if (two == "==" || two == "!=" || two == "<=" || two == ">=" ||
                two == "&&" || two == "||" || two == "++" || two == "--") {
                tokens.push_back(two);
                i += 2;
                continue;
            }
        }

        char c = line[i];
        if (c == '+' || c == '-' || c == '*' || c == '/' ||
            c == '(' || c == ')' || c == '{' || c == '}' ||
            c == '[' || c == ']' || c == ';' || c == ',' ||
            c == '<' || c == '>' || c == '=' || c == '!') {
            tokens.emplace_back(1, c);
            ++i;
            continue;
        }

        size_t j = i;
        while (j < line.size() && !isspace(static_cast<unsigned char>(line[j])) &&
               line[j] != '+' && line[j] != '-' && line[j] != '*' && line[j] != '/' &&
               line[j] != '(' && line[j] != ')' && line[j] != '{' && line[j] != '}' &&
               line[j] != '[' && line[j] != ']' && line[j] != ';' && line[j] != ',' &&
               line[j] != '<' && line[j] != '>' && line[j] != '=' && line[j] != '!') {
            ++j;
        }
        tokens.push_back(line.substr(i, j - i));
        i = j;
    }
    return tokens;
}

// Hàm kiểm tra một chuỗi có phải số nguyên (có thể âm)
bool isNumber(const std::string& s) {
    if (s.empty()) return false;
    size_t start = (s[0] == '-') ? 1 : 0;
    return std::all_of(s.begin() + start, s.end(), ::isdigit);
}

// --- Hàm hỗ trợ cần thiết ---
bool isOperator(const std::string& token) {
    static const std::unordered_map<std::string, int> ops = {
        {"=", 0}, {"||", 1}, {"&&", 2},
        {"==", 3}, {"!=", 3}, {"<", 3}, {">", 3}, {"<=", 3}, {">=", 3},
        {"+", 4}, {"-", 4}, {"*", 5}, {"/", 5}, {"%", 5}
    };
    return ops.count(token) > 0;
}
int precedence(const std::string& op) {
    if (op == "=") return 0;
    if (op == "||") return 1;
    if (op == "&&") return 2;
    if (op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=") return 3;
    if (op == "+" || op == "-") return 4;
    if (op == "*" || op == "/" || op == "%") return 5;
    return -1;
}
char associativity(const std::string& op) {
    if (op == "=") return 'r';
    if (op == "!") return 'r';
    return 'l';
}
bool isVariable(const std::string& token) {
    if (token.empty()) return false;
    if (isNumber(token)) return false;
    if (isOperator(token)) return false;
    if (token == "(" || token == ")" || token == "{" || token == "}") return false;
    if (token == "[" || token == "]") return false;
    if (token == ";" || token == ",") return false;
    return true;
}

// --- Helper: inline compile block from token list ---
// Định nghĩa trước để có thể gọi trong compileLoop
void compileBlockInline(const std::vector<std::string>& tokens,
                        std::vector<Instruction>& bytecode,
                        std::unordered_map<std::string,int>& symTab,
                        int& nextId,
                        const std::unordered_map<std::string,Opcode>& keywordMap)
{
    std::ostringstream line;
    for (const auto& tk : tokens) {
        if (tk == ";") {
            compileExpr(line.str(), bytecode, symTab, nextId, keywordMap);
            bytecode.push_back({OP_DONG_LENH, 0, 0});
            line.str(""); line.clear();
        } else {
            line << tk << " ";
        }
    }
    if (!line.str().empty()) {
        compileExpr(line.str(), bytecode, symTab, nextId, keywordMap);
    }
}

// --- Hàm Chính: convertToPostfix ---
std::vector<std::string> convertToPostfix(const std::vector<std::string>& infix_tokens) {
    std::vector<std::string> output;
    std::stack<std::string> op_stack;

    for (const std::string& token : infix_tokens) {
        if (token.empty()) continue;

        if (isNumber(token) || isVariable(token)) {
            output.push_back(token);
        } else if (isOperator(token)) {
            while (!op_stack.empty() && isOperator(op_stack.top())) {
                const std::string& top_op = op_stack.top();
                bool condition = (
                    precedence(top_op) > precedence(token) ||
                    (precedence(top_op) == precedence(token) && associativity(token) == 'l')
                );
                if (condition) {
                    output.push_back(top_op);
                    op_stack.pop();
                } else break;
            }
            op_stack.push(token);
        } else if (token == "(") {
            op_stack.push(token);
        } else if (token == ")") {
            while (!op_stack.empty() && op_stack.top() != "(") {
                output.push_back(op_stack.top());
                op_stack.pop();
            }
            if (op_stack.empty()) {
                throw std::runtime_error("Lỗi cú pháp: Ngoặc đơn không khớp.");
            }
            op_stack.pop();
        } else {
            throw std::runtime_error("Lỗi: Token không xác định trong biểu thức: " + token);
        }
    }

    while (!op_stack.empty()) {
        if (op_stack.top() == "(" || op_stack.top() == ")") {
            throw std::runtime_error("Lỗi cú pháp: Ngoặc đơn không khớp.");
        }
        output.push_back(op_stack.top());
        op_stack.pop();
    }

    return output;
}

// --- 2. Biên dịch biểu thức (Phiên bản RPN) ---
void compileExpr(const std::string& expr,
                 std::vector<Instruction>& bytecode,
                 std::unordered_map<std::string,int>& symTab,
                 int& nextId,
                 const std::unordered_map<std::string,Opcode>& keywordMap
                 )
{
    auto toks = tokenize(trim(expr));
    auto eq_it = std::find(toks.begin(), toks.end(), "=");
    if (eq_it != toks.end()) {
        if (std::distance(toks.begin(), eq_it) != 1) {
            throw std::runtime_error("Lỗi cú pháp gán: LHS phải là một tên biến đơn.");
        }

        std::string var_name = toks[0];
        int dst_id = getOrCreate(symTab, var_name, nextId);

        std::vector<std::string> rhs_toks(eq_it + 1, toks.end());
        std::vector<std::string> clean_tokens;
        for (const auto& t : rhs_toks) {
            if (t != "{" && t != "}") {
                clean_tokens.push_back(t);
            }
        }
        std::vector<std::string> postfix_tokens = convertToPostfix(clean_tokens);

        for (const auto& token : postfix_tokens) {
            if (isNumber(token)) {
                int value = std::stoi(token);
                bytecode.push_back({OP_BIEN_SO, 0, value});  // ✅ operand nằm ở vị trí thứ 3
            }
            else if (isVariable(token)) {
                int varId = getOrCreate(symTab, token, nextId);
                bytecode.push_back({OP_TEN_BIEN_GIA_TRI, 0, varId});
            }
            else if (token == "+")  bytecode.push_back({OP_CONG, 0, 0});
            else if (token == "-")  bytecode.push_back({OP_TRU, 0, 0});
            else if (token == "*")  bytecode.push_back({OP_NHAN, 0, 0});
            else if (token == "/")  bytecode.push_back({OP_CHIA, 0, 0});
            else if (token == "%")  bytecode.push_back({OP_MODULO, 0, 0});
            else if (token == "==")  bytecode.push_back({OP_SO_SANH_BANG, 0, 0});
            else throw std::runtime_error("Toán tử chưa hỗ trợ: " + token);
        }

        bytecode.push_back({OP_TEN_BIEN_ID, dst_id, 0});
        bytecode.push_back({OP_GAN, 0, 0});
        return;
    }
    std::vector<std::string> clean_tokens;
    for (const auto& t : toks) {
        if (t != "{" && t != "}") {
            clean_tokens.push_back(t);
        }
    }
    std::vector<std::string> postfix_tokens = convertToPostfix(clean_tokens);
    for (const auto& token : postfix_tokens) {
        if (isNumber(token)) {
            if (isNumber(token)) {
                int value = std::stoi(token);
                bytecode.push_back({OP_BIEN_SO, 0, value});  // ✅ operand nằm ở vị trí thứ 3
            }
        }
        else if (isVariable(token)) {
            int varId = getOrCreate(symTab, token, nextId);
            bytecode.push_back({OP_TEN_BIEN_GIA_TRI, 0, varId});
        }
        else {
            if      (token == "+")  bytecode.push_back({OP_CONG, 0, 0});
            else if (token == "-")  bytecode.push_back({OP_TRU, 0, 0});
            else if (token == "*")  bytecode.push_back({OP_NHAN, 0, 0});
            else if (token == "/")  bytecode.push_back({OP_CHIA, 0, 0});
            else if (token == "%")  bytecode.push_back({OP_MODULO, 0, 0});
            else if (token == "==") bytecode.push_back({OP_SO_SANH_BANG, 0, 0});
            else if (token == "!=") bytecode.push_back({OP_KHAC_BANG, 0, 0});
            else if (token == "<")  bytecode.push_back({OP_NHO_HON, 0, 0});
            else if (token == ">")  bytecode.push_back({OP_LON_HON, 0, 0});
            else if (token == "<=") bytecode.push_back({OP_NHO_HON_HOAC_BANG, 0, 0});
            else if (token == ">=") bytecode.push_back({OP_LON_HON_HOAC_BANG, 0, 0});
            else if (token == "&&") bytecode.push_back({OP_Logic_VA, 0, 0});
            else if (token == "||") bytecode.push_back({OP_Logic_HOAC, 0, 0});
            else throw std::runtime_error("Toán tử chưa hỗ trợ: " + token);
        }
    }
}

// --- 3. Biên dịch token đơn lẻ ---
void compileToken(const std::string& tok,
                  std::vector<Instruction>& bytecode,
                  std::unordered_map<std::string,int>& symTab,
                  int& nextId,
                  const std::unordered_map<std::string,Opcode>& keywordMap)
{
    if (keywordMap.count(tok)) {
        bytecode.push_back({keywordMap.at(tok), 0, 0});
    }
    else if (isNumber(tok)) {
        bytecode.push_back({OP_BIEN_SO, std::stoi(tok), 0});
    }
    else {
        int vid = getOrCreate(symTab, tok, nextId);
        bytecode.push_back({OP_TEN_BIEN_GIA_TRI, 0, vid});
    }
}

// Thêm struct này ở đầu file compiler.cpp (hoặc trong file header)
struct LoopIndices {
    int cond_start_index; // Địa chỉ cho OP_JUMP (nhảy ngược)
    int exit_jump_index;  // Địa chỉ của OP_JUMP_IF_FALSE (cần backpatching)
};

// --- Overload wrapper để tương thích các chỗ gọi cũ ---
// Gọi phiên bản đầy đủ bên dưới với body rỗng và keywordMap rỗng.
// Giữ nguyên chữ ký cũ: compileLoop(parts, bytecode, symTab, nextId)



// --- 4. Sinh phần (init; cond; update) của vòng lặp (phiên bản đầy đủ) ---
LoopIndices compileLoop(const std::vector<std::string> &parts,
                        const std::string &bodyCode,  // 👈 thân vòng dạng chuỗi
                        std::vector<Instruction> &bytecode,
                        std::unordered_map<std::string, int> &symTab,
                        int &nextId,
                        const std::unordered_map<std::string, Opcode> &keywordMap)
{
    if (parts.size() != 3)
        throw std::runtime_error("Cú pháp lặp sai");

    // --- Bắt đầu vòng ---
    bytecode.push_back({OP_LAP, 0, 0});
    bytecode.push_back({OP_MO_NGOAC, 0, 0});

    // --- 2️⃣ Ghi nhớ vị trí điều kiện ---
    int cond_index = bytecode.size();
    bytecode.push_back({OP_DIEU_KIEN, 0, 0});
    compileExpr(parts[1], bytecode, symTab, nextId, keywordMap);

    // --- 3️⃣ Nếu sai thì nhảy ra ---
    bytecode.push_back({OP_JUMP_IF_FALSE, 0, 0});
    int jmp_exit_index = bytecode.size() - 1;

    // --- 4️⃣ Thân vòng lặp ---
    bytecode.push_back({OP_MO_KHOI, 0, 0});
    compileBlock(bodyCode, bytecode, symTab, nextId, keywordMap);

    // --- 5️⃣ Cập nhật ---
    bytecode.push_back({OP_CAP_NHAT, 0, 0});
    compileExpr(parts[2], bytecode, symTab, nextId, keywordMap);

    bytecode.push_back({OP_DONG_KHOI, 0, 0});


    // --- 6️⃣ Nhảy ngược về điều kiện ---
    bytecode.push_back({OP_JUMP, 0, cond_index});

    // --- 7️⃣ Gắn nhãn nhảy ra ---
    int end_index = bytecode.size();
    bytecode[jmp_exit_index].operand = end_index;

    // --- 8️⃣ Đóng vòng ---
    bytecode.push_back({OP_DONG_NGOAC, 0, 0});
    bytecode.push_back({OP_DONG_LENH, 0, 0});

    return {cond_index, jmp_exit_index};
}


// --- compileCondition (IF/WHILE) ---
int compileCondition(const std::vector<std::string> &parts,
                     std::vector<Instruction> &bytecode,
                     std::unordered_map<std::string, int> &symTab,
                     int &nextId)
{
    if (parts.size() != 1) throw std::runtime_error("Cú pháp điều kiện sai");

    bytecode.push_back({OP_NEU, 0, 0});
    bytecode.push_back({OP_MO_NGOAC, 0, 0});
    compileExpr(parts[0], bytecode, symTab, nextId, keywordMap);
    bytecode.push_back({OP_JUMP_IF_FALSE, 0, 0});
    int jump_instruction_index = bytecode.size() - 1;
    bytecode.push_back({OP_DONG_NGOAC, 0, 0});
    return jump_instruction_index;
}

// --- 5. Biên dịch block { ... } (từ string source) ---
void compileBlock(const std::string& src,
                  std::vector<Instruction>& bytecode,
                  std::unordered_map<std::string,int>& symTab,
                  int& nextId,
                  const std::unordered_map<std::string,Opcode>& keywordMap)
{
    auto tokens = tokenize(src);
    size_t i = 0;
    while (i < tokens.size()) {
        const std::string& tk = tokens[i];
        if (tk == "{") {
            ++i;
            continue;
        }
        // --- Lệnh in ---
        if (keywordMap.count(tk) && keywordMap.at(tk) == OP_IN) {
            ++i;
            int vid = getOrCreate(symTab, tokens[i], nextId);
            bytecode.push_back({OP_TEN_BIEN_GIA_TRI, 0, vid});
            bytecode.push_back({OP_IN, 0, 0});
            ++i;
            continue;
        }

        // --- Lệnh nếu ---
        if (keywordMap.count(tk) && keywordMap.at(tk) == OP_NEU) {
            size_t j = i + 2; int d = 1;
            std::ostringstream cond;
            while (j < tokens.size() && d > 0) {
                if (tokens[j] == "(") ++d;
                else if (tokens[j] == ")") --d;
                if (d > 0) cond << tokens[j] << " ";
                ++j;
            }

            // auto parts = splitLoopParts(cond.str());
            std::vector<std::string> parts = {cond.str()};
            int jump_index = compileCondition(parts, bytecode, symTab, nextId);

            // xử lý khối { ... }
            if (j < tokens.size() && tokens[j] == "{") {
                size_t k = j + 1; int bc = 1;
                std::ostringstream body;
                while (k < tokens.size() && bc > 0) {
                    if (tokens[k] == "{") ++bc;
                    else if (tokens[k] == "}") --bc;
                    if (bc > 0) body << tokens[k] << " ";
                    ++k;
                }

                bytecode.push_back({OP_MO_KHOI, 0, 0});
                compileBlock(body.str(), bytecode, symTab, nextId, keywordMap);
                bytecode.push_back({OP_DONG_KHOI, 0, 0});

                bytecode[jump_index].operand = bytecode.size();
                i = k;
                continue;
            }

            i = j;
            continue;
        }

        // --- Biểu thức thông thường ---
        std::ostringstream expr;
        while (i < tokens.size() && tokens[i] != ";") {
            expr << tokens[i] << " ";
            ++i;
        }
        compileExpr(expr.str(), bytecode, symTab, nextId, keywordMap);
        bytecode.push_back({OP_DONG_LENH, 0, 0});
        ++i;
    }
}
std::vector<Instruction> compileSource(const std::string& source,
                                       const std::unordered_map<std::string,Opcode>& keywordMap)
{
    std::vector<Instruction> bytecode;
    std::unordered_map<std::string,int> symTab;
    int nextId = 0;

    std::istringstream iss(source);
    std::string line;
    while (std::getline(iss, line)) {
        line = trim(line);
        if (line.empty()) continue;

        auto tokens = tokenize(line);

        for (size_t i = 0; i < tokens.size(); ++i) {
            const auto& tk = tokens[i];
            if (tk == "}") continue;

            if (tk == ";") {
                bytecode.push_back({OP_DONG_LENH,0,0});
                continue;
            }

            // ✅ Xử lý vòng lặp
            if (keywordMap.count(tk) && keywordMap.at(tk) == OP_LAP) {
                size_t j = i+2; int d=1;
                std::ostringstream oss;
                while (j < tokens.size() && d > 0) {
                    if (tokens[j] == "(") ++d;
                    else if (tokens[j] == ")") --d;
                    if (d > 0) {
                        oss << tokens[j];
                        if (tokens[j] != ";") oss << " ";
                    }
                    ++j;
                }

                auto parts = splitLoopParts(oss.str());

                // ✅ Đọc thêm dòng để gom toàn bộ khối { ... }
                std::ostringstream block;
                int braceDepth = 0;
                do {
                    if (line.find("{") != std::string::npos) ++braceDepth;
                    if (line.find("}") != std::string::npos) --braceDepth;
                    block << line << "\n";
                } while (braceDepth > 0 && std::getline(iss, line));

                // ✅ Tách lại token từ toàn bộ khối
                auto bodyTokens = tokenize(block.str());

                // ✅ Gom phần thân khối từ token
                std::ostringstream body;
                int depth = 0;
                for (size_t k = j; k < bodyTokens.size(); ++k) {
                    if (bodyTokens[k] == "{") ++depth;
                    else if (bodyTokens[k] == "}") --depth;
                    if (depth > 0) body << bodyTokens[k] << " ";
                }

                compileLoop(parts, body.str(), bytecode, symbolTable, nextId, keywordMap);
                break; // vì đã xử lý toàn bộ khối rồi
            }


            // ✅ Xử lý điều kiện neu
            if (keywordMap.count(tk) && keywordMap.at(tk) == OP_NEU) {
                size_t j = i+2; int d=1;
                std::ostringstream oss;
                while (j < tokens.size() && d > 0) {
                    if (tokens[j] == "(") ++d;
                    else if (tokens[j] == ")") --d;
                    if (d > 0) oss << tokens[j] << " ";
                    ++j;
                }

                auto parts = splitLoopParts(oss.str());
                int jump_index = compileCondition(parts, bytecode, symTab, nextId);

                if (j < tokens.size() && tokens[j] == "{") {
                    size_t k = j+1; int bc = 1;
                    std::ostringstream bs;
                    while (k < tokens.size() && bc > 0) {
                        if (tokens[k] == "{") ++bc;
                        else if (tokens[k] == "}") --bc;
                        if (bc > 0) bs << tokens[k] << " ";
                        ++k;
                    }
                    std::cerr << "[DEBUG] body of loop: " << bs.str() << "\n";
                    bytecode.push_back({OP_MO_KHOI,0,0});
                    compileBlock(bs.str(), bytecode, symbolTable, nextId, keywordMap);
                    bytecode.push_back({OP_DONG_KHOI,0,0});
                    int jump_target_index = bytecode.size();
                    bytecode[jump_index].operand = jump_target_index;
                    i = k - 1;
                    continue;
                }
                i = j;
                continue;
            }
            if (keywordMap.count(tk) && keywordMap.at(tk) == OP_IN) {
                ++i;
                int vid = getOrCreate(symTab, tokens[i], nextId);
                bytecode.push_back({OP_TEN_BIEN_GIA_TRI,0,vid});
                bytecode.push_back({OP_IN,0,0});
                continue;
            }
            // compileToken(tk, bytecode, symbolTable, nextId, keywordMap);
            std::ostringstream expr;
            while (i < tokens.size() && tokens[i] != ";") {
                expr << tokens[i] << " ";
                ++i;
            }
            compileExpr(expr.str(), bytecode, symTab, nextId, keywordMap);
            bytecode.push_back({OP_DONG_LENH, 0, 0});
        }
    }

    bytecode.push_back({OP_DUNG_CHUONG_TRINH,0,0});
    return bytecode;
}

