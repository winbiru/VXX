// compiler.cpp
#include "../include/compiler.h"
#include "../include/instruction.h"
#include <sstream>
#include <stdexcept>
#include <vector>
// Hàm helper: loại bỏ khoảng trắng ở đầu và cuối chuỗi
static std::string trim(const std::string &s) {
    size_t start = s.find_first_not_of(" \t");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t");
    return s.substr(start, end - start + 1);
}

std::vector<Instruction> compileSource(const std::string &source) {
    std::vector<Instruction> bytecode;
    std::istringstream iss(source);
    std::string line;
    while (std::getline(iss, line)) {
        // Loại bỏ khoảng trắng đầu cuối của dòng
        std::string trimmedLine = trim(line);

        if (trimmedLine.empty()) continue; // bỏ qua dòng trống

        std::istringstream lineStream(trimmedLine);
        std::string token;
        lineStream >> token;

        if (token == "BIẾN") {
            int value;
            lineStream >> value;
            bytecode.push_back({OP_BIEN_SO, value});
        } else if (token == "NẾU") {
            bytecode.push_back({OP_NEU, 0});
        } else if (token == "NGƯỢC LẠI") {
            bytecode.push_back({OP_NGUOC_LAI, 0});
        } else if (token == "LẶP") {
            bytecode.push_back({OP_LAP, 0});
        } else if (token == "KẾT THÚC") {
            bytecode.push_back({OP_KET_THUC_LAP, 0});
        } else if (token == "HÀM") {
            bytecode.push_back({OP_HAM, 0});
        } else if (token == "GỌI") {
            bytecode.push_back({OP_GOI_HAM, 0});
        } else if (token == "TRẢ VỀ") {
            bytecode.push_back({OP_TRA_VE, 0});
        } else if (token == "BỎ QUA") {
            bytecode.push_back({OP_BO_QUA, 0});
        } else if (token == "THOÁT") {
            bytecode.push_back({OP_THOAT, 0});
        } else if (token == "+") {
            bytecode.push_back({OP_CONG, 0});
        } else if (token == "-") {
            bytecode.push_back({OP_TRU, 0});
        } else if (token == "*") {
            bytecode.push_back({OP_NHAN, 0});
        } else if (token == "/") {
            bytecode.push_back({OP_CHIA, 0});
        } else if (token == "VÀ") {
            bytecode.push_back({OP_VA, 0});
        } else if (token == "HOẶC") {
            bytecode.push_back({OP_HOAC, 0});
        } else if (token == "KHÔNG") {
            bytecode.push_back({OP_KHONG, 0});
        } else if (token == "IN") {
            bytecode.push_back({OP_IN, 0});
        } else if (token == "DỪNG") {
            bytecode.push_back({OP_DUNG_CHUONG_TRINH, 0});
        } else {
            throw std::runtime_error("Lỗi cú pháp: từ khóa không xác định '" + token + "'");
        }
    }
    return bytecode;
}
