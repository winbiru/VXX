#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace vietvm::frontend {

// Biểu diễn một tọa độ nguồn bằng dòng, cột và offset; lexer cập nhật record này khi quét để tạo diagnostic chính xác.
struct SourcePosition {
    std::size_t offset = 0;
    std::size_t line = 1;
    std::size_t column = 1;
};

// Biểu diễn đoạn nguồn nửa mở từ vị trí bắt đầu tới kết thúc; AST/semantic/IR mang span này xuyên suốt pipeline.
struct SourceSpan {
    SourcePosition begin{};
    SourcePosition end{};
};

enum class TokenKind {
    Identifier,
    Integer,
    Float,
    String,
    Keyword,
    Operator,
    Punctuation,
    Unknown,
};

// Lưu một token lexer gồm kind, lexeme và source span; parser dùng dãy token này để dựng AST mà vẫn giữ vị trí nguồn.
struct Token {
    TokenKind kind = TokenKind::Unknown;
    std::string lexeme;
    SourceSpan span{};
};

// Chuyển lexeme; hàm chuyển giá trị đầu vào sang kiểu/biểu diễn đích và trả kết quả đã chuẩn hóa.
std::vector<std::string> tokenLexemes(const std::vector<Token> &tokens);

} // namespace vietvm::frontend
