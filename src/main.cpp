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
            // ✅ Chế độ chạy 1 file (dành cho make check hoặc test riêng lẻ)
            const std::string filename = argv[1];
            std::string source = readFile(filename);
            stringPool.clear();

            std::vector<Instruction> bytecode = compileSource(source, keywordMap);
            VM vm(bytecode);
            vm.loadStringPool(stringPool);
            vm.run();
        } else {
            // ✅ Chế độ chạy toàn bộ thư mục tests/ (dành cho test local nhanh)
            std::string testDir = "../tests";
            for (const auto& entry : fs::directory_iterator(testDir)) {
                if (entry.path().extension() == ".vi") {
                    const std::string filename = entry.path().string();
                    std::string source = readFile(filename);
                    stringPool.clear();

                    std::vector<Instruction> bytecode = compileSource(source, keywordMap);
                    VM vm(bytecode);
                    vm.loadStringPool(stringPool);
                    vm.run();
                }
            }
        }

    } catch (const std::exception &ex) {
        std::cerr << "Lỗi: " << ex.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
