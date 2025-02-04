// compiler.cpp
#include "include/compiler.h"
#include "include/instruction.h"
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
            bytecode.push_back({OP_BIEN, value});
        } else if (token == "NẾU") {
            bytecode.push_back({OP_NEU, 0});
        } else if (token == "CỘNG") {
            bytecode.push_back({OP_CONG, 0});
        } else if (token == "TRỪ") {
            bytecode.push_back({OP_TRU, 0});
        } else if (token == "NHÂN") {
            bytecode.push_back({OP_NHAN, 0});
        } else if (token == "CHIA") {
            bytecode.push_back({OP_CHIA, 0});
        } else if (token == "IN") {
            bytecode.push_back({OP_IN, 0});
        } else if (token == "DỪNG") {
            bytecode.push_back({OP_DUNG, 0});
        } else {
            throw std::runtime_error("Lỗi cú pháp: từ khóa không xác định '" + token + "'");
        }
    }
    return bytecode;
}
