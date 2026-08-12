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

bool isExpressionNamePiece(const Token &token) {
    const std::string &lexeme = token.lexeme;
    if (lexeme.empty() || lexeme == "đúng" || lexeme == "sai" || lexeme == "rỗng" ||
        lexeme == "hàm" || lexeme == "gọi") {
        return false;
    }
    if (token.kind == TokenKind::Integer || token.kind == TokenKind::Float ||
        token.kind == TokenKind::String || token.kind == TokenKind::Operator) {
        return false;
    }
    return lexeme != "(" && lexeme != ")" && lexeme != "{" && lexeme != "}" &&
           lexeme != "[" && lexeme != "]" && lexeme != ";" && lexeme != "," &&
           lexeme != ":";
}

bool isCallableNamePiece(const Token &token) {
    const std::string &lexeme = token.lexeme;
    if (lexeme.empty() || token.kind == TokenKind::Integer ||
        token.kind == TokenKind::Float || token.kind == TokenKind::String ||
        token.kind == TokenKind::Operator) {
        return false;
    }
    return lexeme != "(" && lexeme != ")" && lexeme != "{" && lexeme != "}" &&
           lexeme != "[" && lexeme != "]" && lexeme != ";" && lexeme != "," &&
           lexeme != ":";
}

bool isAssignmentOperator(const std::string &lexeme) {
    return lexeme == "=" || lexeme == "+=" || lexeme == "-=" || lexeme == "*=" ||
           lexeme == "/=" || lexeme == "%=";
}

int leftBindingPower(const std::string &lexeme) {
    // The legacy shunting-yard parser gives every assignment spelling the
    // same precedence, but only plain '=' is right-associative. Giving '=' a
    // distinct left binding power preserves that behavior even when plain and
    // compound assignments are mixed in one expression.
    if (lexeme == "+=" || lexeme == "-=" || lexeme == "*=" ||
        lexeme == "/=" || lexeme == "%=") {
        return 0;
    }
    if (lexeme == "=") return 1;
    if (lexeme == "||") return 2;
    if (lexeme == "&&") return 3;
    if (lexeme == "==" || lexeme == "!=" || lexeme == "<" || lexeme == ">" ||
        lexeme == "<=" || lexeme == ">=") {
        return 4;
    }
    if (lexeme == "+" || lexeme == "-") return 5;
    if (lexeme == "*" || lexeme == "/" || lexeme == "%") return 6;
    return -1;
}

int rightBindingPower(const std::string &lexeme) {
    return isAssignmentOperator(lexeme) ? 1 : leftBindingPower(lexeme) + 1;
}

bool beginsLambda(const std::vector<Token> &tokens,
                  std::size_t begin,
                  std::size_t end) {
    if (begin + 1 >= end || tokens[begin].lexeme != "hàm" ||
        tokens[begin + 1].lexeme != "(") {
        return false;
    }
    int depth = 0;
    for (std::size_t index = begin + 1; index < end; ++index) {
        if (tokens[index].lexeme == "(") {
            ++depth;
        } else if (tokens[index].lexeme == ")" && --depth == 0) {
            return index + 1 < end && tokens[index + 1].lexeme == "{";
        }
    }
    return false;
}

std::string joinTokenText(const std::vector<Token> &tokens,
                          std::size_t begin,
                          std::size_t end) {
    std::string result;
    for (std::size_t index = begin; index < end; ++index) {
        if (!result.empty()) result.push_back(' ');
        result += tokens[index].lexeme;
    }
    return result;
}

SourceSpan tokenRangeSpan(const std::vector<Token> &tokens,
                          std::size_t begin,
                          std::size_t end) {
    if (begin >= end || end > tokens.size()) return {};
    return {tokens[begin].span.begin, tokens[end - 1].span.end};
}

AstVisibility visibilityFor(const std::string &lexeme) {
    if (lexeme == "công khai") return AstVisibility::Public;
    if (lexeme == "riêng tư") return AstVisibility::Private;
    if (lexeme == "bảo vệ") return AstVisibility::Protected;
    return AstVisibility::Unspecified;
}

