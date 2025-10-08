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
#include "../include/common/Utility.h"
#include "../include/instruction.h"        // Include định nghĩa Instruction, Opcode
#include "common/LoopUltil.h"

// Hàm loại bỏ khoảng trắng đầu/cuối chuỗi
static std::string trim(const std::string &s) {
    size_t start = s.find_first_not_of(" \t\r\n");  // Tìm vị trí ký tự đầu tiên không phải khoảng trắng
    if (start == std::string::npos) return "";      // Nếu toàn khoảng trắng trả về chuỗi rỗng
    size_t end = s.find_last_not_of(" \t\r\n");     // Tìm vị trí ký tự cuối cùng không phải khoảng trắng
    return s.substr(start, end - start + 1);        // Trả về substring đã cắt khoảng trắng đầu cuối
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
    std::vector<std::string> tokens;       // Mảng token kết quả
    size_t i = 0;                         // Chỉ số đọc trên dòng

    while (i < line.size()) {
        if (isspace(line[i])) {           // Bỏ qua khoảng trắng
            ++i;
            continue;
        }

        // Kiểm tra toán tử 2 ký tự như ==, !=, <=, >=, &&, ||, ++, --
        if (i + 1 < line.size()) {
            std::string two = line.substr(i, 2);
            if (two == "==" || two == "!=" || two == "<=" || two == ">=" ||
                two == "&&" || two == "||" || two == "++" || two == "--") {
                tokens.push_back(two);     // Thêm token toán tử 2 ký tự
                i += 2;                   // Nhảy 2 bước
                continue;
            }
        }

        // Kiểm tra toán tử/dấu đơn ký tự như + - * / ( ) { } ; , < > = !
        char c = line[i];
        if (c == '+' || c == '-' || c == '*' || c == '/' ||
            c == '(' || c == ')' || c == '{' || c == '}' ||
            c == '[' || c == ']' || c == ';' || c == ',' ||
            c == '<' || c == '>' || c == '=' || c == '!') {
            tokens.emplace_back(1, c);    // Thêm token đơn ký tự
            ++i;                         // Nhảy 1 bước
            continue;
        }

        // Token dạng từ hoặc số: lấy chuỗi liên tục đến khi gặp khoảng trắng hoặc toán tử
        size_t j = i;
        while (j < line.size() && !isspace(line[j]) &&
               line[j] != '+' && line[j] != '-' && line[j] != '*' && line[j] != '/' &&
               line[j] != '(' && line[j] != ')' && line[j] != '{' && line[j] != '}' &&
               line[j] != '[' && line[j] != ']' && line[j] != ';' && line[j] != ',' &&
               line[j] != '<' && line[j] != '>' && line[j] != '=' && line[j] != '!') {
            j++;
        }
        tokens.push_back(line.substr(i, j - i)); // Thêm token vừa lấy
        i = j;                                  // Nhảy đến vị trí mới
    }
    return tokens;                              // Trả về danh sách token
}
// Hàm kiểm tra một chuỗi có phải số nguyên (có thể âm)
bool isNumber(const std::string& s) {
    if (s.empty()) return false;
    size_t start = (s[0] == '-') ? 1 : 0;
    return std::all_of(s.begin() + start, s.end(), ::isdigit);
}
// --- Hàm hỗ trợ cần thiết ---

// 1. Kiểm tra xem token có phải là toán tử không
bool isOperator(const std::string& token) {
    static const std::unordered_map<std::string, int> ops = {
        {"=", 0}, {"||", 1}, {"&&", 2},
        {"==", 3}, {"!=", 3}, {"<", 3}, {">", 3}, {"<=", 3}, {">=", 3},
        {"+", 4}, {"-", 4}, {"*", 5}, {"/", 5}, {"%", 5}
    };
    return ops.count(token) > 0;
}

