// compiler.cpp
#include "../include/compiler.h"              // Include file header cho compiler
#include "../include/common/Utility.h"   // để dùng Utility::trim(...)
#include <algorithm>                         // Thư viện cho các hàm như all_of, find_first_not_of
#include <sstream>                          // Để sử dụng istringstream tách dòng
#include <stdexcept>                        // Để ném lỗi runtime_error
#include <vector>                          // Dùng vector chứa token, bytecode
#include <cctype>                          // Dùng các hàm kiểm tra ký tự như isspace, isdigit
#include <iostream>                        // Dùng để debug xuất ra cerr
#include <unordered_map>                   // Dùng bảng hash ánh xạ tên biến, opcode
#include "../include/common/Utility.h"
#include "../include/instruction.h"        // Include định nghĩa Instruction, Opcode
#include "common/LoopUltil.h"

void compileExpr(const std::string& expr,
                 std::vector<Instruction>& bytecode,
                 std::unordered_map<std::string, int>& symbolTable,
                 int& nextSymbolIndex);


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

// --- 2. Biên dịch biểu thức ---
void compileExpr(const std::string& expr,
                 std::vector<Instruction>& bytecode,
                 std::unordered_map<std::string,int>& symTab,
                 int& nextId)
{
    auto toks = tokenize(trim(expr));

    // a = b
    if (toks.size() == 3 && toks[1] == "=") {
        int dst = getOrCreate(symTab, toks[0], nextId);
        // giá trị
        if (isNumber(toks[2]))
            bytecode.push_back({OP_BIEN_SO, std::stoi(toks[2]), 0});
        else {
            int src = getOrCreate(symTab, toks[2], nextId);
            bytecode.push_back({OP_TEN_BIEN_GIA_TRI, 0, src});
        }
        // ID đích
        bytecode.push_back({OP_TEN_BIEN_ID, dst, 0});
        bytecode.push_back({OP_GAN, 0, 0});
        return;
    }

    // a = b + c  (hoặc -,*,/)
    if (toks.size() == 5 && toks[1] == "=") {
        int dst = getOrCreate(symTab, toks[0], nextId);
        // giá trị b
        if (isNumber(toks[2]))
            bytecode.push_back({OP_BIEN_SO, std::stoi(toks[2]), 0});
        else {
            int b = getOrCreate(symTab, toks[2], nextId);
            bytecode.push_back({OP_TEN_BIEN_GIA_TRI, 0, b});
        }
        // giá trị c
        if (isNumber(toks[4]))
            bytecode.push_back({OP_BIEN_SO, std::stoi(toks[4]), 0});
        else {
            int c = getOrCreate(symTab, toks[4], nextId);
            bytecode.push_back({OP_TEN_BIEN_GIA_TRI, 0, c});
        }
        // phép toán
        if      (toks[3] == "+") bytecode.push_back({OP_CONG, 0, 0});
        else if (toks[3] == "-") bytecode.push_back({OP_TRU, 0, 0});
        else if (toks[3] == "*") bytecode.push_back({OP_NHAN, 0, 0});
        else if (toks[3] == "/") bytecode.push_back({OP_CHIA, 0, 0});
        else throw std::runtime_error("Phép toán chưa hỗ trợ: " + toks[3]);
        // gán
        bytecode.push_back({OP_TEN_BIEN_ID, dst, 0});
        bytecode.push_back({OP_GAN, 0, 0});
        return;
    }

    // so sánh: x < 100, x>=y, v.v.
    if (toks.size() == 3 && (
        toks[1] == "==" || toks[1] == "!=" ||
        toks[1] == "<"  || toks[1] == ">"  ||
        toks[1] == "<=" || toks[1] == ">="))
    {
        // giá trị trái
        if (isNumber(toks[0]))
            bytecode.push_back({OP_BIEN_SO, std::stoi(toks[0]), 0});
        else {
            int l = getOrCreate(symTab, toks[0], nextId);
            bytecode.push_back({OP_TEN_BIEN_GIA_TRI, 0, l});
        }
        // giá trị phải
        if (isNumber(toks[2]))
            bytecode.push_back({OP_BIEN_SO, std::stoi(toks[2]), 0});
        else {
            int r = getOrCreate(symTab, toks[2], nextId);
            bytecode.push_back({OP_TEN_BIEN_GIA_TRI, 0, r});
        }
        // operator
        if      (toks[1] == "==") bytecode.push_back({OP_SO_SANH_BANG,        0, 0});
        else if (toks[1] == "!=") bytecode.push_back({OP_KHAC_BANG,           0, 0});
        else if (toks[1] == "<")  bytecode.push_back({OP_NHO_HON,            0, 0});
        else if (toks[1] == ">")  bytecode.push_back({OP_LON_HON,            0, 0});
        else if (toks[1] == "<=") bytecode.push_back({OP_NHO_HON_HOAC_BANG, 0, 0});
        else if (toks[1] == ">=") bytecode.push_back({OP_LON_HON_HOAC_BANG, 0, 0});
        return;
    }

    throw std::runtime_error("Biểu thức chưa được hỗ trợ: " + expr);
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


// --- 4. Sinh phần (init; cond; update) ---
void compileLoop(const std::vector<std::string>& parts,
                 std::vector<Instruction>& bytecode,
                 std::unordered_map<std::string,int>& symTab,
                 int& nextId)
{
    if (parts.size() != 3) throw std::runtime_error("Cú pháp lặp sai");

    bytecode.push_back({OP_LAP, 0, 0});
    bytecode.push_back({OP_MO_NGOAC, 0, 0});

    // init
    bytecode.push_back({OP_KHOI_TAO, 0, 0});
    compileExpr(parts[0], bytecode, symTab, nextId);

    // cond
    bytecode.push_back({OP_DIEU_KIEN, 0, 0});
    compileExpr(parts[1], bytecode, symTab, nextId);

    // update
    bytecode.push_back({OP_CAP_NHAT, 0, 0});
    compileExpr(parts[2], bytecode, symTab, nextId);

    bytecode.push_back({OP_DONG_NGOAC, 0, 0});
    bytecode.push_back({OP_MO_KHOI, 0, 0});

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
        auto toks = tokenize(line);
        for (size_t i = 0; i < toks.size(); ++i) {
            const auto& tk = toks[i];
            if (tk == ";") {
                bytecode.push_back({OP_DONG_LENH, 0, 0});
            }
            else if (kwMap.count(tk) && kwMap.at(tk) == OP_IN) {
                // in x;
                ++i;
                int vid = getOrCreate(symTab, toks[i], nextId);
                bytecode.push_back({OP_TEN_BIEN_GIA_TRI,0,vid});
                bytecode.push_back({OP_IN,0,0});
            }
            else {
                compileToken(tk, bytecode, symTab, nextId, kwMap);
            }
        }
        bytecode.push_back({OP_DONG_LENH,0,0});
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
            if (tk == ";") {
                bytecode.push_back({OP_DONG_LENH,0,0});
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
                    if (d>0) oss << tokens[j]<<" ";
                    ++j;
                }
                auto parts = splitLoopParts(oss.str());
                compileLoop(parts, bytecode, symbolTable, nextSymbolIndex);

                // phần thân { … }
                if (j+1<tokens.size() && tokens[j+1]=="{") {
                    size_t k=j+2; int bc=1;
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
                    i=k;
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
        bytecode.push_back({OP_DONG_LENH,0,0});
    }

    bytecode.push_back({OP_DUNG_CHUONG_TRINH,0,0});
    return bytecode;
}



