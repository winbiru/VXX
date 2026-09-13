#pragma once

#include <string>
#include <vector>

#include "../vm/instruction.h"

namespace vietvm::tooling {

// Chuyển dãy bytecode thành từng dòng opcode và operand; chỉ gắn nội dung `StringPool` cho các opcode có operand chuỗi thực sự.
std::string disassembleBytecode(const std::vector<Instruction> &bytecode,
                                const std::vector<std::string> &stringPool);

// Định dạng lại mã nguồn từ chuỗi token; hàm quản lý thụt lề theo dấu ngoặc khối và xuống dòng tại dấu chấm phẩy.
std::string formatSource(const std::string &source);

// Phân tích nguồn để thu thập lỗi lexer/parser/semantic và trả về danh sách chẩn đoán thay vì trực tiếp thực thi chương trình.
bool lintSource(const std::string &source, std::string &errorMessage);

} // namespace vietvm::tooling