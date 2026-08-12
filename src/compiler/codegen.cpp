#include "vpp/compiler/codegen.h"

#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "common/storeString.h"
#include "frontend/lexer.h"
#include "vpp/core/message_constants.h"

namespace vietvm::compiler {
namespace {

bool isBinaryOperator(const std::string &op) noexcept {
    return op == "+" || op == "-" || op == "*" || op == "/" || op == "%" ||
           op == "==" || op == "!=" || op == "<" || op == ">" ||
           op == "<=" || op == ">=" || op == "&&" || op == "||";
}

bool isCompoundAssignment(const std::string &op) noexcept {
    return op == "+=" || op == "-=" || op == "*=" || op == "/=" || op == "%=";
}

bool isExactSourceToken(const IrInstruction &instruction,
                        const IrValue &value) noexcept {
    for (const vietvm::frontend::Token &token : instruction.tokens) {
        if (token.span.begin.offset == value.span.begin.offset &&
            token.span.end.offset == value.span.end.offset &&
            token.lexeme == value.text) {
            return true;
        }
    }
    return false;
}

bool isExactSourceName(const IrInstruction &instruction,
                       const IrValue &value) {
    std::string sourceName;
    bool sawToken = false;
    std::size_t firstOffset = 0;
    std::size_t lastOffset = 0;
    for (const vietvm::frontend::Token &token : instruction.tokens) {
        if (token.span.end.offset <= value.span.begin.offset ||
            token.span.begin.offset >= value.span.end.offset) {
            continue;
        }
        if (token.span.begin.offset < value.span.begin.offset ||
            token.span.end.offset > value.span.end.offset) {
            return false;
        }
        if (!sawToken) {
            firstOffset = token.span.begin.offset;
            sawToken = true;
        } else {
            sourceName.push_back(' ');
        }
        sourceName += token.lexeme;
        lastOffset = token.span.end.offset;
    }
    return sawToken && firstOffset == value.span.begin.offset &&
           lastOffset == value.span.end.offset && sourceName == value.text;
}

bool endsWithSourceToken(const IrInstruction &instruction,
                         const IrValue &value,
                         const std::string &lexeme) noexcept {
    for (const vietvm::frontend::Token &token : instruction.tokens) {
        if (token.span.end.offset == value.span.end.offset) {
            return token.lexeme == lexeme;
        }
    }
    return false;
}

bool hasLegacyMapBounds(const IrInstruction &instruction,
                        const IrValue &value) noexcept {
    bool startsWithBrace = false;
    bool endsWithBrace = false;
    for (const vietvm::frontend::Token &token : instruction.tokens) {
        if (token.span.begin.offset == value.span.begin.offset) {
            startsWithBrace = token.lexeme == "{";
        }
        if (token.span.end.offset == value.span.end.offset) {
            endsWithBrace = token.lexeme == "}";
        }
    }
    return startsWithBrace && endsWithBrace;
}

bool isMapKey(const IrInstruction &instruction,
              const IrValue *value) noexcept {
    if (value == nullptr || !value->operands.empty()) return false;
    if (!isExactSourceToken(instruction, *value)) return false;
    if (value->opcode == IrValueOpcode::ConstString) {
        return isStringLiteral(value->text);
    }
    if (value->opcode == IrValueOpcode::LoadName) {
        // The legacy map parser accepts one identifier token here. In
        // particular, a contextual multi-word AST name is not a map key.
        return isVariable(value->text);
    }
    return false;
}

bool isMapValue(const IrInstruction &instruction,
                const IrValue *value) noexcept {
    if (value == nullptr || !value->operands.empty()) return false;
    if (!isExactSourceToken(instruction, *value)) return false;
    switch (value->opcode) {
        case IrValueOpcode::ConstInt: return isNumber(value->text);
        case IrValueOpcode::ConstFloat: return isFloat(value->text);
        case IrValueOpcode::ConstString: return isStringLiteral(value->text);
        case IrValueOpcode::ConstBool:
            return value->text == "đúng" || value->text == "sai";
        case IrValueOpcode::ConstNull: return value->text == "rỗng";
        default: return false;
    }
}

enum class ValueContext {
    Nested,
    StatementRoot,
    SimpleAssignmentRhs,
};

bool supportsValue(const IrProgram &program,
                   const IrInstruction &instruction,
                   IrValueId id,
                   ValueContext context,
                   std::unordered_set<IrValueId> &visiting) {
    const IrValue *value = program.value(id);
    if (value == nullptr || !visiting.insert(id).second) return false;

    bool supported = false;
    switch (value->opcode) {
        case IrValueOpcode::ConstInt:
        case IrValueOpcode::ConstFloat:
        case IrValueOpcode::ConstString:
        case IrValueOpcode::ConstBool:
        case IrValueOpcode::ConstNull:
        case IrValueOpcode::LoadName:
            supported = value->operands.empty();
            break;

        case IrValueOpcode::MapLiteral:
            // Legacy compileExpr recognizes a map only when it occupies the
            // complete expression slice or the complete RHS of a simple '='.
            // It does not parse a map nested under unary/binary/compound ops.
            supported = context != ValueContext::Nested &&
                        hasLegacyMapBounds(instruction, *value) &&
                        value->operands.size() % 2 == 0;
            for (std::size_t index = 0; supported && index < value->operands.size(); index += 2) {
                supported = isMapKey(
                                instruction,
                                program.value(value->operands[index])) &&
                            isMapValue(
                                instruction,
                                program.value(value->operands[index + 1]));
            }
            break;

        case IrValueOpcode::Unary:
            supported = value->operands.size() == 1 &&
                        value->text == "!" &&
                        supportsValue(program, instruction, value->operands.front(),
                                      ValueContext::Nested, visiting);
            break;

        case IrValueOpcode::Binary:
            supported = value->operands.size() == 2 && isBinaryOperator(value->text) &&
                        supportsValue(program, instruction, value->operands[0],
                                      ValueContext::Nested, visiting) &&
                        supportsValue(program, instruction, value->operands[1],
                                      ValueContext::Nested, visiting);
            break;

        case IrValueOpcode::StoreName: {
            if (context != ValueContext::StatementRoot || value->operands.empty()) break;
            const IrValue *target = program.value(value->operands.front());
            if (target == nullptr || target->opcode != IrValueOpcode::LoadName ||
                target->text.empty() || !isExactSourceName(instruction, *target) ||
                value->span.begin.offset != target->span.begin.offset) {
                break;
            }
            if (value->text == "++" || value->text == "--") {
                supported = value->operands.size() == 1 &&
                            endsWithSourceToken(instruction, *value, value->text);
                break;
            }
            supported = value->operands.size() == 2 &&
                        (value->text == "=" || isCompoundAssignment(value->text)) &&
                        supportsValue(
                            program, instruction, value->operands[1],
                            value->text == "=" ? ValueContext::SimpleAssignmentRhs
                                               : ValueContext::Nested,
                            visiting);
            break;
        }

        case IrValueOpcode::LegacyRegion:
        case IrValueOpcode::Call:
        case IrValueOpcode::CallDynamic:
            supported = false;
            break;
    }

    visiting.erase(id);
    return supported;
}

bool supportsInstruction(const IrProgram &program,
                         const IrInstruction &instruction) {
    if (instruction.legacyRegion || !instruction.children.empty()) return false;

    if (instruction.opcode == IrOpcode::NoOp) {
        return instruction.expressionRoots.empty();
    }
    if (instruction.opcode != IrOpcode::Print &&
        instruction.opcode != IrOpcode::Statement) {
        return false;
    }
    if (instruction.expressionRoots.size() != 1) return false;

    std::unordered_set<IrValueId> visiting;
    const bool assignmentAllowed = instruction.opcode == IrOpcode::Statement;
    const IrValue *root = program.value(instruction.expressionRoots.front());
    if (root == nullptr) return false;
    if (!assignmentAllowed && root->opcode == IrValueOpcode::StoreName) return false;
    return supportsValue(
        program, instruction, root->id, ValueContext::StatementRoot, visiting);
}

Opcode binaryOpcode(const std::string &op) {
    if (op == "+") return OP_CONG;
    if (op == "-") return OP_TRU;
    if (op == "*") return OP_NHAN;
    if (op == "/") return OP_CHIA;
    if (op == "%") return OP_MODULO;
    if (op == "==") return OP_SO_SANH_BANG;
    if (op == "!=") return OP_KHAC_BANG;
    if (op == "<") return OP_NHO_HON;
    if (op == ">") return OP_LON_HON;
    if (op == "<=") return OP_NHO_HON_HOAC_BANG;
    if (op == ">=") return OP_LON_HON_HOAC_BANG;
    if (op == "&&") return OP_Logic_VA;
    if (op == "||") return OP_Logic_HOAC;
    throw std::logic_error("unsupported direct IR binary operator");
}

Opcode compoundOpcode(const std::string &op) {
    if (op == "+=") return OP_CONG;
    if (op == "-=") return OP_TRU;
    if (op == "*=") return OP_NHAN;
    if (op == "/=") return OP_CHIA;
    if (op == "%=") return OP_MODULO;
    throw std::logic_error("unsupported direct IR compound assignment");
}

std::string unquote(const std::string &text) {
    if (text.size() >= 2 &&
        ((text.front() == '"' && text.back() == '"') ||
         (text.front() == '\'' && text.back() == '\''))) {
        return text.substr(1, text.size() - 2);
    }
    return text;
}

std::string encodeMapField(const std::string &text) {
    std::string encoded;
    encoded.reserve(text.size() + 8);
    for (unsigned char byte : text) {
        if (byte == '\\' || byte == '\n' || byte == '\r' || byte == '\t' ||
            byte == '\x1e' || byte == '\x1f') {
            encoded.push_back('\\');
            if (byte == '\n') encoded.push_back('n');
            else if (byte == '\r') encoded.push_back('r');
            else if (byte == '\t') encoded.push_back('t');
            else if (byte == '\x1e') encoded.push_back('e');
            else if (byte == '\x1f') encoded.push_back('f');
            else encoded.push_back('\\');
        } else {
            encoded.push_back(static_cast<char>(byte));
        }
    }
    return encoded;
}

std::string encodeMapLiteral(const IrProgram &program, const IrValue &map) {
    constexpr char recordSeparator = '\x1e';
    constexpr char fieldSeparator = '\x1f';
    std::ostringstream encoded;
    for (std::size_t index = 0; index < map.operands.size(); index += 2) {
        const IrValue &key = *program.value(map.operands[index]);
        const IrValue &value = *program.value(map.operands[index + 1]);
        if (index != 0) encoded << recordSeparator;
        encoded << encodeMapField(key.opcode == IrValueOpcode::ConstString
                                      ? stripQuotes(key.text)
                                      : key.text)
                << fieldSeparator;
        switch (value.opcode) {
            case IrValueOpcode::ConstInt:
            case IrValueOpcode::ConstBool:
                encoded << 'i' << fieldSeparator;
                if (value.opcode == IrValueOpcode::ConstBool) {
                    encoded << (value.text == "đúng" ? '1' : '0');
                } else {
                    encoded << encodeMapField(value.text);
                }
                break;
            case IrValueOpcode::ConstFloat:
                encoded << 'd' << fieldSeparator << encodeMapField(value.text);
                break;
            case IrValueOpcode::ConstString:
                // Unlike ordinary string expressions, the legacy map encoder
                // decodes source escapes before applying its RS/FS escaping.
                encoded << 's' << fieldSeparator
                        << encodeMapField(stripQuotes(value.text));
                break;
            case IrValueOpcode::ConstNull:
                encoded << 'n' << fieldSeparator;
                break;
            default:
                throw std::logic_error("unsupported direct IR map value");
        }
    }
    return encoded.str();
}

struct Emitter {
    const IrProgram &program;
    std::vector<Instruction> bytecode;
    std::unordered_map<std::string, int> slots;
    int nextSlot = 0;