// Pratt reader for one bounded token slice. It never throws diagnostics: the
// statement parser remains tolerant and commits a root only after the entire
// slice has been consumed.
class ExpressionReader {
public:
    ExpressionReader(const std::vector<Token> &tokens,
                     std::vector<AstExpression> &expressions,
                     std::size_t begin,
                     std::size_t end)
        : tokens_(tokens), expressions_(expressions), pos_(begin), end_(end) {}

    ExprId parse() { return parseExpression(0); }
    std::size_t position() const noexcept { return pos_; }

private:
    ExprId append(AstExpression expression) {
        expression.id = expressions_.size();
        expression.span = tokenRangeSpan(tokens_, expression.tokenBegin, expression.tokenEnd);
        expressions_.push_back(std::move(expression));
        return expressions_.back().id;
    }

    ExprId parseExpression(int minimumPrecedence) {
        ExprId left = parsePrefix();
        if (left == kInvalidExprId) return kInvalidExprId;

        while (pos_ < end_) {
            if (tokens_[pos_].lexeme == "(") {
                left = parseCall(left);
                if (left == kInvalidExprId) return kInvalidExprId;
                continue;
            }

            if (tokens_[pos_].lexeme == "++" || tokens_[pos_].lexeme == "--") {
                const std::size_t begin = expressions_[left].tokenBegin;
                const std::string op = tokens_[pos_].lexeme;
                ++pos_;
                AstExpression postfix;
                postfix.kind = AstExpressionKind::Postfix;
                postfix.tokenBegin = begin;
                postfix.tokenEnd = pos_;
                postfix.text = op;
                postfix.operand = left;
                left = append(std::move(postfix));
                continue;
            }

            const std::string &op = tokens_[pos_].lexeme;
            const int bindingPower = leftBindingPower(op);
            if (bindingPower < minimumPrecedence) break;

            const std::size_t begin = expressions_[left].tokenBegin;
            const bool assignment = isAssignmentOperator(op);
            ++pos_;
            ExprId right = parseExpression(rightBindingPower(op));
            if (right == kInvalidExprId) return kInvalidExprId;

            AstExpression binary;
            binary.kind = op == "="
                ? AstExpressionKind::Assignment
                : (assignment ? AstExpressionKind::CompoundAssignment
                              : AstExpressionKind::Binary);
            binary.tokenBegin = begin;
            binary.tokenEnd = expressions_[right].tokenEnd;
            binary.text = op;
            binary.left = left;
            binary.right = right;
            left = append(std::move(binary));
        }
        return left;
    }

