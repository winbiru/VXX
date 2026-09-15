#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "../vm/instruction.h"
#include "vpp/frontend/token.h"

namespace vietvm::tooling {

enum class DiagnosticSeverity {
    Warning,
    Error,
};

// Chẩn đoán có cấu trúc dùng chung cho CLI và editor/LSP. Span giữ tọa độ nguồn
// 1-based như lexer/parser; caller chỉ cần đổi sang 0-based khi phát LSP range.
struct LintDiagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string message;
    vietvm::frontend::SourceSpan span{};
};

// Chuyển dãy bytecode thành từng dòng opcode và operand; chỉ gắn nội dung `StringPool` cho các opcode có operand chuỗi thực sự.
std::string disassembleBytecode(const std::vector<Instruction> &bytecode,
                                const std::vector<std::string> &stringPool);

// Định dạng lại mã nguồn từ chuỗi token; hàm quản lý thụt lề theo dấu ngoặc khối và xuống dòng tại dấu chấm phẩy.
std::string formatSource(const std::string &source);

// Phân tích nguồn và trả diagnostic có cấu trúc cho lexer/parser/semantic/compiler.
// Danh sách rỗng nghĩa là nguồn hợp lệ theo pipeline hiện tại.
std::vector<LintDiagnostic> lintDiagnostics(const std::string &source);
std::vector<LintDiagnostic> lintDiagnostics(
    const std::string &source,
    const std::filesystem::path &resolutionBase);

// Phân tích nguồn để thu thập lỗi lexer/parser/semantic và trả về danh sách chẩn đoán thay vì trực tiếp thực thi chương trình.
bool lintSource(const std::string &source, std::string &errorMessage);

} // namespace vietvm::tooling
