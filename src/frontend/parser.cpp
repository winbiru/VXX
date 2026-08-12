#include "vpp/frontend/parser.h"

#include <utility>

#include "vpp/core/message_constants.h"

namespace vietvm::frontend {
namespace {

bool isVisibility(const std::string &lexeme) {
    return lexeme == "công khai" || lexeme == "riêng tư" || lexeme == "bảo vệ";
}

bool isNamePiece(const std::string &lexeme) {
    if (lexeme.empty()) return false;
    return lexeme != "(" && lexeme != ")" && lexeme != "{" && lexeme != "}" &&
           lexeme != "[" && lexeme != "]" && lexeme != ";" && lexeme != "," &&
           lexeme != ":" && lexeme != "=" && lexeme != "+" && lexeme != "-" &&
           lexeme != "*" && lexeme != "/" && lexeme != "%" && lexeme != "!" &&
           lexeme != "==" && lexeme != "!=" && lexeme != "<" && lexeme != ">" &&
           lexeme != "<=" && lexeme != ">=" && lexeme != "&&" && lexeme != "||";
}

} // namespace

ParseError::ParseError(std::string message, SourceSpan sourceSpan)
    : std::runtime_error(std::move(message)), span(sourceSpan) {}

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

AstProgram Parser::parseProgram() {
    AstProgram program;
    if (!tokens_.empty()) {
        program.span = {tokens_.front().span.begin, tokens_.back().span.end};
    }

    while (pos_ < tokens_.size()) {
        if (tokens_[pos_].lexeme == "}") {
            throw ParseError(
                vietvm::messages::formatMessage(vietvm::messages::kSyntaxUnbalancedBraces),
                tokens_[pos_].span);
        }
        program.statements.push_back(parseStatement(false));
    }

    program.tokens = std::move(tokens_);
    return program;
}

AstStatement Parser::parseBlock() {
    const std::size_t begin = pos_;
    if (pos_ >= tokens_.size() || tokens_[pos_].lexeme != "{") {
        const SourceSpan span = pos_ < tokens_.size() ? tokens_[pos_].span : SourceSpan{};
        throw ParseError(
            vietvm::messages::formatMessage(vietvm::messages::kSyntaxExpectedOpeningBlock), span);
    }

    ++pos_;
    AstStatement block;
    block.kind = AstStatementKind::Block;
    block.tokenBegin = begin;

    while (pos_ < tokens_.size() && tokens_[pos_].lexeme != "}") {
        block.children.push_back(parseStatement(true));
    }

    if (pos_ >= tokens_.size()) {
        throw ParseError(
            vietvm::messages::formatMessage(vietvm::messages::kSyntaxUnbalancedBraces),
            spanFor(begin, pos_));
    }

    ++pos_;
    block.tokenEnd = pos_;
    block.span = spanFor(begin, pos_);
    return block;
}

AstStatement Parser::parseStatement(bool insideBlock) {
    if (pos_ >= tokens_.size()) {
        return {};
    }

    const std::size_t begin = pos_;
    AstStatementKind kind = classify(begin);
    if (kind == AstStatementKind::Empty) {
        ++pos_;
        return {kind, spanFor(begin, pos_), begin, pos_, {}, {}};
    }
    if (kind == AstStatementKind::Block) {
        return parseBlock();
    }

    AstStatement statement;
    statement.kind = kind;
    statement.tokenBegin = begin;

    int parenDepth = 0;
    int bracketDepth = 0;
    int braceDepth = 0;
    std::size_t parsedBlocks = 0;

    while (pos_ < tokens_.size()) {
        const std::string &lexeme = tokens_[pos_].lexeme;

        if (lexeme == "(" ) {
            ++parenDepth;
            ++pos_;
            continue;
        }
        if (lexeme == ")") {
            if (parenDepth == 0) {
                throw ParseError(
                    vietvm::messages::formatMessage(vietvm::messages::kSyntaxUnbalancedParens),
                    tokens_[pos_].span);
            }
            --parenDepth;
            ++pos_;
            continue;
        }
        if (lexeme == "[") {
            ++bracketDepth;
            ++pos_;
            continue;
        }
        if (lexeme == "]") {
            if (bracketDepth > 0) --bracketDepth;
            ++pos_;
            continue;
        }

        const bool topLevelHeader = parenDepth == 0 && bracketDepth == 0 && braceDepth == 0;
        if (lexeme == "{" && topLevelHeader && isCompound(kind)) {
            statement.children.push_back(parseBlock());
            ++parsedBlocks;

            if (kind == AstStatementKind::Conditional &&
                pos_ < tokens_.size() && isContinuation(pos_)) {
                continue;
            }
            if (kind == AstStatementKind::Try && parsedBlocks == 1 &&
                pos_ < tokens_.size() && tokens_[pos_].lexeme == "bắt lỗi") {
                continue;
            }
            break;
        }

        if (lexeme == "{") {
            ++braceDepth;
            ++pos_;
            continue;
        }
        if (lexeme == "}") {
            if (braceDepth > 0) {
                --braceDepth;
                ++pos_;
                continue;
            }
            if (insideBlock) break;
            throw ParseError(
                vietvm::messages::formatMessage(vietvm::messages::kSyntaxUnbalancedBraces),
                tokens_[pos_].span);
        }

        ++pos_;
        if (lexeme == ";" && parenDepth == 0 && bracketDepth == 0 && braceDepth == 0) {
            break;
        }
    }

    if (parenDepth != 0) {
        throw ParseError(
            vietvm::messages::formatMessage(vietvm::messages::kSyntaxUnbalancedParens),
            spanFor(begin, pos_));
    }
    if (braceDepth != 0) {
        throw ParseError(
            vietvm::messages::formatMessage(vietvm::messages::kSyntaxUnbalancedBraces),
            spanFor(begin, pos_));
    }

    statement.tokenEnd = pos_;
    statement.span = spanFor(begin, pos_);
    statement.declarationName = declarationName(begin, pos_, kind);
    return statement;
}

AstStatementKind Parser::classify(std::size_t begin) const noexcept {
    if (begin >= tokens_.size()) return AstStatementKind::Unknown;
    const std::string &lexeme = tokens_[begin].lexeme;
    if (lexeme == ";") return AstStatementKind::Empty;
    if (lexeme == "{") return AstStatementKind::Block;
    if (lexeme == "nhập") return AstStatementKind::Import;
    if (lexeme == "hàm") return AstStatementKind::Function;
    if (lexeme == "lớp") return AstStatementKind::Class;
    if (lexeme == "nếu") return AstStatementKind::Conditional;
    if (lexeme == "lặp") return AstStatementKind::Loop;
    if (lexeme == "chọn") return AstStatementKind::Switch;
    if (lexeme == "trả về") return AstStatementKind::Return;
    if (lexeme == "in") return AstStatementKind::Print;
    if (lexeme == "thoát") return AstStatementKind::Break;
    if (lexeme == "bỏ qua") return AstStatementKind::Continue;
    if (lexeme == "ném") return AstStatementKind::Throw;
    if (lexeme == "thử") return AstStatementKind::Try;
    return AstStatementKind::Expression;
}

std::string Parser::declarationName(std::size_t begin,
                                    std::size_t end,
                                    AstStatementKind kind) const {
    if (begin >= end || end > tokens_.size()) return {};

    if (kind == AstStatementKind::Class) {
        std::size_t index = begin + 1;
        if (index < end && isVisibility(tokens_[index].lexeme)) ++index;
        return index < end && isNamePiece(tokens_[index].lexeme) ? tokens_[index].lexeme : std::string{};
    }

    if (kind != AstStatementKind::Function) return {};
    std::size_t index = begin + 1;
    if (index < end && isVisibility(tokens_[index].lexeme)) ++index;

    std::string name;
    while (index < end && tokens_[index].lexeme != "(" && tokens_[index].lexeme != "{" &&
           tokens_[index].lexeme != ";") {
        if (!isNamePiece(tokens_[index].lexeme)) break;
        if (!name.empty()) name.push_back(' ');
        name += tokens_[index].lexeme;
        ++index;
    }
    return name;
}

SourceSpan Parser::spanFor(std::size_t begin, std::size_t end) const noexcept {
    if (begin >= tokens_.size() || begin >= end) return {};
    const std::size_t last = end == 0 ? 0 : end - 1;
    if (last >= tokens_.size()) {
        return {tokens_[begin].span.begin, tokens_.back().span.end};
    }
    return {tokens_[begin].span.begin, tokens_[last].span.end};
}

bool Parser::isCompound(AstStatementKind kind) const noexcept {
    return kind == AstStatementKind::Function || kind == AstStatementKind::Class ||
           kind == AstStatementKind::Conditional || kind == AstStatementKind::Loop ||
           kind == AstStatementKind::Switch || kind == AstStatementKind::Try;
}

bool Parser::isContinuation(std::size_t tokenIndex) const noexcept {
    if (tokenIndex >= tokens_.size()) return false;
    const std::string &lexeme = tokens_[tokenIndex].lexeme;
    return lexeme == "hoặc" || lexeme == "nếu không";
}

AstProgram parseTokens(std::vector<Token> tokens) {
    return Parser(std::move(tokens)).parseProgram();
}

} // namespace vietvm::frontend
