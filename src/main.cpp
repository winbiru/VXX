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

int main() {
    try {
        // Duyệt qua tất cả file .vi trong thư mục tests
        std::string testDir = "../tests";
        for (const auto& entry : fs::directory_iterator(testDir)) {
            if (entry.path().extension() == ".vi") {
                const std::string filename = entry.path().string();
                std::cout << "\n=== Đang chạy test: " << filename << " ===\n";

                std::string source = readFile(filename);

                // Khởi tạo bảng từ khóa
                initCompileMap();

                // Biên dịch mã nguồn thành bytecode
                std::vector<Instruction> bytecode = compileSource(source, keywordMap);

                // // In toàn bộ bytecode để debug
                // std::cout << "Danh sách bytecode:" << std::endl;
                // for (size_t i = 0; i < bytecode.size(); ++i) {
                //     const Instruction &instr = bytecode[i];
                //     std::cout << "[" << i << "] "
                //               << "op: " << instr.op
                //               << " (" << name_op(instr.op) << ")";
                //     if (instr.operandIndex != -1)
                //         std::cout << ", operandIndex: " << instr.operandIndex;
                //     if (instr.operand != 0)
                //         std::cout << ", operand: " << instr.operand;
                //     std::cout << std::endl;
                // }
                //
                // // In ra stringPool để kiểm tra
                // std::cout << "\nDanh sách chuỗi đã lưu:\n";
                // for (size_t i = 0; i < stringPool.size(); ++i) {
                //     std::cout << "[" << i << "] = \"" << stringPool[i] << "\"\n";
                // }

                // Tạo máy ảo và truyền stringPool vào
                VM vm(bytecode);
                vm.loadStringPool(stringPool);  // ✅ Truyền chuỗi đã biên dịch vào VM

                // Thực thi chương trình
                // std::cout << "\nKết quả thực thi:\n";
                vm.run();
                stringPool.clear();
            }
        }
    } catch (const std::exception &ex) {
        std::cerr << "Lỗi: " << ex.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
