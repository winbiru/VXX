#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "vpp/compiler/ir.h"
#include "vm/instruction.h"

namespace vietvm::compiler {

struct CompilationRegistryState;

// Tổng hợp kết quả kiểm tra backend direct IR, gồm số vùng chưa hỗ trợ và thông tin để pipeline quyết định có thể phát bytecode trực tiếp hay không.
struct DirectIrSupport {
    bool supported = false;
    std::size_t unsupportedRegions = 0;
};

// Duyệt IR để xác định phần nào backend direct IR có thể phát bytecode mà không cần rơi về đường biên dịch cũ.
DirectIrSupport analyzeDirectIrSupport(const IrProgram &program);

// Phát bytecode trực tiếp từ IR đã được xác nhận hỗ trợ; emitter ánh xạ lệnh/giá trị IR thành opcode VM và metadata tương ứng.
std::vector<Instruction> emitDirectBytecode(CompilationRegistryState &state,
                                            const IrProgram &program,
                                            const std::unordered_map<std::string, Opcode> &keywordMap,
                                            bool emitMainCall = true);

} // namespace vietvm::compiler
