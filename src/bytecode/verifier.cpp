#include "vpp/bytecode/verifier.h"

#include <cstdint>

#include "vpp/bytecode/opcode.h"

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
        const int rawOpcode = static_cast<int>(instruction.op);
        if (!isKnownOpcode(rawOpcode)) {
            return issue(index, rawOpcode, "mã lệnh bytecode không xác định");
        }

        const auto requirePoolIndex = [&](int poolIndex,
                                          const char *label)
            -> std::optional<BytecodeVerificationIssue> {
            if (validPoolIndex(poolIndex, context.stringPoolSize)) {
                return std::nullopt;
            }
            return issue(index, rawOpcode,
                         std::string(label) + " tham chiếu ngoài StringPool");
        };

        switch (instruction.op) {
            case OP_JUMP:
            case OP_JUMP_IF_FALSE:
                if (instruction.operand < 0 ||
                    static_cast<std::size_t>(instruction.operand) >= code.size()) {
                    return issue(index, rawOpcode, "địa chỉ nhảy ngoài phạm vi");
                }
                break;

            case OP_THU:
                if (instruction.operand < 0 ||
                    static_cast<std::size_t>(instruction.operand) >= code.size()) {
                    return issue(index, rawOpcode,
                                 "địa chỉ khối bắt lỗi ngoài phạm vi");
                }
                if (code[static_cast<std::size_t>(instruction.operand)].op !=
                    OP_BAT_LOI) {
                    return issue(index, rawOpcode,
                                 "địa chỉ khối bắt lỗi không trỏ tới OP_BAT_LOI");
                }
                break;

            case OP_THU_KET_THUC:
                if (instruction.operand < 0 ||
                    static_cast<std::size_t>(instruction.operand) > code.size()) {
                    return issue(index, rawOpcode,
                                 "địa chỉ kết thúc khối thử ngoài phạm vi");
                }
                break;

            case OP_CHUOI:
            case OP_BIEN_SO_FLOAT:
            case OP_MAP_LITERAL:
            case OP_LIST_LITERAL:
                if (auto result = requirePoolIndex(instruction.operandIndex,
                                                   "literal")) {
                    return result;
                }
                break;

            case OP_HAM:
                if (auto result = requirePoolIndex(instruction.operand,
                                                   "tên hàm")) {
                    return result;
                }
                if (instruction.operandIndex < 0) {
                    return issue(index, rawOpcode, "function id âm");
                }
                if (!context.functionIds.empty() &&
                    context.functionIds.find(instruction.operandIndex) ==
                        context.functionIds.end()) {
                    return issue(index, rawOpcode,
                                 "function id không có bytecode tương ứng");
                }
                break;

            case OP_GOI:
                if (instruction.operand < 0) {
                    return issue(index, rawOpcode, "số đối số gọi hàm âm");
                }
                if (instruction.operandIndex < 0) {
                    const std::int64_t nameIndex =
                        -(static_cast<std::int64_t>(instruction.operandIndex) + 1);
                    if (nameIndex < 0 ||
                        static_cast<std::size_t>(nameIndex) >= context.stringPoolSize) {
                        return issue(index, rawOpcode,
                                     "tên hàm gọi gián tiếp ngoài StringPool");
                    }
                }
                break;

            case OP_GOI_GIAN_TIEP:
                if (instruction.operand < 0) {
                    return issue(index, rawOpcode, "số đối số gọi gián tiếp âm");
                }
                break;

            case OP_PARAM:
                if (instruction.operandIndex < 0 || instruction.operandValue < -1) {
                    return issue(index, rawOpcode,
                                 "metadata tham số không hợp lệ");
                }
                break;

            case OP_PARAM_MAC_DINH:
                if (instruction.operandIndex < 0 || instruction.operandValue < 0) {
                    return issue(index, rawOpcode,
                                 "metadata tham số mặc định không hợp lệ");
                }
                if (auto result = requirePoolIndex(instruction.operand,
                                                   "giá trị mặc định")) {
                    return result;
                }
                break;

            case OP_CA:
                if (instruction.operandIndex < -2) {
                    return issue(index, rawOpcode, "kiểu nhãn ca không hợp lệ");
                }
                break;

            case OP_TAO_LOP:
                if (auto result = requirePoolIndex(instruction.operandIndex,
                                                   "tên lớp")) {
                    return result;
                }
                if (instruction.operandValue < 0) {
                    return issue(index, rawOpcode,
                                 "metadata lớp cha không hợp lệ");
                }
                if (instruction.operandValue > 0) {
                    if (auto result = requirePoolIndex(instruction.operandValue - 1,
                                                       "tên lớp cha")) {
                        return result;
                    }
                }
                break;

            case OP_THEM_PHUONG_THUC: {
                const std::int64_t classIndex = decodeSignedIndex(instruction.operand);
                if (classIndex < 0 ||
                    static_cast<std::size_t>(classIndex) >= context.stringPoolSize) {
                    return issue(index, rawOpcode,
                                 "tên lớp của phương thức ngoài StringPool");
                }
                if (auto result = requirePoolIndex(instruction.operandIndex,
                                                   "tên phương thức")) {
                    return result;
                }
                const std::int64_t functionId =
                    decodeSignedIndex(instruction.operandValue);
                if (!context.functionIds.empty() &&
                    context.functionIds.find(static_cast<int>(functionId)) ==
                        context.functionIds.end()) {
                    return issue(index, rawOpcode,
                                 "phương thức tham chiếu function id không tồn tại");
                }
                break;
            }

            case OP_TAO_DOI_TUONG:
                if (instruction.operand < 0) {
                    return issue(index, rawOpcode, "số đối số constructor âm");
                }
                if (auto result = requirePoolIndex(instruction.operandIndex,
                                                   "tên lớp constructor")) {
                    return result;
                }
                break;

            case OP_DOC_THUOC_TINH:
            case OP_GAN_THUOC_TINH:
                if (auto result = requirePoolIndex(instruction.operandIndex,
                                                   "tên thuộc tính")) {
                    return result;
                }
                break;

            case OP_GOI_PHUONG_THUC:
                if (instruction.operand < 0) {
                    return issue(index, rawOpcode, "số đối số phương thức âm");
                }
                if (instruction.operandValue != 0 && instruction.operandValue != 1) {
                    return issue(index, rawOpcode,
                                 "chế độ dispatch phương thức không hợp lệ");
                }
                if (auto result = requirePoolIndex(instruction.operandIndex,
                                                   "tên phương thức")) {
                    return result;
                }
                break;

            case OP_TAO_DONG_BAO:
                if (instruction.operand < 0 || instruction.operandIndex < 0) {
                    return issue(index, rawOpcode, "metadata closure không hợp lệ");
                }
                if (!context.functionIds.empty() &&
                    context.functionIds.find(instruction.operand) ==
                        context.functionIds.end()) {
                    return issue(index, rawOpcode,
                                 "closure tham chiếu function id không tồn tại");
                }
                break;

            default:
                break;
        }
    }
    return std::nullopt;
}

} // namespace vietvm::bytecode