// 2. Định nghĩa độ ưu tiên của toán tử (Giá trị càng lớn, ưu tiên càng cao)
int precedence(const std::string& op) {
    if (op == "=") return 0;
    if (op == "||") return 1;
    if (op == "&&") return 2;
    if (op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=") return 3;
    if (op == "+" || op == "-") return 4;
    if (op == "*" || op == "/" || op == "%") return 5;
    return -1; // Không phải toán tử
}

// 3. Định nghĩa tính kết hợp (Associativity: left 'l' hoặc right 'r')
char associativity(const std::string& op) {
    if (op == "=") return 'r'; // Gán thường kết hợp phải (a = b = c)
    if (op == "!") return 'r'; // Phủ định
    // Hầu hết các toán tử khác (số học, logic, so sánh) kết hợp trái
    return 'l';
}

// Giả định hàm này kiểm tra token có phải là hằng số (số) không
bool isNumber(const std::string& token);

// Giả định hàm này kiểm tra token có phải là biến không
bool isVariable(const std::string& token) {
    if (token.empty()) return false;
    // 1. Không phải là số
    if (isNumber(token)) return false;
    // 2. Không phải là toán tử đã định nghĩa
    if (isOperator(token)) return false;
    // 3. Không phải là dấu ngoặc
    if (token == "(" || token == ")" || token == "{" || token == "}") return false;
    if (token == "[" || token == "]") return false;
    if (token == ";" || token == ",") return false;

    // Thêm các từ khóa/lệnh đơn của ngôn ngữ của bạn nếu cần (ví dụ: 'in', 'neu', 'lap')

    // Nếu nó không phải là bất kỳ loại token đặc biệt nào khác, ta xem nó là một biến.
    return true;
}


// --- Hàm Chính: convertToPostfix ---

/**
 * Sử dụng thuật toán Shunting-yard để chuyển biểu thức từ trung tố sang hậu tố.
 * @param infix_tokens Vector chứa các token biểu thức ở dạng trung tố (ví dụ: {"i", "%", "2", "==", "0"})
 * @return Vector chứa các token ở dạng hậu tố RPN (ví dụ: {"i", "2", "%", "0", "=="})
 */
std::vector<std::string> convertToPostfix(const std::vector<std::string>& infix_tokens) {
    std::vector<std::string> output;
    std::stack<std::string> op_stack;

    for (const std::string& token : infix_tokens) {
        if (token.empty()) continue;

        if (isNumber(token) || isVariable(token)) {
            // 1. Nếu là toán hạng (biến hoặc hằng số), đẩy vào Output.
            output.push_back(token);
        } else if (isOperator(token)) {
            // 2. Nếu là toán tử
            while (!op_stack.empty() && isOperator(op_stack.top())) {
                const std::string& top_op = op_stack.top();

                // Điều kiện dừng:
                // a) Nếu độ ưu tiên của toán tử trên stack LỚN HƠN (hoặc bằng nếu kết hợp trái)
                // b) Và toán tử hiện tại KHÔNG phải là toán tử kết hợp phải CÓ ĐỘ ƯU TIÊN BẰNG
                bool condition = (
                    precedence(top_op) > precedence(token) ||
                    (precedence(top_op) == precedence(token) && associativity(token) == 'l')
                );

                if (condition) {
                    output.push_back(top_op);
                    op_stack.pop();
                } else {
                    break;
                }
            }
            op_stack.push(token);
        } else if (token == "(") {
            // 3. Mở ngoặc, đẩy vào Stack.
            op_stack.push(token);
        } else if (token == ")") {
            // 4. Đóng ngoặc, di chuyển tất cả từ Stack sang Output cho đến khi gặp mở ngoặc.
            while (!op_stack.empty() && op_stack.top() != "(") {
                output.push_back(op_stack.top());
                op_stack.pop();
            }
            if (op_stack.empty()) {
                throw std::runtime_error("Lỗi cú pháp: Ngoặc đơn không khớp.");
            }
            op_stack.pop(); // Loại bỏ "(" khỏi Stack
        } else {
            throw std::runtime_error("Lỗi: Token không xác định trong biểu thức: " + token);
        }
    }

    // 5. Sau khi xử lý hết token, di chuyển tất cả toán tử còn lại trong Stack sang Output.
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
                 int& nextId)
{
    // Giả định: Các hàm tokenize, trim, isNumber, isVariable đã tồn tại
    auto toks = tokenize(trim(expr));
    auto eq_it = std::find(toks.begin(), toks.end(), "=");
    if (eq_it != toks.end()) {
        // Đây là câu lệnh gán: LHS = RHS

        if (std::distance(toks.begin(), eq_it) != 1) {
            throw std::runtime_error("Lỗi cú pháp gán: LHS phải là một tên biến đơn.");
        }

        std::string var_name = toks[0];
        int dst_id = getOrCreate(symTab, var_name, nextId);

        // Tạo chuỗi biểu thức RHS (ví dụ: "b + c" hoặc "i % 2 == 0")
        std::string rhs_expr;
        for (auto it = eq_it + 1; it != toks.end(); ++it) {
            rhs_expr += *it;
            rhs_expr += " ";
        }

        // Đệ quy gọi compileExpr (hoặc tạo hàm compileMathExpr riêng) để xử lý RHS
        // Tạm thời, gọi lại compileExpr trên RHS đã được tokenize
        std::vector<std::string> rhs_toks(eq_it + 1, toks.end());

        // BƯỚC 1: Chuyển RHS thành Postfix
        std::vector<std::string> postfix_tokens = convertToPostfix(rhs_toks);

        // BƯỚC 2: Sinh Bytecode cho RHS
        for (const auto& token : postfix_tokens) {
            // Logic RPN cũ (chỉ dành cho toán tử/toán hạng, không có gán)
            if (isNumber(token)) {
                bytecode.push_back({OP_BIEN_SO, std::stoi(token), 0});
            }
            else if (isVariable(token)) {
                int varId = getOrCreate(symTab, token, nextId);
                bytecode.push_back({OP_TEN_BIEN_GIA_TRI, 0, varId});
            }
            // ... (Phần xử lý toán tử ở đây, giống như logic cũ) ...
            else if (token == "+")  bytecode.push_back({OP_CONG, 0, 0});
            else if (token == "-")  bytecode.push_back({OP_TRU, 0, 0});
            else if (token == "*")  bytecode.push_back({OP_NHAN, 0, 0});
            else if (token == "/")  bytecode.push_back({OP_CHIA, 0, 0});
            else if (token == "%")  bytecode.push_back({OP_MODULO, 0, 0});
            // ... các toán tử so sánh, logic khác ...
            else throw std::runtime_error("Toán tử chưa hỗ trợ: " + token);
        }

        // BƯỚC 3: Thực hiện GÁN
        // Giá trị RHS đã nằm trên đỉnh stack. Ta đẩy ID biến đích lên
        bytecode.push_back({OP_TEN_BIEN_ID, dst_id, 0});
        bytecode.push_back({OP_GAN, 0, 0});

        return;
    }
    // KẾT THÚC XỬ LÝ GÁN

    // Nếu không phải là gán, nó là một biểu thức đơn giản (ví dụ: x < 10)
    // Thực hiện RPN thông thường để lại kết quả trên stack
    std::vector<std::string> postfix_tokens = convertToPostfix(toks);

    for (const auto& token : postfix_tokens) {
        if (isNumber(token)) {
            bytecode.push_back({OP_BIEN_SO, std::stoi(token), 0});
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
                  const std::unordered_map<std::string,Opcode>& kwMap)
{
    if (kwMap.count(tok)) {
        bytecode.push_back({kwMap.at(tok), 0, 0});
    }
    else if (isNumber(tok)) {
        bytecode.push_back({OP_BIEN_SO, std::stoi(tok), 0});
    }
    else {
        // đọc giá trị biến khi xuất hiện đơn lẻ (ví dụ in x; hay trong expr đơn)
        int vid = getOrCreate(symTab, tok, nextId);
        bytecode.push_back({OP_TEN_BIEN_GIA_TRI, 0, vid});
    }
}

// Thêm struct này ở đầu file compiler.cpp (hoặc trong file header)
struct LoopIndices {
    int cond_start_index; // Địa chỉ cho OP_JUMP (nhảy ngược)
    int exit_jump_index;  // Địa chỉ của OP_JUMP_IF_FALSE (cần backpatching)
};

// --- 4. Sinh phần (init; cond) của vòng lặp ---
// Hàm này phải trả về cả hai chỉ mục quan trọng
LoopIndices compileLoop(const std::vector<std::string> &parts,
                        std::vector<Instruction> &bytecode,
                        std::unordered_map<std::string, int> &symTab,
                        int &nextId)
{
    if (parts.size() != 3) throw std::runtime_error("Cú pháp lặp sai");

    bytecode.push_back({OP_LAP, 0, 0});
    bytecode.push_back({OP_MO_NGOAC, 0, 0});

    // 1. init (Khởi tạo: chỉ chạy một lần)
    bytecode.push_back({OP_KHOI_TAO, 0, 0});
    compileExpr(parts[0], bytecode, symTab, nextId);

    // 2. cond (Điều kiện: Nhãn nhảy ngược)
    bytecode.push_back({OP_DIEU_KIEN, 0, 0});
    int cond_start_index = bytecode.size() - 1; // Địa chỉ của OP_DIEU_KIEN
    compileExpr(parts[1], bytecode, symTab, nextId); // Sinh bytecode điều kiện

    // 3. JUMP_IF_FALSE (Nhảy ra khỏi vòng lặp khi điều kiện sai)
    bytecode.push_back({OP_JUMP_IF_FALSE, 0, 0});
    int exit_jump_index = bytecode.size() - 1; // Địa chỉ cần backpatching

    // Bỏ OP_CAP_NHAT ra khỏi đây! Nó thuộc về compileSource.

    bytecode.push_back({OP_DONG_NGOAC, 0, 0});
    // Bỏ OP_MO_KHOI ra khỏi đây! Nó thuộc về compileSource.

    return {cond_start_index, exit_jump_index};
}



// --- 4. Sinh phần (init; cond; update) ---
int compileCondition(const std::vector<std::string> &parts,
                     std::vector<Instruction> &bytecode,
                     std::unordered_map<std::string, int> &symTab,
                     int &nextId)
{
    // 1. Kiểm tra cú pháp: IF/WHILE chỉ cần 1 biểu thức điều kiện
    if (parts.size() != 1) throw std::runtime_error("Cú pháp điều kiện sai");

    bytecode.push_back({OP_NEU, 0, 0});
    bytecode.push_back({OP_MO_NGOAC, 0, 0});

    // 2. Biên dịch biểu thức điều kiện (phần tử duy nhất parts[0])
    // Giá trị kết quả (true/false) sẽ nằm trên đỉnh stack
    compileExpr(parts[0], bytecode, symTab, nextId);

    // 4. Thêm lệnh nhảy quan trọng: OP_NHAY_NEU_SAI
    // Nếu điều kiện SAI (false), VM sẽ nhảy qua khối lệnh IF.
    // Đích nhảy (0) sẽ được cập nhật sau (backpatching).
    bytecode.push_back({OP_JUMP_IF_FALSE, 0, 0});
    int jump_instruction_index = bytecode.size() - 1;

    bytecode.push_back({OP_DONG_NGOAC, 0, 0});

    return jump_instruction_index;

}


// --- 5. Biên dịch block { ... } ---
void compileBlock(const std::string& src,
                  std::vector<Instruction>& bytecode,
                  std::unordered_map<std::string,int>& symTab,
                  int& nextId,
                  const std::unordered_map<std::string,Opcode>& kwMap)
{
    std::istringstream iss(src);
    std::string line;
    while (std::getline(iss, line)) {
        line = trim(line);
        if (line.empty()) continue;
        auto tokens = tokenize(line);
        for (size_t i = 0; i < tokens.size(); ++i) {
            const auto& tk = tokens[i];
            if (tk == ";") {
                bytecode.push_back({OP_DONG_LENH, 0, 0});
            }
            else if (kwMap.count(tk) && kwMap.at(tk) == OP_IN) {
                // in x;
                ++i;
                int vid = getOrCreate(symTab, tokens[i], nextId);
                bytecode.push_back({OP_TEN_BIEN_GIA_TRI,0,vid});
                bytecode.push_back({OP_IN,0,0});
                continue;
            }
            else if (keywordMap.count(tk) && keywordMap.at(tk) == OP_NEU) {
                // Logic này phải gần như sao chép từ khối OP_NEU trong compileSource

                // 1. Tìm nội dung trong ()
                size_t j = i + 2; int d = 1;
                std::ostringstream oss;
                while (j < tokens.size() && d > 0) {
                    if      (tokens[j] == "(") ++d;
                    else if (tokens[j] == ")") --d;
                    if (d > 0) oss << tokens[j] << " ";
                    ++j;
                }

                auto parts = splitLoopParts(oss.str());
                int jump_index = compileCondition(parts, bytecode, symTab, nextId);

                // 2. Phần thân { … } - Cần phải dùng đệ quy hoặc logic phức tạp
                if (j < tokens.size() && tokens[j] == "{") { // Sửa lỗi chỉ mục đã thảo luận
                    size_t k = j + 1; int bc = 1; // Bắt đầu sau token '{' (tại j)
                    std::ostringstream bs;
                    while (k < tokens.size() && bc > 0) {
                        if      (tokens[k] == "{") ++bc;
                        else if (tokens[k] == "}") --bc;
                        if (bc > 0) bs << tokens[k] << " ";
                        ++k;
                    }

                    // **QUAN TRỌNG:** Phải gọi compileBlock đệ quy hoặc một hàm xử lý block khác cho thân lệnh IF
                    bytecode.push_back({OP_MO_KHOI, 0, 0});
                    compileBlock(bs.str(), bytecode, symTab, nextId, kwMap);
                    bytecode.push_back({OP_DONG_KHOI, 0, 0});

                    int jump_target_index = bytecode.size();
                    bytecode[jump_index].operand = jump_target_index;
                    i = k - 1; // Cập nhật i để bỏ qua khối IF đã xử lý
                    continue;
                }
                i = j;
                continue;
            }
            else {
                compileToken(tk, bytecode, symTab, nextId, kwMap);
            }
        }
        // bytecode.push_back({OP_DONG_LENH,0,0});
    }
}

// --- 6. Hàm chính ---
std::vector<Instruction> compileSource(const std::string& source,
                                       const std::unordered_map<std::string,Opcode>& keywordMap)
{
    std::vector<Instruction> bytecode;
    std::unordered_map<std::string,int> symbolTable;
    int nextSymbolIndex = 0;

    std::istringstream iss(source);
    std::string line;
    while (std::getline(iss, line)) {
        line = trim(line);
        if (line.empty()) continue;

        auto tokens = tokenize(line);

        for (size_t i = 0; i < tokens.size(); ++i) {
            const auto& tk = tokens[i];
            // dấu kết thúc dòng
            if (tk == "}") {
                continue; // Bỏ qua '}' đã được xử lý bởi OP_LAP/OP_NEU
            }
            if (tk == ";") {
                if (i < tokens.size() - 1 || std::getline(iss, line)) {
                    bytecode.push_back({OP_DONG_LENH,0,0});
                }
                continue;
            }
            // lặp (...)
            if (keywordMap.count(tk) && keywordMap.at(tk) == OP_LAP) {
                // tìm nội dung trong ()
                size_t j = i+2; int d=1;
                std::ostringstream oss;
                while (j < tokens.size() && d>0) {
                    if      (tokens[j]=="(") ++d;
                    else if (tokens[j]==")") --d;

                    if (d > 0) {
                        oss << tokens[j];
                        // Fix: Ensures ';' is included without an extra space after it
                        if (tokens[j] != ";") {
                            oss << " ";
                        }
                    }
                    ++j;
                }

                auto parts = splitLoopParts(oss.str());
                LoopIndices indices = compileLoop(parts, bytecode, symbolTable, nextSymbolIndex);

                // phần thân { … }
                // SỬA LỖI CHỈ MỤC: j là index của token NGAY sau ')'
                if (j < tokens.size() && tokens[j] == "{") {
                    size_t k=j+1; // K bắt đầu sau token '{' (tại j)
                    int bc=1;
                    std::ostringstream bs;
                    while (k<tokens.size()&&bc>0) {
                        if      (tokens[k]=="{") ++bc;
                        else if (tokens[k]=="}") --bc;
                        if (bc>0) bs<<tokens[k]<<" ";
                        ++k;
                    }

                    // 2a. Mở khối và biên dịch thân lệnh (Đúng)
                    bytecode.push_back({OP_MO_KHOI,0,0});

                    // **GỌI compileBlock Ở ĐÂY** -> Biên dịch toàn bộ khối { neu (..) { in i; } }
                    compileBlock(bs.str(), bytecode, symbolTable, nextSymbolIndex, keywordMap);

                    // **Đóng khối thân lệnh** (từ `OP_MO_KHOI` trong 2a)
                    // bytecode.push_back({OP_DONG_KHOI,0,0}); // <-- Thêm DONG_KHOI cho khối body

                    // 2b. XỬ LÝ UPDATE (CAP_NHAT) (Đúng)
                    bytecode.push_back({OP_CAP_NHAT, 0, 0});
                    compileExpr(parts[2], bytecode, symbolTable, nextSymbolIndex);

                    // 2c. JUMP NGƯỢC (Đúng)
                    bytecode.push_back({OP_JUMP, indices.cond_start_index, 0});

                    bytecode.push_back({OP_DONG_KHOI,0,0}); // <-- Khối đóng duy nhất cho [12]

                    // 4. BACKPATCHING JUMP_IF_FALSE (Đúng)
                    int jump_target_index = bytecode.size();
                    bytecode[indices.exit_jump_index].operand = jump_target_index;

                    i = k - 1; // i must point to the token BEFORE the closing '}'
                    continue;
                }

                // If there is no block {} (single statement loop), the VM will execute
                // the next instruction, but the update/jump logic must be handled differently.
                // For now, assume { } is mandatory.
                i = j;
                continue;
            }
            if (keywordMap.count(tk) && keywordMap.at(tk) == OP_NEU) {
                // tìm nội dung trong ()
                size_t j = i+2; int d=1;
                std::ostringstream oss;
                while (j < tokens.size() && d>0) {
                    if      (tokens[j]=="(") ++d;
                    else if (tokens[j]==")") --d;
                    if (d>0) oss << tokens[j]<<" ";
                    ++j;
                }
                auto parts = splitLoopParts(oss.str());
                // compileCondition(parts, bytecode, symbolTable, nextSymbolIndex);
                int jump_index = compileCondition(parts, bytecode, symbolTable, nextSymbolIndex); // Chỉ mục của OP_JUMP_IF_FALSE

                // phần thân { … }
                if (j < tokens.size() && tokens[j]=="{") {
                    size_t k=j+1; int bc=1;
                    std::ostringstream bs;
                    while (k<tokens.size()&&bc>0) {
                        if      (tokens[k]=="{") ++bc;
                        else if (tokens[k]=="}") --bc;
                        if (bc>0) bs<<tokens[k]<<" ";
                        ++k;
                    }

                    bytecode.push_back({OP_MO_KHOI,0,0});
                    compileBlock(bs.str(), bytecode, symbolTable, nextSymbolIndex, keywordMap);
                    bytecode.push_back({OP_DONG_KHOI,0,0});
                    int jump_target_index = bytecode.size();

                    // Cập nhật operand của OP_JUMP_IF_FALSE
                    bytecode[jump_index].operand = jump_target_index;
                    i = k - 1; // Di chuyển i đến sau dấu "}" cuối cùng
                    continue;
                }
                i = j;
                continue;
            }

            // in x;
            if (keywordMap.count(tk) && keywordMap.at(tk) == OP_IN) {
                ++i;
                int vid = getOrCreate(symbolTable, tokens[i], nextSymbolIndex);
                bytecode.push_back({OP_TEN_BIEN_GIA_TRI,0,vid});
                bytecode.push_back({OP_IN,0,0});
                continue;
            }
            // khác đều là token đơn
            compileToken(tk, bytecode, symbolTable, nextSymbolIndex, keywordMap);
        }
        // kết thúc dòng
        // bytecode.push_back({OP_DONG_LENH,0,0});
    }

    bytecode.push_back({OP_DUNG_CHUONG_TRINH,0,0});
    return bytecode;
}



