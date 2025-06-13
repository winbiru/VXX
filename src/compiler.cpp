// compiler.cpp
#include "../include/compiler.h"              // Include file header cho compiler

#include <algorithm>                         // Thư viện cho các hàm như all_of, find_first_not_of
#include <sstream>                          // Để sử dụng istringstream tách dòng
#include <stdexcept>                        // Để ném lỗi runtime_error
#include <vector>                          // Dùng vector chứa token, bytecode
#include <cctype>                          // Dùng các hàm kiểm tra ký tự như isspace, isdigit
#include <iostream>                        // Dùng để debug xuất ra cerr
#include <unordered_map>                   // Dùng bảng hash ánh xạ tên biến, opcode

#include "../include/instruction.h"        // Include định nghĩa Instruction, Opcode

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

// Hàm lấy hoặc tạo id cho một khóa trong bảng symbolTable
int getOrCreate(std::unordered_map<std::string, int>& table, const std::string& key, int& nextIndex) {
    if (table.count(key)) {                // Nếu đã tồn tại trong bảng
        return table[key];                 // Trả về id đã có
    } else {
        int index = nextIndex++;           // Tạo id mới = giá trị hiện tại của nextIndex
        table[key] = index;                // Thêm vào bảng
        return index;                     // Trả về id mới
    }
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

// Hàm chính biên dịch source code thành bytecode
std::vector<Instruction> compileSource(const std::string &source) {
    std::vector<Instruction> bytecode;        // Vector lưu trữ lệnh bytecode sau khi biên dịch

    // Bảng ánh xạ từ khóa, toán tử thành mã opcode
    static std::unordered_map<std::string, Opcode> keywordMap = {
        {"nếu", OP_NEU},
        {"khác", OP_KHAC},
        {"nếu không", OP_NEU_KHONG},
        {"lặp", OP_LAP},
        {"khởi tạo", OP_KHOI_TAO},
        {"điều kiện", OP_DIEU_KIEN},
        {"cập nhật", OP_CAP_NHAT},
        {"kiểm tra sau", OP_KIEM_TRA_SAU},
        {"chuyển", OP_CHUYEN},
        {"trường hợp", OP_TRUONG_HOP},
        {"mặc định", OP_MAC_DINH},
        {"hàm", OP_HAM},
        {"gọi", OP_GOI},
        {"trả về", OP_TRA_VE},
        {"biến", OP_BIEN_SO},
        {"in", OP_IN},
        {"dừng", OP_DUNG_CHUONG_TRINH},
        {"bỏ qua", OP_BO_QUA},
        {"thoát", OP_THOAT},
        {"==", OP_SO_SANH_BANG},
        {"!=", OP_KHAC_BANG},
        {">", OP_LON_HON},
        {"<", OP_NHO_HON},
        {">=", OP_LON_HON_HOAC_BANG},
        {"<=", OP_NHO_HON_HOAC_BANG},
        {"=", OP_GAN},
        {"+", OP_CONG},
        {"-", OP_TRU},
        {"*", OP_NHAN},
        {"/", OP_CHIA},
        {"&&", OP_VA},
        {"||", OP_HOAC},
        {"{", OP_MO_KHOI},
        {"}", OP_DONG_KHOI},
        {"(", OP_MO_NGOAC},
        {")", OP_DONG_NGOAC},
        {"[", OP_MO_MANG},
        {"]", OP_DONG_MANG},
        {";", OP_DONG_LENH},
        {",", OP_PHAY}
    };

    std::istringstream iss(source);            // Tạo luồng đọc dòng source
    std::string line;                          // Biến chứa từng dòng

    while (std::getline(iss, line)) {         // Đọc từng dòng trong source
        line = trim(line);                     // Loại bỏ khoảng trắng đầu cuối
        if (line.empty()) continue;            // Bỏ qua dòng trống

        auto tokens = tokenize(line);         // Tách dòng thành token

        for (size_t i = 0; i < tokens.size(); ++i) {
            const std::string &token = tokens[i];  // Lấy token hiện tại

            if (token == ";") {                 // Nếu là dấu chấm phẩy kết thúc câu lệnh
                bytecode.push_back({OP_DONG_LENH, 0}); // Thêm opcode kết thúc dòng
                continue;
            }

            if (keywordMap.count(token)) {    // Nếu token là từ khóa hoặc toán tử đã định nghĩa
                const Opcode op = keywordMap[token]; // Lấy opcode tương ứng

                if (op == OP_LAP) {           // Nếu là từ khóa "lặp" (vòng lặp)
                    bytecode.push_back({OP_LAP, 0});  // <<< DÒNG QUAN TRỌNG
                    bytecode.push_back({OP_MO_NGOAC, 0});  // <<< DÒNG QUAN TRỌNG
                    if (i + 1 < tokens.size() && tokens[i + 1] == "(") { // Kiểm tra dấu ngoặc tròn
                        size_t j = i + 2;     // Vị trí bắt đầu bên trong ngoặc
                        int parenCount = 1;  // Đếm ngoặc mở để xác định ngoặc đóng
                        std::string insideParens; // Chuỗi chứa nội dung trong ngoặc

                        while (j < tokens.size() && parenCount > 0) {  // Đọc đến hết ngoặc
                            if (tokens[j] == ")") {    // Nếu gặp ngoặc đóng
                                --parenCount;          // Giảm đếm ngoặc mở
                                if (parenCount == 0) break;  // Nếu hết ngoặc mở -> thoát vòng
                            } else if (tokens[j] == "(") { // Nếu gặp ngoặc mở lồng
                                ++parenCount;          // Tăng đếm ngoặc mở
                            }
                            if (parenCount > 0) {      // Chỉ thêm token nếu còn trong ngoặc
                                insideParens += tokens[j];
                                if (tokens[j] != ";") insideParens += " "; // Thêm dấu cách giữa token
                            }
                            ++j;                      // Tăng vị trí đọc token
                        }

                        // Hàm con tách phần bên trong ngoặc thành các phần theo dấu chấm phẩy
                        auto splitLoopParts = [](const std::string& s) -> std::vector<std::string> {
                            std::vector<std::string> parts;
                            std::string current;
                            int parenDepth = 0;

                            for (const char c : s) {
                                if (c == '(') parenDepth++;
                                else if (c == ')') parenDepth--;

                                if (c == ';' && parenDepth == 0) {
                                    parts.push_back(trim(current));
                                    current.clear();
                                } else {
                                    current += c;
                                }
                            }

                            if (!current.empty()) parts.push_back(trim(current));
                            return parts;
                        };

                        // Tách 3 phần khởi tạo; điều kiện; cập nhật của vòng lặp
                        auto parts = splitLoopParts(insideParens);

                        // Debug in ra số phần tách được (phải là 3)
                        std::cerr << "Parts trong vòng lặp: " << parts.size() << std::endl;
                        for (auto &p : parts) {
                            std::cerr << "[" << p << "]" << std::endl;
                        }

                        if (parts.size() != 3) {       // Nếu không đủ 3 phần -> lỗi cú pháp
                            throw std::runtime_error("Cú pháp lặp không hợp lệ: cần 3 phần trong dấu ()");
                        }

                        // Hàm con biên dịch từng biểu thức thành bytecode
                        auto compileExpr = [&](const std::string& expr) {
                            auto toks = tokenize(trim(expr));

                            // Trường hợp biểu thức gán: biến = giá trị (biến hoặc số)
                            if (toks.size() == 3 && toks[1] == "=") {
                                int varId = getOrCreate(symbolTable, toks[0], nextSymbolIndex);

                                // Nếu giá trị là số nguyên
                                if (std::all_of(toks[2].begin(), toks[2].end(), ::isdigit)) {
                                    bytecode.push_back({OP_BIEN_SO, std::stoi(toks[2])});
                                } else {
                                    // Nếu giá trị là biến
                                    int valId = getOrCreate(symbolTable, toks[2], nextSymbolIndex);
                                    bytecode.push_back({OP_TEN_BIEN, valId});
                                }
                                bytecode.push_back({OP_TEN_BIEN, varId});
                                bytecode.push_back({OP_GAN, 0});
                            }
                            // Trường hợp biểu thức gán dạng: biến = biến OP số/biến (ví dụ i = i + 1)
                            else if (toks.size() == 5 && toks[1] == "=") {
                                int varId = getOrCreate(symbolTable, toks[0], nextSymbolIndex);

                                // Đưa biến trái phải lên stack
                                int leftId = getOrCreate(symbolTable, toks[2], nextSymbolIndex);
                                bytecode.push_back({OP_TEN_BIEN, leftId});

                                // Đưa giá trị phải (có thể là biến hoặc số)
                                if (std::all_of(toks[4].begin(), toks[4].end(), ::isdigit)) {
                                    bytecode.push_back({OP_BIEN_SO, std::stoi(toks[4])});
                                } else {
                                    int rightId = getOrCreate(symbolTable, toks[4], nextSymbolIndex);
                                    bytecode.push_back({OP_TEN_BIEN, rightId});
                                }

                                // Thêm opcode tương ứng phép toán ở toks[3]
                                if (toks[3] == "+") bytecode.push_back({OP_CONG, 0});
                                else if (toks[3] == "-") bytecode.push_back({OP_TRU, 0});
                                else if (toks[3] == "*") bytecode.push_back({OP_NHAN, 0});
                                else if (toks[3] == "/") bytecode.push_back({OP_CHIA, 0});
                                else throw std::runtime_error("Phép toán chưa được hỗ trợ: " + toks[3]);

                                // Gán kết quả cho biến đầu tiên
                                bytecode.push_back({OP_TEN_BIEN, varId});
                                bytecode.push_back({OP_GAN, 0});
                            }
                            // Trường hợp biểu thức so sánh đơn giản: biến OP số hoặc biến
                            else if (toks.size() == 3 && (
                                toks[1] == "==" || toks[1] == "!=" ||
                                toks[1] == "<" || toks[1] == ">" ||
                                toks[1] == "<=" || toks[1] == ">=")) {

                                // Lấy id biến trái
                                int leftId = getOrCreate(symbolTable, toks[0], nextSymbolIndex);

                                // Đưa biến trái lên stack
                                bytecode.push_back({OP_TEN_BIEN, leftId});

                                // Đưa giá trị phải lên stack (biến hoặc số)
                                if (std::all_of(toks[2].begin(), toks[2].end(), ::isdigit)) {
                                    bytecode.push_back({OP_BIEN_SO, std::stoi(toks[2])});
                                } else {
                                    const int rightId = getOrCreate(symbolTable, toks[2], nextSymbolIndex);
                                    bytecode.push_back({OP_TEN_BIEN, rightId});
                                }

                                // Tùy toán tử so sánh thêm opcode tương ứng
                                if (toks[1] == "==") bytecode.push_back({OP_SO_SANH_BANG, 0});
                                else if (toks[1] == "!=") bytecode.push_back({OP_KHAC_BANG, 0});
                                else if (toks[1] == "<") bytecode.push_back({OP_NHO_HON, 0});
                                else if (toks[1] == ">") bytecode.push_back({OP_LON_HON, 0});
                                else if (toks[1] == "<=") bytecode.push_back({OP_NHO_HON_HOAC_BANG, 0});
                                else if (toks[1] == ">=") bytecode.push_back({OP_LON_HON_HOAC_BANG, 0});
                            }
                            else {
                                throw std::runtime_error("Biểu thức trong vòng lặp chưa được hỗ trợ: " + expr);
                            }
                        };

                        // Biên dịch phần khởi tạo vòng lặp
                        bytecode.push_back({OP_KHOI_TAO, 0});      // Opcode khởi tạo
                        compileExpr(parts[0]);

                        // Biên dịch phần điều kiện vòng lặp
                        compileExpr(parts[1]);
                        bytecode.push_back({OP_DIEU_KIEN, 0});     // Opcode điều kiện

                        // Biên dịch phần cập nhật vòng lặp
                        compileExpr(parts[2]);
                        bytecode.push_back({OP_CAP_NHAT, 0});      // Opcode cập nhật
                        bytecode.push_back({OP_DONG_NGOAC, 0});     // Opcode đóng ngoặc
                        bytecode.push_back({OP_MO_KHOI, 0});       // Mở khối lệnh vòng lặp

                        // Nhảy tới token cuối ngoặc để tiếp tục vòng for
                        i = j;
                        continue;     // Bỏ qua phần xử lý dưới cùng vòng for
                    }
                }

                // Các từ khóa khác: thêm opcode tương ứng vào bytecode
                bytecode.push_back({op, 0});
            } else {
                // Token không phải từ khóa, có thể là tên biến hoặc số nguyên

                if (std::all_of(token.begin(), token.end(), ::isdigit)) {
                    // Nếu token toàn chữ số -> opcode BIEN_SO với giá trị số nguyên
                    bytecode.push_back({OP_BIEN_SO, std::stoi(token)});
                } else {
                    // Nếu là tên biến, lấy id biến trong bảng symbolTable
                    int id = getOrCreate(symbolTable, token, nextSymbolIndex);
                    bytecode.push_back({OP_TEN_BIEN, id});
                }
            }
        }
    }

    return bytecode;  // Trả về bytecode sau khi biên dịch toàn bộ source
}
