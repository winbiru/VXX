#pragma once

#include "vm/instruction.h"

#include <string>

namespace vietvm::bytecode {

// Trả tên văn bản ổn định cho opcode; hàm ánh xạ enum/giá trị nội bộ sang chuỗi để tooling, log hoặc test có thể hiển thị nhất quán.
std::string opcodeName(Opcode opcode);

} // namespace vietvm::bytecode
