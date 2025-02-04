// src/main.cpp
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "src/include/compiler.h"
#include "src/include/vm.h"

std::string readFile(const std::string &filename) {
    std::ifstream fileStream(filename);
    if (!fileStream.is_open()) {
        throw std::runtime_error("Không thể mở file: " + filename);
    }
    std::stringstream buffer;
    buffer << fileStream.rdbuf();
    return buffer.str();
}

int main() {
    try {
        // Đọc nội dung từ file có đuôi .vi (ví dụ: program.vi)
        const std::string filename = "/Users/winbiru/VietVM/tests/program.vi";
        std::string source = readFile(filename);

        // Biên dịch mã nguồn thành bytecode
        std::vector<Instruction> bytecode = compileSource(source);

        // Tạo máy ảo và chạy bytecode
        VM vm(bytecode);
        std::cout << "Kết quả thực thi:" << std::endl;
        vm.run();
    } catch (const std::exception &ex) {
        std::cerr << "Lỗi: " << ex.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