    int slotFor(const std::string &name) {
        const auto found = slots.find(name);
        if (found != slots.end()) return found->second;
        const int slot = nextSlot++;
        slots.emplace(name, slot);
        return slot;
    }

    void emitValue(IrValueId id) {
        const IrValue *value = program.value(id);
        if (value == nullptr) throw std::logic_error("invalid direct IR value id");

        switch (value->opcode) {
            case IrValueOpcode::ConstInt:
                try {
                    bytecode.push_back({OP_BIEN_SO, std::stoi(value->text), 0, 0});
                } catch (...) {
                    throw std::runtime_error(vietvm::messages::formatMessage(
                        vietvm::messages::kInternalNumberParseMismatch, {value->text}));
                }
                return;
            case IrValueOpcode::ConstFloat: {
                const int index = StringPool::storeString(value->text);
                bytecode.push_back({OP_BIEN_SO_FLOAT, 0, index, 0});
                return;
            }
            case IrValueOpcode::ConstString: {
                const int index = StringPool::storeString(unquote(value->text));
                bytecode.push_back({OP_CHUOI, 0, index, 0});
                return;
            }
            case IrValueOpcode::ConstBool:
                bytecode.push_back({OP_BIEN_SO, value->text == "đúng" ? 1 : 0, 0, 0});
                return;
            case IrValueOpcode::ConstNull:
                bytecode.push_back({OP_RONG_GIA_TRI, 0, 0, 0});
                return;
            case IrValueOpcode::MapLiteral: {
                const int index = StringPool::storeString(encodeMapLiteral(program, *value));
                bytecode.push_back({OP_MAP_LITERAL, 0, index, 0});
                return;
            }
            case IrValueOpcode::LoadName: {
                const int slot = slotFor(value->text);
                bytecode.push_back({OP_TEN_BIEN_GIA_TRI, 0, slot, 0});
                return;
            }
            case IrValueOpcode::Unary:
                emitValue(value->operands.front());
                bytecode.push_back({OP_PHU_DINH, 0, 0, 0});
                return;
            case IrValueOpcode::Binary:
                emitValue(value->operands[0]);
                emitValue(value->operands[1]);
                bytecode.push_back({binaryOpcode(value->text), 0, 0, 0});
                return;
            case IrValueOpcode::StoreName: {
                const IrValue *target = program.value(value->operands.front());
                if (target == nullptr) throw std::logic_error("invalid direct IR store target");

                // The legacy compiler assigns the target slot before compiling
                // the RHS.  Preserve that ordering even though the target ID is
                // pushed after the value.
                const int slot = slotFor(target->text);
                if (value->text == "++" || value->text == "--") {
                    bytecode.push_back({OP_TEN_BIEN_ID, 0, slot, 0});
                    bytecode.push_back({value->text == "++" ? OP_CONG_MOT : OP_TRU_MOT,
                                        0, 0, 0});
                    return;
                }
                if (isCompoundAssignment(value->text)) {
                    bytecode.push_back({OP_TEN_BIEN_GIA_TRI, 0, slot, 0});
                    emitValue(value->operands[1]);
                    bytecode.push_back({compoundOpcode(value->text), 0, 0, 0});
                } else {
                    emitValue(value->operands[1]);
                }
                bytecode.push_back({OP_TEN_BIEN_ID, 0, slot, 0});
                bytecode.push_back({OP_GAN, 0, 0, 0});
                return;
            }
            case IrValueOpcode::LegacyRegion:
            case IrValueOpcode::Call:
            case IrValueOpcode::CallDynamic:
                throw std::logic_error("unsupported value reached direct IR emitter");
        }
    }

