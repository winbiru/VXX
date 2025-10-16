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
        if (argc != 2) {
            std::cerr << "Cách dùng: " << argv[0] << " <file.vi>\n";
            return EXIT_FAILURE;
        }

        std::string filename = argv[1];
        std::string source = readFile(filename);

        initCompileMap();
        stringPool.clear();

        std::vector<Instruction> bytecode = compileSource(source, keywordMap);
        VM vm(bytecode);
        vm.loadStringPool(stringPool);
        vm.run();

    } catch (const std::exception &ex) {
        std::cerr << "Lỗi: " << ex.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
