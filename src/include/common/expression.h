#pragma once
#include <string>
#include <unordered_map>
#include <vector>

// Biểu diễn một lệnh bytecode VM bằng opcode và ba operand; compiler phát struct này còn VM đọc các trường để dispatch handler.
struct Instruction;

namespace vietvm::compiler {

    // Chuyển đổi to postfix; hàm biến dữ liệu từ biểu diễn hiện tại sang biểu diễn đích và giữ nguyên ý nghĩa logic.
    std::vector<std::string> convertToPostfix(const std::vector<std::string>& infix_tokens);

    // inline std::unordered_map<int, std::vector<Instruction>> hamBytecodeMap;

    // Trả độ ưu tiên của toán tử khi chuyển biểu thức sang postfix; hàm tra bảng precedence dùng chung của lexer/compiler.
    int precedence_op(const std::string& op);
    // Xác định toán tử kết hợp trái hay phải; kết quả được thuật toán shunting-yard dùng khi quyết định pop toán tử khỏi stack.
    char associativity_op(const std::string& op);

} // namespace vietvm::Compiler