    ExprId parsePrefix() {
        if (pos_ >= end_) return kInvalidExprId;
        const std::size_t begin = pos_;
        const std::string &lexeme = tokens_[pos_].lexeme;

        if (lexeme == "!" || lexeme == "+" || lexeme == "-") {
            const std::string op = lexeme;
            ++pos_;
            ExprId operand = parseExpression(7);
            if (operand == kInvalidExprId) return kInvalidExprId;
            AstExpression unary;
            unary.kind = AstExpressionKind::Unary;
            unary.tokenBegin = begin;
            unary.tokenEnd = expressions_[operand].tokenEnd;
            unary.text = op;
            unary.operand = operand;
            return append(std::move(unary));
        }

        if (lexeme == "(") {
            ++pos_;
            ExprId grouped = parseExpression(0);
            if (grouped == kInvalidExprId || pos_ >= end_ || tokens_[pos_].lexeme != ")") {
                return kInvalidExprId;
            }
            ++pos_;
            expressions_[grouped].tokenBegin = begin;
            expressions_[grouped].tokenEnd = pos_;
            expressions_[grouped].span = tokenRangeSpan(tokens_, begin, pos_);
            return grouped;
        }

        if (lexeme == "{") return parseMapLiteral();
        if (beginsLambda(tokens_, pos_, end_)) {
            return parseLambda();
        }

        if (lexeme == "gọi" && pos_ + 1 < end_ &&
            tokens_[pos_ + 1].lexeme != "(") {
            ++pos_;
            ExprId callee = parseName(true);
            if (callee == kInvalidExprId || pos_ >= end_ || tokens_[pos_].lexeme != "(") {
                return kInvalidExprId;
            }
            ExprId call = parseCall(callee);
            if (call != kInvalidExprId) {
                expressions_[call].tokenBegin = begin;
                expressions_[call].span = tokenRangeSpan(
                    tokens_, begin, expressions_[call].tokenEnd);
            }
            return call;
        }

        // Callable names are maximal and contextual. Keyword spellings such
        // as `đúng` and `rỗng` are literals on their own, but remain valid
        // pieces of names like `yêu cầu đúng(...)` and `là chuỗi rỗng(...)`.
        std::size_t callableEnd = pos_;
        while (callableEnd < end_ && isCallableNamePiece(tokens_[callableEnd])) {
            ++callableEnd;
        }
        if (callableEnd > pos_ && callableEnd < end_ &&
            tokens_[callableEnd].lexeme == "(") {
            return parseName(true);
        }

        AstLiteralKind literalKind = AstLiteralKind::None;
        if (tokens_[pos_].kind == TokenKind::Integer) literalKind = AstLiteralKind::Integer;
        else if (tokens_[pos_].kind == TokenKind::Float) literalKind = AstLiteralKind::Float;
        else if (tokens_[pos_].kind == TokenKind::String) literalKind = AstLiteralKind::String;
        else if (lexeme == "đúng" || lexeme == "sai") literalKind = AstLiteralKind::Boolean;
        else if (lexeme == "rỗng") literalKind = AstLiteralKind::Null;

        if (literalKind != AstLiteralKind::None) {
            ++pos_;
            AstExpression literal;
            literal.kind = AstExpressionKind::Literal;
            literal.literalKind = literalKind;
            literal.tokenBegin = begin;
            literal.tokenEnd = pos_;
            literal.text = lexeme;
            return append(std::move(literal));
        }

        return parseName(false);
    }

    ExprId parseName(bool callable) {
        const std::size_t begin = pos_;
        while (pos_ < end_ &&
               (callable ? isCallableNamePiece(tokens_[pos_])
                         : isExpressionNamePiece(tokens_[pos_]))) {
            ++pos_;
        }
        if (begin == pos_) return kInvalidExprId;

        AstExpression name;
        name.kind = AstExpressionKind::Name;
        name.tokenBegin = begin;
        name.tokenEnd = pos_;
        name.text = joinTokenText(tokens_, begin, pos_);
        return append(std::move(name));
    }

    ExprId parseCall(ExprId callee) {
        if (callee == kInvalidExprId || pos_ >= end_ || tokens_[pos_].lexeme != "(") {
            return kInvalidExprId;
        }
        const std::size_t begin = expressions_[callee].tokenBegin;
        ++pos_;
        std::vector<ExprId> arguments;

        if (pos_ < end_ && tokens_[pos_].lexeme != ")") {
            while (true) {
                ExprId argument = parseExpression(0);
                if (argument == kInvalidExprId) return kInvalidExprId;
                arguments.push_back(argument);
                if (pos_ >= end_ || tokens_[pos_].lexeme != ",") break;
                ++pos_;
                if (pos_ >= end_ || tokens_[pos_].lexeme == ")") return kInvalidExprId;
            }
        }

        if (pos_ >= end_ || tokens_[pos_].lexeme != ")") return kInvalidExprId;
        ++pos_;
        AstExpression call;
        call.kind = AstExpressionKind::Call;
        call.tokenBegin = begin;
        call.tokenEnd = pos_;
        call.callee = callee;
        call.arguments = std::move(arguments);
        return append(std::move(call));
    }

