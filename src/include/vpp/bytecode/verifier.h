#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "vpp/bytecode/instruction.h"

namespace vietvm::bytecode {

// Ngữ cảnh tối thiểu cần để kiểm tra các tham chiếu ngoài chính vector bytecode.
struct BytecodeVerificationContext {
    std::size_t stringPoolSize = 0;
    std::unordered_set<int> functionIds;
};

// Mô tả lỗi cấu trúc đầu tiên của bytecode để VM/tooling có thể từ chối dữ liệu
// trước khi opcode được thực thi.
struct BytecodeVerificationIssue {
    std::size_t instructionIndex = 0;
    int rawOpcode = 0;
    std::string message;
};

// Kiểm tra cấu trúc bytecode và các tham chiếu chỉ số/địa chỉ có thể xác minh
// tĩnh. Trả nullopt khi toàn bộ chương trình hợp lệ theo contract hiện tại.
std::optional<BytecodeVerificationIssue> verifyBytecode(
    const std::vector<Instruction> &code,
    const BytecodeVerificationContext &context = {});

} // namespace vietvm::bytecode
