#include "vpp/bytecode/verifier.h"

#include <cstdint>

#include "vpp/bytecode/opcode.h"
#include "vpp/bytecode/intrinsic.h"
#include "vpp/core/message_constants.h"

namespace vietvm::bytecode {

namespace {

BytecodeVerificationIssue issue(std::size_t index,
                                int rawOpcode,
                                std::string message) {
    return {index, rawOpcode, std::move(message)};
}

bool validPoolIndex(int index, std::size_t size) noexcept {
    return index >= 0 && static_cast<std::size_t>(index) < size;
}

std::int64_t decodeSignedIndex(int encoded) noexcept {
    return encoded >= 0
        ? static_cast<std::int64_t>(encoded)
        : -(static_cast<std::int64_t>(encoded) + 1);
}

} // namespace

std::optional<BytecodeVerificationIssue> verifyBytecode(
    const std::vector<Instruction> &code,
    const BytecodeVerificationContext &context) {
    for (std::size_t index = 0; index < code.size(); ++index) {
        const Instruction &instruction = code[index];
        const int rawOpcode = instruction.op;
        if (!isKnownOpcode(rawOpcode)) {
            return issue(index, rawOpcode,
                         messages::messageText(messages::kBytecodeUnknownOpcode));
        }
        const Opcode opcode = static_cast<Opcode>(rawOpcode);
        if (intrinsicByOpcode(rawOpcode) != nullptr &&
            (instruction.operand != 0 || instruction.operandIndex != 0 ||
             instruction.operandValue != 0)) {
            return issue(index, rawOpcode, "VM primitive không nhận immediate operand");
        }

        const auto requirePoolIndex = [&](int poolIndex,
                                          std::string_view label)
            -> std::optional<BytecodeVerificationIssue> {
            if (validPoolIndex(poolIndex, context.stringPoolSize)) {
                return std::nullopt;
            }
            return issue(index, rawOpcode,
                         messages::formatMessage(
                             messages::kBytecodePoolReferenceOutOfRange,
                             {label}));
        };

        switch (opcode) {
            case OP_JUMP:
            case OP_JUMP_IF_FALSE:
                if (instruction.operand < 0 ||
                    static_cast<std::size_t>(instruction.operand) >= code.size()) {
                    return issue(index, rawOpcode,
                                 messages::messageText(messages::kBytecodeJumpAddressOutOfRange));
                }
                break;

            case OP_THU:
                if (instruction.operand < 0 ||
                    static_cast<std::size_t>(instruction.operand) >= code.size()) {
                    return issue(index, rawOpcode,
                                 messages::messageText(messages::kBytecodeCatchAddressOutOfRange));
                }
                if (code[static_cast<std::size_t>(instruction.operand)].op !=
                    OP_BAT_LOI) {
                    return issue(index, rawOpcode,
                                 messages::messageText(messages::kBytecodeCatchAddressMustPointToCatch));
                }
                break;

            case OP_THU_KET_THUC:
                if (instruction.operand < 0 ||
                    static_cast<std::size_t>(instruction.operand) > code.size()) {
                    return issue(index, rawOpcode,
                                 messages::messageText(messages::kBytecodeTryEndAddressOutOfRange));
                }
                break;

            case OP_CHUOI:
            case OP_BIEN_SO_FLOAT:
                if (auto result = requirePoolIndex(
                        instruction.operandIndex,
                        messages::kBytecodePoolLabelLiteral)) {
                    return result;
                }
                break;

            case OP_MAP_LITERAL:
            case OP_LIST_LITERAL:
                if (instruction.operandIndex == -1) {
                    if (instruction.operand < 0) {
                        return issue(index, rawOpcode,
                                     messages::messageText(messages::kBytecodeDynamicCollectionCountNegative));
                    }
                    break;
                }
                if (auto result = requirePoolIndex(
                        instruction.operandIndex,
                        messages::kBytecodePoolLabelLiteral)) {
                    return result;
                }
                break;

            case OP_HAM:
                if (auto result = requirePoolIndex(
                        instruction.operand,
                        messages::kBytecodePoolLabelFunctionName)) {
                    return result;
                }
                if (instruction.operandIndex < 0) {
                    return issue(index, rawOpcode,
                                 messages::messageText(messages::kBytecodeFunctionIdNegative));
                }
                if (!context.functionIds.empty() &&
                    context.functionIds.find(instruction.operandIndex) ==
                        context.functionIds.end()) {
                    return issue(index, rawOpcode,
                                 messages::messageText(messages::kBytecodeFunctionIdMissing));
                }
                break;

            case OP_GOI:
                if (instruction.operand < 0) {
                    return issue(index, rawOpcode,
                                 messages::messageText(messages::kBytecodeCallArgumentCountNegative));
                }
                if (instruction.operandIndex < 0) {
                    const std::int64_t nameIndex =
                        -(static_cast<std::int64_t>(instruction.operandIndex) + 1);
                    if (nameIndex < 0 ||
                        static_cast<std::size_t>(nameIndex) >= context.stringPoolSize) {
                        return issue(index, rawOpcode,
                                     messages::messageText(messages::kBytecodeIndirectCallNameOutOfRange));
                    }
                }
                break;

            case OP_GOI_GIAN_TIEP:
                if (instruction.operand < 0) {
                    return issue(index, rawOpcode,
                                 messages::messageText(messages::kBytecodeIndirectCallArgumentCountNegative));
                }
                break;

            case OP_PARAM:
                if (instruction.operandIndex < 0 || instruction.operandValue < -1) {
                    return issue(index, rawOpcode,
                                 messages::messageText(messages::kBytecodeParameterMetadataInvalid));
                }
                break;

            case OP_PARAM_MAC_DINH:
                if (instruction.operandIndex < 0 || instruction.operandValue < 0) {
                    return issue(index, rawOpcode,
                                 messages::messageText(messages::kBytecodeDefaultParameterMetadataInvalid));
                }
                if (auto result = requirePoolIndex(
                        instruction.operand,
                        messages::kBytecodePoolLabelDefaultValue)) {
                    return result;
                }
                break;

            case OP_CA:
                if (instruction.operandIndex < -2) {
                    return issue(index, rawOpcode,
                                 messages::messageText(messages::kBytecodeCaseLabelKindInvalid));
                }
                break;

            case OP_TAO_LOP:
                if (auto result = requirePoolIndex(
                        instruction.operandIndex,
                        messages::kBytecodePoolLabelClassName)) {
                    return result;
                }
                if (instruction.operandValue < 0) {
                    return issue(index, rawOpcode,
                                 messages::messageText(messages::kBytecodeSuperclassMetadataInvalid));
                }
                if (instruction.operandValue > 0) {
                    if (auto result = requirePoolIndex(
                            instruction.operandValue - 1,
                            messages::kBytecodePoolLabelSuperclassName)) {
                        return result;
                    }
                }
                break;

            case OP_THEM_PHUONG_THUC: {
                const std::int64_t classIndex = decodeSignedIndex(instruction.operand);
                if (classIndex < 0 ||
                    static_cast<std::size_t>(classIndex) >= context.stringPoolSize) {
                    return issue(index, rawOpcode,
                                 messages::messageText(messages::kBytecodeMethodClassNameOutOfRange));
                }
                if (auto result = requirePoolIndex(
                        instruction.operandIndex,
                        messages::kBytecodePoolLabelMethodName)) {
                    return result;
                }
                const std::int64_t functionId =
                    decodeSignedIndex(instruction.operandValue);
                if (!context.functionIds.empty() &&
                    context.functionIds.find(static_cast<int>(functionId)) ==
                        context.functionIds.end()) {
                    return issue(index, rawOpcode,
                                 messages::messageText(messages::kBytecodeMethodFunctionIdMissing));
                }
                break;
            }

            case OP_TAO_DOI_TUONG:
                if (instruction.operand < 0) {
                    return issue(index, rawOpcode,
                                 messages::messageText(messages::kBytecodeConstructorArgumentCountNegative));
                }
                if (auto result = requirePoolIndex(
                        instruction.operandIndex,
                        messages::kBytecodePoolLabelConstructorClassName)) {
                    return result;
                }
                break;

            case OP_DOC_THUOC_TINH:
            case OP_GAN_THUOC_TINH:
                if (auto result = requirePoolIndex(
                        instruction.operandIndex,
                        messages::kBytecodePoolLabelPropertyName)) {
                    return result;
                }
                break;

            case OP_GOI_PHUONG_THUC:
                if (instruction.operand < 0) {
                    return issue(index, rawOpcode,
                                 messages::messageText(messages::kBytecodeMethodArgumentCountNegative));
                }
                if (instruction.operandValue != 0 && instruction.operandValue != 1) {
                    return issue(index, rawOpcode,
                                 messages::messageText(messages::kBytecodeMethodDispatchModeInvalid));
                }
                if (auto result = requirePoolIndex(
                        instruction.operandIndex,
                        messages::kBytecodePoolLabelMethodName)) {
                    return result;
                }
                break;

            case OP_TAO_DONG_BAO:
                if (instruction.operand < 0 || instruction.operandIndex < 0) {
                    return issue(index, rawOpcode,
                                 messages::messageText(messages::kBytecodeClosureMetadataInvalid));
                }
                if (!context.functionIds.empty() &&
                    context.functionIds.find(instruction.operand) ==
                        context.functionIds.end()) {
                    return issue(index, rawOpcode,
                                 messages::messageText(messages::kBytecodeClosureFunctionIdMissing));
                }
                break;

            default:
                break;
        }
    }
    return std::nullopt;
}

} // namespace vietvm::bytecode