    ExprId parseLambda() {
        const std::size_t begin = pos_;
        ++pos_;
        if (pos_ >= end_ || tokens_[pos_].lexeme != "(") return kInvalidExprId;
        ++pos_;

        std::vector<std::string> parameters;
        std::size_t parameterBegin = pos_;
        int parenDepth = 0;
        int bracketDepth = 0;
        int braceDepth = 0;
        while (pos_ < end_) {
            const std::string &token = tokens_[pos_].lexeme;
            if (token == "(" ) ++parenDepth;
            else if (token == ")") {
                if (parenDepth == 0 && bracketDepth == 0 && braceDepth == 0) {
                    if (parameterBegin < pos_) {
                        std::size_t nameEnd = parameterBegin;
                        while (nameEnd < pos_ && tokens_[nameEnd].lexeme != "=") ++nameEnd;
                        if (parameterBegin < nameEnd) {
                            parameters.push_back(joinTokenText(tokens_, parameterBegin, nameEnd));
                        }
                    }
                    break;
                }
                --parenDepth;
            } else if (token == "[") ++bracketDepth;
            else if (token == "]") --bracketDepth;
            else if (token == "{") ++braceDepth;
            else if (token == "}") --braceDepth;
            else if (token == "," && parenDepth == 0 && bracketDepth == 0 && braceDepth == 0) {
                std::size_t nameEnd = parameterBegin;
                while (nameEnd < pos_ && tokens_[nameEnd].lexeme != "=") ++nameEnd;
                if (parameterBegin < nameEnd) {
                    parameters.push_back(joinTokenText(tokens_, parameterBegin, nameEnd));
                }
                parameterBegin = pos_ + 1;
            }
            ++pos_;
        }
        if (pos_ >= end_ || tokens_[pos_].lexeme != ")") return kInvalidExprId;
        ++pos_;
        if (pos_ >= end_ || tokens_[pos_].lexeme != "{") return kInvalidExprId;

        const std::size_t bodyBegin = pos_;
        int depth = 0;
        while (pos_ < end_) {
            if (tokens_[pos_].lexeme == "{") ++depth;
            else if (tokens_[pos_].lexeme == "}") {
                --depth;
                if (depth == 0) {
                    ++pos_;
                    break;
                }
            }
            ++pos_;
        }
        if (depth != 0) return kInvalidExprId;

        AstExpression lambda;
        lambda.kind = AstExpressionKind::Lambda;
        lambda.tokenBegin = begin;
        lambda.tokenEnd = pos_;
        lambda.parameters = std::move(parameters);
        lambda.bodyTokenBegin = bodyBegin;
        lambda.bodyTokenEnd = pos_;
        return append(std::move(lambda));
    }

    ExprId parseMapLiteral() {
        const std::size_t begin = pos_;
        ++pos_;
        std::vector<AstMapEntry> entries;

        while (pos_ < end_ && tokens_[pos_].lexeme != "}") {
            ExprId key = parseExpression(0);
            if (key == kInvalidExprId || pos_ >= end_ || tokens_[pos_].lexeme != ":") {
                return kInvalidExprId;
            }
            ++pos_;
            ExprId value = parseExpression(0);
            if (value == kInvalidExprId) return kInvalidExprId;
            entries.push_back({key, value,
                               {expressions_[key].span.begin, expressions_[value].span.end}});

            if (pos_ >= end_ || tokens_[pos_].lexeme != ",") break;
            ++pos_;
        }

        if (pos_ >= end_ || tokens_[pos_].lexeme != "}") return kInvalidExprId;
        ++pos_;
        AstExpression map;
        map.kind = AstExpressionKind::MapLiteral;
        map.tokenBegin = begin;
        map.tokenEnd = pos_;
        map.mapEntries = std::move(entries);
        return append(std::move(map));
    }

    const std::vector<Token> &tokens_;
    std::vector<AstExpression> &expressions_;
    std::size_t pos_;
    std::size_t end_;
};

