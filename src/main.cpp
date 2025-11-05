#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <filesystem>
#include "../include/compiler/compiler.h"
#include "../include/vm.h"
#include "../include/name_op.h"
#include "common/storeString.h"

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

        // -----------------------------
        // ✅ Trường hợp có đối số (chạy file được chỉ định)
        // -----------------------------
        if (argc == 2) {
            const std::string filename = argv[1];
            std::string source = readFile(filename);

            vietvm::compiler::StringPool::clear(); // luôn clear trước khi biên dịch mới
            std::vector<Instruction> bytecode = compileSource(source, keywordMap);

            const auto& stringPool = vietvm::compiler::StringPool::getPool();
            VM vm(bytecode, stringPool);

            // copy compiled functions into VM so OP_GOI can find them at runtime
            vm.hamBytecodeMap = vietvm::compiler::hamMap::hamBytecodeMap;
            std::cerr << "[main] copied hamBytecodeMap size=" << vm.hamBytecodeMap.size() << std::endl;

            vm.run();

            return EXIT_SUCCESS;
        }

        // -----------------------------
        // ✅ Nếu không có đối số → chạy file mặc định
        // -----------------------------
        const std::string defaultFile = "../tests/kiem_tra_ham.vi";
        if (argc == 1 && fs::exists(defaultFile)) {
            std::string source = readFile(defaultFile);

            vietvm::compiler::StringPool::clear();
            std::vector<Instruction> bytecode = compileSource(source, keywordMap);
            const auto& stringPool = vietvm::compiler::StringPool::getPool();

            // In bytecode để debug
            std::cout << "📜 Danh sách bytecode (" << defaultFile << "):" << std::endl;
            for (size_t i = 0; i < bytecode.size(); ++i) {
                const Instruction &instr = bytecode[i];
                std::cout << "[" << i << "] "
                          << "op: " << instr.op << " (" << name_op(instr.op) << ")";
                if (instr.operandIndex != -1)
                    std::cout << ", operandIndex: " << instr.operandIndex;
                if (instr.operand != 0)
                    std::cout << ", operand: " << instr.operand;
                std::cout << std::endl;
            }

            VM vm(bytecode, stringPool);

            // copy compiled functions into VM
            vm.hamBytecodeMap = vietvm::compiler::hamMap::hamBytecodeMap;
            std::cerr << "[main] copied hamBytecodeMap size=" << vm.hamBytecodeMap.size() << std::endl;

            vm.run();
            return EXIT_SUCCESS;
        }

        // -----------------------------
        // ✅ Nếu không có cả 2 → chạy toàn bộ thư mục "tests/"
        // -----------------------------
        std::string testDir = "tests/";
        if (fs::exists(testDir)) {
            for (const auto& entry : fs::directory_iterator(testDir)) {
                if (entry.path().extension() == ".vi") {
                    const std::string filename = entry.path().string();
                    std::cout << "\n🔹 Đang chạy test: " << filename << std::endl;

                    std::string source = readFile(filename);
                    vietvm::compiler::StringPool::clear();
                    std::vector<Instruction> bytecode = compileSource(source, keywordMap);
                    const auto& stringPool = vietvm::compiler::StringPool::getPool();

                    VM vm(bytecode, stringPool);
                    // copy compiled functions into VM
                    vm.hamBytecodeMap = vietvm::compiler::hamMap::hamBytecodeMap;
                    std::cerr << "[main] copied hamBytecodeMap size=" << vm.hamBytecodeMap.size() << std::endl;

                    vm.run();
                }
            }
        } else {
            std::cerr << "⚠️ Thư mục tests/ không tồn tại.\n";
        }

    } catch (const std::exception &ex) {
        std::cerr << "❌ Lỗi: " << ex.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}