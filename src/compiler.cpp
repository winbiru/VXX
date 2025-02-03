// compiler.cpp
#include "include/compiler.h"
#include "include/instruction.h"
#include <sstream>
#include <stdexcept>
#include <vector>

std::vector<Instruction> compileSource(const std::string &source) {
    std::vector<Instruction> bytecode;
    std::istringstream iss(source);
    std::string line;
    while (std::getline(iss, line)) {
        // Loại bỏ khoảng trắng thừa
        if (line.empty()) continue;
        // Tách dòng theo từ
        std::istringstream lineStream(line);
        std::string token;
        lineStream >> token;

        if (token == "TẢI_SỐ") {
            int value;
            lineStream >> value;
            bytecode.push_back({OP_TAI_SO, value});
        } else if (token == "CỘNG") {
            bytecode.push_back({OP_CONG, 0});
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