std::size_t matchingDelimiter(const std::vector<Token> &tokens,
                              std::size_t open,
                              std::size_t end,
                              const std::string &left,
                              const std::string &right) {
    if (open >= end || tokens[open].lexeme != left) return end;
    int depth = 0;
    for (std::size_t index = open; index < end; ++index) {
        if (tokens[index].lexeme == left) ++depth;
        else if (tokens[index].lexeme == right && --depth == 0) return index;
    }
    return end;
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

    program.expressions = std::move(expressions_);
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
        AstStatement empty;
        empty.kind = kind;
        empty.span = spanFor(begin, pos_);
        empty.tokenBegin = begin;
        empty.tokenEnd = pos_;
        return empty;
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
    std::vector<char> delimiterStack;
    std::size_t parsedBlocks = 0;

    while (pos_ < tokens_.size()) {
        const std::string &lexeme = tokens_[pos_].lexeme;

        if (lexeme == "(" ) {
            ++parenDepth;
            delimiterStack.push_back('(');
            ++pos_;
            continue;
        }
        if (lexeme == ")") {
            if (parenDepth == 0 || delimiterStack.empty() ||
                delimiterStack.back() != '(') {
                throw ParseError(
                    vietvm::messages::formatMessage(vietvm::messages::kSyntaxUnbalancedParens),
                    tokens_[pos_].span);
            }
            --parenDepth;
            delimiterStack.pop_back();
            ++pos_;
            continue;
        }
        if (lexeme == "[") {
            ++bracketDepth;
            delimiterStack.push_back('[');
            ++pos_;
            continue;
        }
        if (lexeme == "]") {
            if (bracketDepth == 0 || delimiterStack.empty() ||
                delimiterStack.back() != '[') {
                throw ParseError(
                    vietvm::messages::formatMessage(
                        vietvm::messages::kSyntaxUnbalancedBrackets),
                    tokens_[pos_].span);
            }
            --bracketDepth;
            delimiterStack.pop_back();
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
            delimiterStack.push_back('{');
            ++pos_;
            continue;
        }
        if (lexeme == "}") {
            if (braceDepth > 0) {
                if (delimiterStack.empty() || delimiterStack.back() != '{') {
                    throw ParseError(
                        vietvm::messages::formatMessage(
                            vietvm::messages::kSyntaxUnbalancedBraces),
                        tokens_[pos_].span);
                }
                --braceDepth;
                delimiterStack.pop_back();
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

    if (!delimiterStack.empty()) {
        const char unclosed = delimiterStack.back();
        const auto &message = unclosed == '('
            ? vietvm::messages::kSyntaxUnbalancedParens
            : (unclosed == '[' ? vietvm::messages::kSyntaxUnbalancedBrackets
                               : vietvm::messages::kSyntaxUnbalancedBraces);
        throw ParseError(vietvm::messages::formatMessage(message), spanFor(begin, pos_));
    }

    statement.tokenEnd = pos_;
    statement.span = spanFor(begin, pos_);
    statement.declarationName = declarationName(begin, pos_, kind);
    attachDeclarationPayload(statement);
    attachExpressionRoots(statement);
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

void Parser::attachDeclarationPayload(AstStatement &statement) {
    if (statement.kind != AstStatementKind::Function &&
        statement.kind != AstStatementKind::Class) {
        return;
    }

    std::size_t index = statement.tokenBegin + 1;
    if (index < statement.tokenEnd && isVisibility(tokens_[index].lexeme)) {
        statement.visibility = visibilityFor(tokens_[index].lexeme);
        ++index;
    }
    if (statement.kind != AstStatementKind::Function) return;

    std::size_t open = index;
    while (open < statement.tokenEnd && tokens_[open].lexeme != "(") ++open;
    if (open >= statement.tokenEnd) return;
    const std::size_t close = matchingDelimiter(
        tokens_, open, statement.tokenEnd, "(", ")");
    if (close >= statement.tokenEnd) return;

    auto addParameter = [&](std::size_t begin, std::size_t end) {
        if (begin >= end) return;
        std::size_t equals = end;
        int parenDepth = 0;
        int bracketDepth = 0;
        int braceDepth = 0;
        for (std::size_t cursor = begin; cursor < end; ++cursor) {
            const std::string &token = tokens_[cursor].lexeme;
            if (token == "(") ++parenDepth;
            else if (token == ")") --parenDepth;
            else if (token == "[") ++bracketDepth;
            else if (token == "]") --bracketDepth;
            else if (token == "{") ++braceDepth;
            else if (token == "}") --braceDepth;
            else if (token == "=" && parenDepth == 0 && bracketDepth == 0 &&
                     braceDepth == 0) {
                equals = cursor;
                break;
            }
        }
        if (equals == begin) return;

        AstParameter parameter;
        parameter.name = joinTokenText(tokens_, begin, equals);
        parameter.span = tokenRangeSpan(tokens_, begin, end);
        parameter.hasDefault = equals < end;
        if (parameter.hasDefault) {
            parameter.defaultValue = tryParseExpression(equals + 1, end);
        }
        statement.parameters.push_back(std::move(parameter));
    };

    std::size_t parameterBegin = open + 1;
    int parenDepth = 0;
    int bracketDepth = 0;
    int braceDepth = 0;
    for (std::size_t cursor = parameterBegin; cursor < close; ++cursor) {
        const std::string &token = tokens_[cursor].lexeme;
        if (token == "(") ++parenDepth;
        else if (token == ")") --parenDepth;
        else if (token == "[") ++bracketDepth;
        else if (token == "]") --bracketDepth;
        else if (token == "{") ++braceDepth;
        else if (token == "}") --braceDepth;
        else if (token == "," && parenDepth == 0 && bracketDepth == 0 &&
                 braceDepth == 0) {
            addParameter(parameterBegin, cursor);
            parameterBegin = cursor + 1;
        }
    }
    addParameter(parameterBegin, close);
}

ExprId Parser::tryParseExpression(std::size_t begin, std::size_t end) {
    while (end > begin && tokens_[end - 1].lexeme == ";") --end;
    if (begin >= end || end > tokens_.size()) return kInvalidExprId;

    const std::size_t checkpoint = expressions_.size();
    ExpressionReader reader(tokens_, expressions_, begin, end);
    const ExprId root = reader.parse();
    if (root == kInvalidExprId || reader.position() != end) {
        expressions_.resize(checkpoint);
        return kInvalidExprId;
    }
    return root;
}

void Parser::attachExpressionRoots(AstStatement &statement) {
    const std::size_t begin = statement.tokenBegin;
    const std::size_t end = statement.tokenEnd;
    auto attach = [&](std::size_t expressionBegin, std::size_t expressionEnd) {
        const ExprId root = tryParseExpression(expressionBegin, expressionEnd);
        if (root != kInvalidExprId) statement.expressionRoots.push_back(root);
    };

    if (statement.kind == AstStatementKind::Expression) {
        attach(begin, end);
        return;
    }
    if (statement.kind == AstStatementKind::Print ||
        statement.kind == AstStatementKind::Return ||
        statement.kind == AstStatementKind::Throw) {
        attach(begin + 1, end);
        return;
    }
    if (statement.kind != AstStatementKind::Conditional &&
        statement.kind != AstStatementKind::Loop &&
        statement.kind != AstStatementKind::Switch) {
        return;
    }

    std::size_t open = begin + 1;
    while (open < end && tokens_[open].lexeme != "(") ++open;
    if (open >= end) return;
    const std::size_t close = matchingDelimiter(tokens_, open, end, "(", ")");
    if (close >= end) return;

    if (statement.kind != AstStatementKind::Loop) {
        attach(open + 1, close);
        return;
    }

    // A for-style loop exposes init, condition and update as separate roots.
    std::size_t partBegin = open + 1;
    int parenDepth = 0;
    int bracketDepth = 0;
    int braceDepth = 0;
    for (std::size_t cursor = partBegin; cursor < close; ++cursor) {
        const std::string &token = tokens_[cursor].lexeme;
        if (token == "(") ++parenDepth;
        else if (token == ")") --parenDepth;
        else if (token == "[") ++bracketDepth;
        else if (token == "]") --bracketDepth;
        else if (token == "{") ++braceDepth;
        else if (token == "}") --braceDepth;
        else if (token == ";" && parenDepth == 0 && bracketDepth == 0 &&
                 braceDepth == 0) {
            attach(partBegin, cursor);
            partBegin = cursor + 1;
        }
    }
    attach(partBegin, close);
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
