#pragma once

#include "vm/instruction.h"

#include <string>

namespace vietvm::bytecode {

// Trả tên văn bản ổn định cho opcode; hàm ánh xạ enum/giá trị nội bộ sang chuỗi để tooling, log hoặc test có thể hiển thị nhất quán.
std::string opcodeName(Opcode opcode);
// Ánh xạ mã opcode thô sang tên mà không cần tạo một giá trị enum ngoài miền hợp lệ;
// dùng cho diagnostic/verifier khi dữ liệu bytecode chưa được tin cậy.
std::string opcodeName(int rawOpcode);

} // namespace vietvm::bytecode
