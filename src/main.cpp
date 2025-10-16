#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "../include/compiler.h"
#include "../include/vm.h"
#include "../include/name_op.h"

// Biến toàn cục chứa chuỗi hằng đã biên dịch
extern std::vector<std::string> stringPool;

std::string readFile(const std::string &filename) {
    std::ifstream fileStream(filename);
    if (!fileStream.is_open()) {
        throw std::runtime_error("Không thể mở file: " + filename);
    }
    std::stringstream buffer;
    buffer << fileStream.rdbuf();
    return buffer.str();
}

void initCompileMap();

int main() {
    try {
        // Đọc nội dung từ file .vi
        // const std::string filename = "../tests/program.vi";
        // const std::string filename = "../tests/kiem_tra_so_chan_1-20.vi";
        // const std::string filename = "../tests/kiem_tra_so_le_chia_het_cho_5.vi";
        // const std::string filename = "../tests/kiem_tra_so_nguyen.vi";
        // const std::string filename = "../tests/kiem_tra_so_chia_het_cho_3_va_4.vi";
        // const std::string filename = "../tests/kiem_tra_noi_chuoi.vi";
        const std::string filename = "../tests/kiem_tra_dieu_kien_long_nhieu_cap.vi";
        // const std::string filename = "../tests/kiem_tra_dieu_kien_phu_dinh.vi";




        std::string source = readFile(filename);

        // Khởi tạo bảng từ khóa
        initCompileMap();

        // Biên dịch mã nguồn thành bytecode
        std::vector<Instruction> bytecode = compileSource(source, keywordMap);

        // In toàn bộ bytecode để debug
        std::cout << "Danh sách bytecode:" << std::endl;
        for (size_t i = 0; i < bytecode.size(); ++i) {
            const Instruction &instr = bytecode[i];
            std::cout << "[" << i << "] "
                      << "op: " << instr.op
                      << " (" << name_op(instr.op) << ")";
            if (instr.operandIndex != -1)
                std::cout << ", operandIndex: " << instr.operandIndex;
            if (instr.operand != 0)
                std::cout << ", operand: " << instr.operand;
            std::cout << std::endl;
        }

        // In ra stringPool để kiểm tra
        std::cout << "\nDanh sách chuỗi đã lưu:\n";
        for (size_t i = 0; i < stringPool.size(); ++i) {
            std::cout << "[" << i << "] = \"" << stringPool[i] << "\"\n";
        }

        // Tạo máy ảo và truyền stringPool vào
        VM vm(bytecode);
        vm.loadStringPool(stringPool);  // ✅ Truyền chuỗi đã biên dịch vào VM

        // Thực thi chương trình
        std::cout << "\nKết quả thực thi:\n";
        vm.run();
    } catch (const std::exception &ex) {
        std::cerr << "Lỗi: " << ex.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
