#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <filesystem> // Thêm dòng này
#include "../include/compiler.h"
#include "../include/vm.h"
#include "../include/name_op.h"

// Biến toàn cục chứa chuỗi hằng đã biên dịch
extern std::vector<std::string> stringPool;
namespace fs = std::filesystem;

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
int main(int argc, char* argv[]) {
    try {
        initCompileMap();

        if (argc == 2) {
            // Chạy đúng file được truyền qua dòng lệnh
            const std::string filename = argv[1];
            std::string source = readFile(filename);
            stringPool.clear();

            std::vector<Instruction> bytecode = compileSource(source, keywordMap);
            VM vm(bytecode, stringPool);
            // vm.loadStringPool(stringPool);
            vm.run();
            return EXIT_SUCCESS;
        }

        const std::string defaultFile = "../tests/kiem_tra_dieu_kien_phu_dinh.vi";
        if (argc == 1 && fs::exists(defaultFile)) {
            // Chạy file mặc định nếu không truyền đối số
            std::string source = readFile(defaultFile);
            stringPool.clear();

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
                if (instr.operandIndex != -1)
                    std::cout << ", operandIndex: " << instr.operandIndex;
                if (instr.operand != 0)
                    std::cout << ", operand: " << instr.operand;
                std::cout << std::endl;
            }
            VM vm(bytecode, stringPool);
            vm.run();
            return EXIT_SUCCESS;
        }

        // ✅ Nếu không có đối số và không có file mặc định → chạy toàn bộ thư mục
        std::string testDir = "tests/";
        for (const auto& entry : fs::directory_iterator(testDir)) {
            if (entry.path().extension() == ".vi") {
                const std::string filename = entry.path().string();
                std::string source = readFile(filename);
                stringPool.clear();

                std::vector<Instruction> bytecode = compileSource(source, keywordMap);
                VM vm(bytecode, stringPool);
                // vm.loadStringPool(stringPool);
                vm.run();
            }
        }

    } catch (const std::exception &ex) {
        std::cerr << "Lỗi: " << ex.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}