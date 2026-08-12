#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace vietvm::frontend {

// Positions are byte-oriented because the existing lexer consumes UTF-8 as a
// byte stream.  Line and column stay one-based for diagnostics shown to users.
struct SourcePosition {
    std::size_t offset = 0;
    std::size_t line = 1;
    std::size_t column = 1;
};

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

struct Token {
    TokenKind kind = TokenKind::Unknown;
    std::string lexeme;
    SourceSpan span{};
};

std::vector<std::string> tokenLexemes(const std::vector<Token> &tokens);

} // namespace vietvm::frontend
