#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "../include/compiler.h"
#include "../include/vm.h"
#include "../include/name_op.h"

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
        const std::string filename = "../tests/program.vi";
        std::string source = readFile(filename);

        // Biên dịch mã nguồn thành bytecode
        std::vector<Instruction> bytecode = compileSource(source, keywordMap);

        // In toàn bộ bytecode để debug
        std::cout << "Danh sách bytecode:" << std::endl;
        for (size_t i = 0; i < bytecode.size(); ++i) {
            const Instruction &instr = bytecode[i];
            std::cout << "[" << i << "] "
            << "op: " << instr.op   // ID opcode
            << " (" << name_op(instr.op) << ")";      // Tên opcode

            if (bytecode[i].operandIndex != -1)
                std::cout << ", operandIndex: " << bytecode[i].operandIndex;
            if (bytecode[i].operand != 0)
                std::cout << ", operand: " << bytecode[i].operand;
            std::cout << std::endl;
        }

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
