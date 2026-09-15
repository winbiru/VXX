#pragma once

#include "vm/instruction.h"

#include <string>

namespace vietvm::bytecode {

// Trả tên văn bản ổn định cho opcode; hàm ánh xạ enum/giá trị nội bộ sang chuỗi để tooling, log hoặc test có thể hiển thị nhất quán.
std::string opcodeName(Opcode opcode);
// Ánh xạ mã opcode thô sang tên mà không cần tạo một giá trị enum ngoài miền hợp lệ;
// dùng cho diagnostic/verifier khi dữ liệu bytecode chưa được tin cậy.
std::string opcodeName(int rawOpcode);
// Kiểm tra mã opcode thô có thuộc tập lệnh VM hiện hành hay không; verifier dùng
// hàm này trước khi dispatch dữ liệu bytecode chưa được tin cậy.
bool isKnownOpcode(int rawOpcode) noexcept;

} // namespace vietvm::bytecode