    void emitInstruction(const IrInstruction &instruction) {
        if (instruction.opcode == IrOpcode::NoOp) return;
        emitValue(instruction.expressionRoots.front());
        if (instruction.opcode == IrOpcode::Print) {
            bytecode.push_back({OP_IN, 0, 0, 0});
        }
    }
};

} // namespace

DirectIrSupport analyzeDirectIrSupport(const IrProgram &program) {
    DirectIrSupport support{true, 0};
    for (const IrInstruction &instruction : program.instructions) {
        if (!supportsInstruction(program, instruction)) {
            support.supported = false;
            ++support.fallbackRegions;
        }
    }
    return support;
}

std::vector<Instruction> emitDirectBytecode(const IrProgram &program,
                                            bool emitMainCall) {
    const DirectIrSupport support = analyzeDirectIrSupport(program);
    if (!support.supported) {
        throw std::logic_error("program contains IR regions unsupported by direct bytecode emission");
    }

    Emitter emitter{program, {}, {}, 0};
    for (const IrInstruction &instruction : program.instructions) {
        emitter.emitInstruction(instruction);
    }
    if (emitMainCall) {
        // Direct emission currently accepts no function definitions, therefore
        // there is no implicit main call to emit in this cohort.
        emitter.bytecode.push_back({OP_DUNG_CHUONG_TRINH, 0, 0, 0});
    }
    return std::move(emitter.bytecode);
}

const char *bytecodeBackendName(BytecodeBackend backend) noexcept {
    switch (backend) {
        case BytecodeBackend::DirectIr: return "direct-ir";
        case BytecodeBackend::LegacyTokenBridge: return "legacy-token-bridge";
    }
    return "legacy-token-bridge";
}

} // namespace vietvm::compiler
