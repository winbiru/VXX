// main.cpp
#include <iostream>
#include <string>
#include "src/include/compiler.h"
#include "src/include/vm.h"

int main() {
    // Mã nguồn ví dụ: chương trình cộng 2 số và in kết quả
    std::string source = R"(
    BIẾN 2
    BIẾN 3
    CỘNG
    IN
    BIẾN 30
    BIẾN 4
    TRỪ
    IN
    DỪNG
    )";

    try {
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
