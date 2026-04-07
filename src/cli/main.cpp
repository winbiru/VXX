#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <filesystem>
#include "../../include/compiler/compiler.h"
#include "../../include/vm/vm.h"
#include "../../include/frontend/keywords.h"
#include "common/storeString.h"
#include "../../include/compiler/compileRegistry.h"

namespace fs = std::filesystem;

// RAII guard to restore current working directory on scope exit
struct CwdGuard {
    fs::path saved;
    explicit CwdGuard(fs::path p) : saved(std::move(p)) {}
    ~CwdGuard() { try { fs::current_path(saved); } catch(...) {} }
    CwdGuard(const CwdGuard&) = delete;
    CwdGuard& operator=(const CwdGuard&) = delete;
};

std::string readFile(const std::string &filename) {
    std::ifstream fileStream(filename);
    if (!fileStream.is_open()) {
        throw std::runtime_error("Không thể mở file: " + filename);
    }
    std::stringstream buffer;
    buffer << fileStream.rdbuf();
    return buffer.str();
}


int main(int argc, char* argv[]) {
    try {
        initCompileMap();

        // -----------------------------
        // Trường hợp có đối số (chạy file được chỉ định)
        // -----------------------------
        if (argc == 2) {
            const std::string filename = argv[1];
            std::string source = readFile(filename);

            // Ensure relative imports inside the compiled file resolve relative to the file's directory
            // Use RAII-style guard to always restore cwd even on exception
            CwdGuard cwdGuard(fs::current_path());
            if (!fs::path(filename).parent_path().empty()) {
                fs::current_path(fs::path(filename).parent_path());
            }

            vietvm::compiler::StringPool::clear();
            // Reset imported files tracking between compilations
            vietvm::compiler::clearImportedFiles();
            vietvm::compiler::hamMap::hamBytecodeMap.clear();
            vietvm::compiler::hamMap::clearHamNameIndexMap();
            vietvm::compiler::hamMap::resetHamIdCounter();
            std::vector<Instruction> bytecode = compileSource(source, keywordMap);
            // cwd will be restored by CwdGuard destructor

            const auto& stringPool = vietvm::compiler::StringPool::getPool();
            VM vm(bytecode, stringPool);

            // <-- ADD: copy compiled functions into VM so OP_GOI can find them
            vm.hamBytecodeMap = vietvm::compiler::hamMap::hamBytecodeMap;

            vm.run();

            return EXIT_SUCCESS;
        }

        // -----------------------------
        // Nếu không có đối số → chạy file mặc định
        // -----------------------------
        const std::string defaultFile = "../../src/tests/import_main.vi";
        if (argc == 1 && fs::exists(defaultFile)) {
            std::string source = readFile(defaultFile);

            // run default file with cwd set to its parent so imports resolve
            // Use RAII-style guard to always restore cwd even on exception
            CwdGuard cwdGuard(fs::current_path());
            if (!fs::path(defaultFile).parent_path().empty()) {
                fs::current_path(fs::path(defaultFile).parent_path());
            }

            vietvm::compiler::StringPool::clear();
            // Reset imported files tracking between compilations
            vietvm::compiler::clearImportedFiles();
            vietvm::compiler::hamMap::hamBytecodeMap.clear();
            vietvm::compiler::hamMap::clearHamNameIndexMap();
            vietvm::compiler::hamMap::resetHamIdCounter();

            std::vector<Instruction> bytecode = compileSource(source, keywordMap);
            // cwd will be restored by CwdGuard destructor
            const auto& stringPool = vietvm::compiler::StringPool::getPool();

            // // In bytecode để debug
            // std::cout << "=> Danh sách bytecode cho file mã nguồn (" << defaultFile << "):" << std::endl;
            // for (size_t i = 0; i < bytecode.size(); ++i) {
            //     const Instruction &instr = bytecode[i];
            //     std::cout << "[" << i << "] "
            //               << "op: " << instr.op << " (" << name_op(instr.op) << ")";
            //     if (instr.operandIndex != -1)
            //         std::cout << ", operandIndex: " << instr.operandIndex;
            //     if (instr.operand != 0)
            //         std::cout << ", operand: " << instr.operand;
            //     std::cout << std::endl;
            // }
            VM vm(bytecode, stringPool);

            // copy compiled functions into VM
            vm.hamBytecodeMap = vietvm::compiler::hamMap::hamBytecodeMap;
            vm.run();
            return EXIT_SUCCESS;
        }

        std::string testDir = "../../src/tests";
        if (fs::exists(testDir)) {
            for (const auto& entry : fs::directory_iterator(testDir)) {
                if (entry.path().extension() == ".vi") {
                    const std::string filename = entry.path().string();
                    std::cout << "\n🔹 Đang chạy test: " << filename << std::endl;

                    std::string source = readFile(filename);
                    // Ensure imports inside each test file resolve relative to the test file location
                    // Use RAII-style guard to always restore cwd even on exception
                    CwdGuard cwdGuard(fs::current_path());
                    if (!fs::path(filename).parent_path().empty()) {
                        fs::current_path(fs::path(filename).parent_path());
                    }

                    vietvm::compiler::StringPool::clear();
                    // Reset imported files tracking giữa các lần biên dịch
                    vietvm::compiler::clearImportedFiles();
                    std::vector<Instruction> bytecode = compileSource(source, keywordMap);
                    // cwd will be restored by CwdGuard destructor
                    const auto& stringPool = vietvm::compiler::StringPool::getPool();

                    VM vm(bytecode, stringPool);
                    // copy compiled functions into VM
                    vm.hamBytecodeMap = vietvm::compiler::hamMap::hamBytecodeMap;
                    vm.run();
                }
            }
        } else {
            std::cerr << "Thư mục tests/ không tồn tại.\n";
        }

    } catch (const std::exception &ex) {
        std::cerr << "Lỗi: " << ex.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
