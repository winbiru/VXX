#include "vpp/frontend/parser.h"

#include <functional>
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

bool hasViSuffix(const std::string &target) {
    return target.size() > 3 &&
           target.compare(target.size() - 3, 3, ".vi") == 0;
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
    using LambdaBodyReader =
        std::function<bool(std::size_t, std::size_t, AstStatement &)>;

    ExpressionReader(const std::vector<Token> &tokens,
                     std::vector<AstExpression> &expressions,
                     std::vector<AstLambda> &lambdas,
                     std::size_t begin,
                     std::size_t end,
                     LambdaBodyReader readLambdaBody)
        : tokens_(tokens), expressions_(expressions), lambdas_(lambdas),
          pos_(begin), end_(end), readLambdaBody_(std::move(readLambdaBody)) {}

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

            if (tokens_[pos_].lexeme == "[") {
                const std::size_t begin = expressions_[left].tokenBegin;
                ++pos_;
                ExprId index = parseExpression(0);
                if (index == kInvalidExprId || pos_ >= end_ || tokens_[pos_].lexeme != "]") {
                    return kInvalidExprId;
                }
                ++pos_;
                AstExpression indexed;
                indexed.kind = AstExpressionKind::Index;
                indexed.tokenBegin = begin;
                indexed.tokenEnd = pos_;
                indexed.left = left;
                indexed.right = index;
                left = append(std::move(indexed));
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
        if (lexeme == "[") return parseListLiteral();
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
                expressions_[call].explicitCall = true;
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
        const std::size_t expressionCheckpoint = expressions_.size();
        const std::size_t lambdaCheckpoint = lambdas_.size();
        auto rollback = [&]() {
            pos_ = begin;
            expressions_.resize(expressionCheckpoint);
            lambdas_.resize(lambdaCheckpoint);
            return kInvalidExprId;
        };

        ++pos_;
        if (pos_ >= end_ || tokens_[pos_].lexeme != "(") return rollback();
        ++pos_;

        std::vector<AstParameter> parameters;
        auto addParameter = [&](std::size_t parameterBegin,
                                std::size_t parameterEnd) {
            if (parameterBegin >= parameterEnd) return false;

            std::size_t equals = parameterEnd;
            int nestedParenDepth = 0;
            int nestedBracketDepth = 0;
            int nestedBraceDepth = 0;
            for (std::size_t cursor = parameterBegin;
                 cursor < parameterEnd; ++cursor) {
                const std::string &token = tokens_[cursor].lexeme;
                if (token == "(") ++nestedParenDepth;
                else if (token == ")") --nestedParenDepth;
                else if (token == "[") ++nestedBracketDepth;
                else if (token == "]") --nestedBracketDepth;
                else if (token == "{") ++nestedBraceDepth;
                else if (token == "}") --nestedBraceDepth;
                else if (token == "=" && nestedParenDepth == 0 &&
                         nestedBracketDepth == 0 && nestedBraceDepth == 0) {
                    equals = cursor;
                    break;
                }
            }
            if (equals == parameterBegin) return false;

            AstParameter parameter;
            parameter.name = joinTokenText(tokens_, parameterBegin, equals);
            parameter.span = tokenRangeSpan(tokens_, parameterBegin, parameterEnd);
            parameter.hasDefault = equals < parameterEnd;
            if (parameter.name.empty()) return false;
            if (parameter.hasDefault) {
                if (equals + 1 >= parameterEnd) return false;
                const std::size_t expressionSave = expressions_.size();
                const std::size_t lambdaSave = lambdas_.size();
                ExpressionReader defaultReader(
                    tokens_, expressions_, lambdas_, equals + 1, parameterEnd,
                    readLambdaBody_);
                parameter.defaultValue = defaultReader.parse();
                if (parameter.defaultValue == kInvalidExprId ||
                    defaultReader.position() != parameterEnd) {
                    expressions_.resize(expressionSave);
                    lambdas_.resize(lambdaSave);
                    return false;
                }
            }
            parameters.push_back(std::move(parameter));
            return true;
        };

        std::size_t parameterBegin = pos_;
        int parenDepth = 0;
        int bracketDepth = 0;
        int braceDepth = 0;
        while (pos_ < end_) {
            const std::string &token = tokens_[pos_].lexeme;
            if (token == "(" ) ++parenDepth;
            else if (token == ")") {
                if (parenDepth == 0 && bracketDepth == 0 && braceDepth == 0) {
                    if (parameterBegin < pos_ &&
                        !addParameter(parameterBegin, pos_)) return rollback();
                    break;
                }
                --parenDepth;
            } else if (token == "[") ++bracketDepth;
            else if (token == "]") --bracketDepth;
            else if (token == "{") ++braceDepth;
            else if (token == "}") --braceDepth;
            else if (token == "," && parenDepth == 0 && bracketDepth == 0 && braceDepth == 0) {
                if (parameterBegin >= pos_ ||
                    !addParameter(parameterBegin, pos_)) return rollback();
                parameterBegin = pos_ + 1;
            }
            ++pos_;
        }
        if (pos_ >= end_ || tokens_[pos_].lexeme != ")") return rollback();
        ++pos_;
        if (pos_ >= end_ || tokens_[pos_].lexeme != "{") return rollback();

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
        if (depth != 0) return rollback();

        AstStatement body;
        if (!readLambdaBody_ || !readLambdaBody_(bodyBegin, pos_, body) ||
            body.kind != AstStatementKind::Block || body.tokenBegin != bodyBegin ||
            body.tokenEnd != pos_) {
            return rollback();
        }

        AstLambda payload;
        payload.id = lambdas_.size();
        payload.span = tokenRangeSpan(tokens_, begin, pos_);
        payload.parameters = std::move(parameters);
        payload.body = std::move(body);
        const LambdaId lambdaId = payload.id;
        lambdas_.push_back(std::move(payload));

        AstExpression lambda;
        lambda.kind = AstExpressionKind::Lambda;
        lambda.tokenBegin = begin;
        lambda.tokenEnd = pos_;
        lambda.lambdaId = lambdaId;
        const ExprId expressionId = append(std::move(lambda));
        lambdas_[lambdaId].expression = expressionId;
        return expressionId;
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

    ExprId parseListLiteral() {
        const std::size_t begin = pos_;
        ++pos_;
        std::vector<ExprId> elements;
        while (pos_ < end_ && tokens_[pos_].lexeme != "]") {
            ExprId element = parseExpression(0);
            if (element == kInvalidExprId) return kInvalidExprId;
            elements.push_back(element);
            if (pos_ >= end_ || tokens_[pos_].lexeme != ",") break;
            ++pos_;
            if (pos_ >= end_ || tokens_[pos_].lexeme == "]") return kInvalidExprId;
        }
        if (pos_ >= end_ || tokens_[pos_].lexeme != "]") return kInvalidExprId;
        ++pos_;
        AstExpression list;
        list.kind = AstExpressionKind::ListLiteral;
        list.tokenBegin = begin;
        list.tokenEnd = pos_;
        list.listElements = std::move(elements);
        return append(std::move(list));
    }

    const std::vector<Token> &tokens_;
    std::vector<AstExpression> &expressions_;
    std::vector<AstLambda> &lambdas_;
    std::size_t pos_;
    std::size_t end_;
    LambdaBodyReader readLambdaBody_;
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
    program.lambdas = std::move(lambdas_);
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

bool Parser::tryParseStructuredSwitch(AstStatement &statement) {
    const std::size_t savedPosition = pos_;
    const std::size_t expressionCheckpoint = expressions_.size();
    const std::size_t lambdaCheckpoint = lambdas_.size();
    auto rollback = [&]() {
        pos_ = savedPosition;
        expressions_.resize(expressionCheckpoint);
        lambdas_.resize(lambdaCheckpoint);
    };

    if (pos_ >= tokens_.size() || tokens_[pos_].lexeme != "chọn") return false;
    const std::size_t begin = pos_++;
    if (pos_ >= tokens_.size() || tokens_[pos_].lexeme != "(") {
        rollback();
        return false;
    }

    const std::size_t selectorOpen = pos_;
    const std::size_t selectorClose = matchingDelimiter(
        tokens_, selectorOpen, tokens_.size(), "(", ")");
    if (selectorClose >= tokens_.size()) {
        rollback();
        return false;
    }
    const ExprId selector = tryParseExpression(selectorOpen + 1, selectorClose);
    if (selector == kInvalidExprId || selectorClose + 1 >= tokens_.size() ||
        tokens_[selectorClose + 1].lexeme != "{") {
        rollback();
        return false;
    }

    AstStatement parsed;
    parsed.kind = AstStatementKind::Switch;
    parsed.tokenBegin = begin;
    parsed.switchForm = AstSwitchForm::Structured;
    parsed.expressionRoots.push_back(selector);
    pos_ = selectorClose + 2;

    try {
        while (pos_ < tokens_.size() && tokens_[pos_].lexeme != "}") {
            if (tokens_[pos_].lexeme == ";") {
                ++pos_;
                continue;
            }

            const std::size_t armBegin = pos_;
            AstSwitchArm arm;
            if (tokens_[pos_].lexeme == "ca") {
                arm.prefixedByCase = true;
                ++pos_;
                if (pos_ >= tokens_.size() || tokens_[pos_].lexeme == "}") {
                    rollback();
                    return false;
                }
            }

            if (pos_ < tokens_.size() && tokens_[pos_].lexeme == "mặc định") {
                arm.kind = AstSwitchArmKind::Default;
                arm.labelSpan = tokens_[pos_].span;
                ++pos_;
            } else {
                if (!arm.prefixedByCase || pos_ >= tokens_.size()) {
                    rollback();
                    return false;
                }
                const std::size_t labelBegin = pos_;
                const ExprId label = tryParseExpression(labelBegin, labelBegin + 1);
                if (label == kInvalidExprId) {
                    rollback();
                    return false;
                }
                arm.kind = AstSwitchArmKind::Case;
                arm.label = label;
                arm.labelSpan = tokens_[labelBegin].span;
                parsed.expressionRoots.push_back(label);
                ++pos_;
            }

            if (pos_ < tokens_.size() && tokens_[pos_].lexeme == ":") {
                arm.hasColon = true;
                ++pos_;
            }
            if (pos_ >= tokens_.size() || tokens_[pos_].lexeme != "{") {
                rollback();
                return false;
            }

            arm.bodyChildIndex = parsed.children.size();
            parsed.children.push_back(parseBlock());
            arm.span = spanFor(armBegin, parsed.children.back().tokenEnd);
            parsed.switchArms.push_back(std::move(arm));
        }
    } catch (const ParseError &) {
        rollback();
        return false;
    }

    if (pos_ >= tokens_.size() || tokens_[pos_].lexeme != "}") {
        rollback();
        return false;
    }
    ++pos_;
    parsed.tokenEnd = pos_;
    parsed.span = spanFor(begin, pos_);
    statement = std::move(parsed);
    return true;
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
    if (kind == AstStatementKind::Switch) {
        AstStatement structuredSwitch;
        if (tryParseStructuredSwitch(structuredSwitch)) {
            return structuredSwitch;
        }
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
    attachImportForm(statement);
    attachExpressionRoots(statement);
    attachClassForm(statement);
    attachConditionalForm(statement);
    attachLoopForm(statement);
    attachTryForm(statement);
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

void Parser::attachImportForm(AstStatement &statement) {
    if (statement.kind != AstStatementKind::Import) return;

    const std::size_t begin = statement.tokenBegin;
    const std::size_t end = statement.tokenEnd;
    if (begin + 2 >= end || end > tokens_.size() ||
        tokens_[begin].lexeme != "nhập" || tokens_[end - 1].lexeme != ";") {
        return;
    }

    AstImportSpec spec;
    spec.hasSemicolon = true;
    std::size_t cursor = begin + 1;
    if (tokens_[cursor].kind == TokenKind::String) {
        const std::string &spelling = tokens_[cursor].lexeme;
        if (spelling.size() < 2 ||
            !((spelling.front() == '"' && spelling.back() == '"') ||
              (spelling.front() == '\'' && spelling.back() == '\''))) {
            return;
        }
        spec.quoted = true;
        spec.target = spelling.substr(1, spelling.size() - 2);
        spec.targetSpan = tokens_[cursor].span;
        ++cursor;
    } else {
        const std::size_t targetBegin = cursor;
        std::size_t targetEnd = cursor;
        while (cursor < end - 1 && tokens_[cursor].lexeme != "như") {
            const std::string &piece = tokens_[cursor].lexeme;
            const bool separator = piece == "/" || piece == "\\";
            if (separator) {
                spec.target += piece;
            } else {
                if (!spec.target.empty() && spec.target.back() != '/' &&
                    spec.target.back() != '\\') {
                    spec.target.push_back(' ');
                }
                spec.target += piece;
            }
            targetEnd = ++cursor;
        }
        if (targetBegin == targetEnd) return;
        spec.targetSpan = tokenRangeSpan(tokens_, targetBegin, targetEnd);
    }

    const bool hasSeparator = spec.target.find('/') != std::string::npos ||
                              spec.target.find('\\') != std::string::npos;
    const std::size_t lastSeparator = spec.target.find_last_of("/\\");
    const std::size_t lastDot = spec.target.find_last_of('.');
    const bool hasExtension = lastDot != std::string::npos &&
                              (lastSeparator == std::string::npos ||
                               lastDot > lastSeparator);
    const bool barePackageTarget = !hasSeparator && !hasExtension;
    if (!hasViSuffix(spec.target) && !barePackageTarget) return;

    if (cursor < end - 1 && tokens_[cursor].lexeme == "như") {
        ++cursor;
        if (cursor >= end - 1 || tokens_[cursor].kind != TokenKind::Identifier) {
            return;
        }
        spec.alias = tokens_[cursor].lexeme;
        spec.aliasSpan = tokens_[cursor].span;
        ++cursor;
    }
    if (cursor != end - 1) return;

    statement.importForm = AstImportForm::LocalSourceFile;
    statement.importSpec = std::move(spec);
}

ExprId Parser::tryParseExpression(std::size_t begin, std::size_t end) {
    while (end > begin && tokens_[end - 1].lexeme == ";") --end;
    if (begin >= end || end > tokens_.size()) return kInvalidExprId;

    const std::size_t checkpoint = expressions_.size();
    const std::size_t lambdaCheckpoint = lambdas_.size();
    ExpressionReader reader(
        tokens_, expressions_, lambdas_, begin, end,
        [this](std::size_t bodyBegin, std::size_t bodyEnd, AstStatement &body) {
            return tryParseLambdaBody(bodyBegin, bodyEnd, body);
        });
    const ExprId root = reader.parse();
    if (root == kInvalidExprId || reader.position() != end) {
        expressions_.resize(checkpoint);
        lambdas_.resize(lambdaCheckpoint);
        return kInvalidExprId;
    }
    return root;
}

bool Parser::tryParseLambdaBody(std::size_t begin,
                                std::size_t end,
                                AstStatement &body) {
    if (begin >= end || end > tokens_.size() || tokens_[begin].lexeme != "{") {
        return false;
    }

    const std::size_t savedPosition = pos_;
    const std::size_t expressionCheckpoint = expressions_.size();
    const std::size_t lambdaCheckpoint = lambdas_.size();
    pos_ = begin;
    try {
        AstStatement parsed = parseBlock();
        if (pos_ != end) {
            pos_ = savedPosition;
            expressions_.resize(expressionCheckpoint);
            lambdas_.resize(lambdaCheckpoint);
            return false;
        }
        body = std::move(parsed);
        pos_ = savedPosition;
        return true;
    } catch (const ParseError &) {
        pos_ = savedPosition;
        expressions_.resize(expressionCheckpoint);
        lambdas_.resize(lambdaCheckpoint);
        return false;
    }
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

void Parser::attachClassForm(AstStatement &statement) {
    if (statement.kind != AstStatementKind::Class ||
        !statement.expressionRoots.empty() || statement.children.size() != 1) {
        return;
    }

    const std::size_t begin = statement.tokenBegin;
    const std::size_t end = statement.tokenEnd;
    if (begin >= end || tokens_[begin].lexeme != "lớp") return;

    std::size_t cursor = begin + 1;
    if (cursor < end && isVisibility(tokens_[cursor].lexeme)) ++cursor;
    if (cursor >= end || tokens_[cursor].kind != TokenKind::Identifier ||
        tokens_[cursor].lexeme.empty()) {
        return;
    }
    ++cursor;

    const AstStatement &body = statement.children.front();
    if (cursor >= end || tokens_[cursor].lexeme != "{" ||
        body.kind != AstStatementKind::Block || body.tokenBegin != cursor ||
        body.tokenEnd != end) {
        return;
    }
    for (const AstStatement &member : body.children) {
        if (member.kind != AstStatementKind::Function &&
            member.kind != AstStatementKind::Empty) {
            return;
        }
    }

    statement.classForm = AstClassForm::MethodBlock;
}

void Parser::attachConditionalForm(AstStatement &statement) {
    if (statement.kind != AstStatementKind::Conditional ||
        statement.expressionRoots.size() != 1) {
        return;
    }

    const std::size_t begin = statement.tokenBegin;
    const std::size_t end = statement.tokenEnd;
    if (begin + 1 >= end || tokens_[begin].lexeme != "nếu" ||
        tokens_[begin + 1].lexeme != "(") {
        return;
    }
    const std::size_t conditionClose = matchingDelimiter(
        tokens_, begin + 1, end, "(", ")");
    if (conditionClose >= end || conditionClose + 1 >= end ||
        tokens_[conditionClose + 1].lexeme != "{" ||
        statement.children.empty()) {
        return;
    }

    const AstStatement &thenBlock = statement.children.front();
    if (thenBlock.kind != AstStatementKind::Block ||
        thenBlock.tokenBegin != conditionClose + 1) {
        return;
    }
    if (thenBlock.tokenEnd == end && statement.children.size() == 1) {
        statement.conditionalForm = AstConditionalForm::IfBlock;
        return;
    }

    if (statement.children.size() != 2 || thenBlock.tokenEnd >= end) return;
    const std::size_t continuation = thenBlock.tokenEnd;
    if (tokens_[continuation].lexeme != "hoặc" &&
        tokens_[continuation].lexeme != "nếu không") {
        return;
    }
    const AstStatement &elseBlock = statement.children[1];
    if (elseBlock.kind != AstStatementKind::Block ||
        elseBlock.tokenBegin != continuation + 1 || elseBlock.tokenEnd != end) {
        return;
    }
    statement.conditionalForm = AstConditionalForm::IfElseBlocks;
}

void Parser::attachLoopForm(AstStatement &statement) {
    if (statement.kind != AstStatementKind::Loop ||
        statement.expressionRoots.size() != 3 ||
        statement.children.size() != 1) {
        return;
    }
    const std::size_t begin = statement.tokenBegin;
    const std::size_t end = statement.tokenEnd;
    if (begin + 1 >= end || tokens_[begin].lexeme != "lặp" ||
        tokens_[begin + 1].lexeme != "(") {
        return;
    }
    const std::size_t close = matchingDelimiter(
        tokens_, begin + 1, end, "(", ")");
    if (close >= end || close + 1 >= end ||
        tokens_[close + 1].lexeme != "{") {
        return;
    }
    const AstStatement &body = statement.children.front();
    if (body.kind != AstStatementKind::Block ||
        body.tokenBegin != close + 1 || body.tokenEnd != end) {
        return;
    }

    std::size_t separators = 0;
    int parenDepth = 0;
    int bracketDepth = 0;
    int braceDepth = 0;
    for (std::size_t cursor = begin + 2; cursor < close; ++cursor) {
        const std::string &token = tokens_[cursor].lexeme;
        if (token == "(") ++parenDepth;
        else if (token == ")") --parenDepth;
        else if (token == "[") ++bracketDepth;
        else if (token == "]") --bracketDepth;
        else if (token == "{") ++braceDepth;
        else if (token == "}") --braceDepth;
        else if (token == ";" && parenDepth == 0 && bracketDepth == 0 &&
                 braceDepth == 0) {
            ++separators;
        }
    }
    if (separators == 2) statement.loopForm = AstLoopForm::ForBlock;
}

void Parser::attachTryForm(AstStatement &statement) {
    if (statement.kind != AstStatementKind::Try ||
        statement.children.size() != 2 ||
        !statement.expressionRoots.empty()) {
        return;
    }

    const AstStatement &tryBody = statement.children[0];
    const AstStatement &catchBody = statement.children[1];
    if (tryBody.kind != AstStatementKind::Block ||
        catchBody.kind != AstStatementKind::Block ||
        tryBody.tokenBegin != statement.tokenBegin + 1 ||
        tryBody.tokenEnd >= statement.tokenEnd ||
        catchBody.tokenEnd != statement.tokenEnd ||
        tokens_[tryBody.tokenEnd].lexeme != "bắt lỗi") {
        return;
    }

    std::size_t cursor = tryBody.tokenEnd + 1;
    if (cursor < catchBody.tokenBegin && tokens_[cursor].lexeme == "(") {
        const std::size_t close = matchingDelimiter(
            tokens_, cursor, catchBody.tokenBegin, "(", ")");
        if (close >= catchBody.tokenBegin) return;
        // Omitting parentheses is the no-binding form. Once a binding list is
        // opened, the structured grammar requires exactly one catch name.
        if (close != cursor + 2 || !isNamePiece(tokens_[cursor + 1].lexeme)) {
            return;
        }
        statement.catchVariable = tokens_[cursor + 1].lexeme;
        statement.catchVariableSpan = tokens_[cursor + 1].span;
        cursor = close + 1;
    }

    if (cursor != catchBody.tokenBegin || tokens_[cursor].lexeme != "{") return;
    statement.tryForm = AstTryForm::TryCatchBlocks;
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
