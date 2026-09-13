#pragma once
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sstream>
#include "../vm/instruction.h"
#include "vpp/bytecode/opcode.h"
#include "vpp/core/message_constants.h"
#include "vpp/runtime/value.h"

// Chạy lỗi op; hàm điều phối toàn bộ luồng xử lý của tác vụ, gọi các bước con theo thứ tự và trả mã/kết quả cuối cùng.
inline std::runtime_error runtime_error_op(const std::string &msg, Opcode op, int pc = -1) {
    std::ostringstream oss;
    oss << msg << " (lệnh=" << vietvm::bytecode::opcodeName(op) << ")";
    if (pc >= 0) oss << " tại vị trí=" << pc;
    return std::runtime_error(oss.str());
}

// Chạy lỗi op; hàm điều phối toàn bộ luồng xử lý của tác vụ, gọi các bước con theo thứ tự và trả mã/kết quả cuối cùng.
inline std::runtime_error runtime_error_op(const std::string &msg, Opcode op, std::size_t pc) {
    std::ostringstream oss;
    oss << msg << " (lệnh=" << vietvm::bytecode::opcodeName(op) << ") tại vị trí=" << pc;
    return std::runtime_error(oss.str());
}

// Lấy int từ StackValue
inline int as_int(const StackValue &v, Opcode op = (Opcode)0, int pc = -1) {
    if (std::holds_alternative<int>(v))    return std::get<int>(v);
    if (std::holds_alternative<double>(v)) return static_cast<int>(std::get<double>(v));
    throw runtime_error_op(vietvm::messages::formatMessage(
        vietvm::messages::kVmInvalidIntegerValue), op, pc);
}

// Chuyển `StackValue` số sang `int`; hàm chấp nhận kiểu số runtime hợp lệ và báo lỗi khi giá trị không thể dùng như số nguyên.
inline int as_int(const StackValue &v, Opcode op, std::size_t pc) {
    if (std::holds_alternative<int>(v))    return std::get<int>(v);
    if (std::holds_alternative<double>(v)) return static_cast<int>(std::get<double>(v));
    throw runtime_error_op(vietvm::messages::formatMessage(
        vietvm::messages::kVmInvalidIntegerValue), op, pc);
}

// Cộng hai giá trị số runtime; hàm giữ kiểu số nguyên khi có thể và nâng lên số thực khi một toán hạng là `double`.
inline StackValue numAdd(const StackValue &a, const StackValue &b) {
    if (std::holds_alternative<double>(a) || std::holds_alternative<double>(b))
        return toDouble(a) + toDouble(b);
    return std::get<int>(a) + std::get<int>(b);
}
// Trừ hai giá trị số runtime; hàm áp dụng quy tắc nâng kiểu int/double giống các phép toán số khác của VM.
inline StackValue numSub(const StackValue &a, const StackValue &b) {
    if (std::holds_alternative<double>(a) || std::holds_alternative<double>(b))
        return toDouble(a) - toDouble(b);
    return std::get<int>(a) - std::get<int>(b);
}
// Nhân hai giá trị số runtime; hàm bảo toàn số nguyên nếu cả hai toán hạng là int, ngược lại tính bằng double.
inline StackValue numMul(const StackValue &a, const StackValue &b) {
    if (std::holds_alternative<double>(a) || std::holds_alternative<double>(b))
        return toDouble(a) * toDouble(b);
    return std::get<int>(a) * std::get<int>(b);
}
// Chia hai giá trị số runtime; hàm kiểm tra chia cho 0 và chọn kết quả int/double theo overload đang được gọi.
inline StackValue numDiv(const StackValue &a, const StackValue &b, Opcode op, int pc) {
    double db = toDouble(b);
    if (db == 0.0) throw runtime_error_op(vietvm::messages::formatMessage(
        vietvm::messages::kVmDivisionByZero), op, pc);
    if (std::holds_alternative<double>(a) || std::holds_alternative<double>(b))
        return toDouble(a) / db;
    int ib = std::get<int>(b);
    if (ib == 0) throw runtime_error_op(vietvm::messages::formatMessage(
        vietvm::messages::kVmDivisionByZero), op, pc);
    return std::get<int>(a) / ib;
}

// Chia hai giá trị số runtime; hàm kiểm tra chia cho 0 và chọn kết quả int/double theo overload đang được gọi.
inline StackValue numDiv(const StackValue &a, const StackValue &b, Opcode op, std::size_t pc) {
    double db = toDouble(b);
    if (db == 0.0) throw runtime_error_op(vietvm::messages::formatMessage(
        vietvm::messages::kVmDivisionByZero), op, pc);
    if (std::holds_alternative<double>(a) || std::holds_alternative<double>(b))
        return toDouble(a) / db;
    int ib = std::get<int>(b);
    if (ib == 0) throw runtime_error_op(vietvm::messages::formatMessage(
        vietvm::messages::kVmDivisionByZero), op, pc);
    return std::get<int>(a) / ib;
}

// Thực thi toán tử nhị phân của VM trên hai `StackValue`; hàm dispatch số, chuỗi, so sánh và luận lý theo ký hiệu toán tử.
inline StackValue evaluateBinaryOperator(Opcode op,
                                         const StackValue &a,
                                         const StackValue &b,
                                         int pc) {
    switch (op) {
        case OP_CONG:
            if (isNumeric(a) && isNumeric(b)) return numAdd(a, b);
            return make_string_value(sv_to_string(a) + sv_to_string(b));

        case OP_TRU:
        case OP_NHAN:
        case OP_CHIA:
            if (!isNumeric(a) || !isNumeric(b)) {
                throw runtime_error_op(vietvm::messages::formatMessage(
                    vietvm::messages::kVmNumericOnlyOperator), op, pc);
            }
            if (op == OP_TRU) return numSub(a, b);
            if (op == OP_NHAN) return numMul(a, b);
            return numDiv(a, b, op, pc);

        case OP_Logic_VA:
        case OP_Logic_HOAC: {
            if (!isNumeric(a) || !isNumeric(b)) {
                throw runtime_error_op(vietvm::messages::formatMessage(
                    vietvm::messages::kVmLogicOnlyOperator), op, pc);
            }
            const int left = as_int(a, op, pc);
            const int right = as_int(b, op, pc);
            return make_int_value(op == OP_Logic_VA ? (left && right ? 1 : 0)
                                                        : (left || right ? 1 : 0));
        }

        case OP_SO_SANH_BANG:
        case OP_KHAC_BANG:
        case OP_LON_HON:
        case OP_NHO_HON:
        case OP_LON_HON_HOAC_BANG:
        case OP_NHO_HON_HOAC_BANG: {
            int result = 0;
            if (isNumeric(a) && isNumeric(b)) {
                const double left = toDouble(a);
                const double right = toDouble(b);
                switch (op) {
                    case OP_SO_SANH_BANG: result = left == right ? 1 : 0; break;
                    case OP_KHAC_BANG: result = left != right ? 1 : 0; break;
                    case OP_LON_HON: result = left > right ? 1 : 0; break;
                    case OP_NHO_HON: result = left < right ? 1 : 0; break;
                    case OP_LON_HON_HOAC_BANG: result = left >= right ? 1 : 0; break;
                    case OP_NHO_HON_HOAC_BANG: result = left <= right ? 1 : 0; break;
                    default: break;
                }
                return make_int_value(result);
            }
            if (std::holds_alternative<std::string>(a) &&
                std::holds_alternative<std::string>(b)) {
                const std::string &left = std::get<std::string>(a);
                const std::string &right = std::get<std::string>(b);
                switch (op) {
                    case OP_SO_SANH_BANG: result = left == right ? 1 : 0; break;
                    case OP_KHAC_BANG: result = left != right ? 1 : 0; break;
                    case OP_LON_HON: result = left > right ? 1 : 0; break;
                    case OP_NHO_HON: result = left < right ? 1 : 0; break;
                    case OP_LON_HON_HOAC_BANG: result = left >= right ? 1 : 0; break;
                    case OP_NHO_HON_HOAC_BANG: result = left <= right ? 1 : 0; break;
                    default: break;
                }
                return make_int_value(result);
            }
            throw runtime_error_op(vietvm::messages::formatMessage(
                vietvm::messages::kVmCannotCompareDifferentTypes), op, pc);
        }

        default:
            throw runtime_error_op(vietvm::messages::formatMessage(
                vietvm::messages::kVmUnknownOperator), op, pc);
    }
}

// Thực thi phép chia lấy dư; hàm yêu cầu toán hạng số nguyên và kiểm tra mẫu số khác 0 trước khi tính `%`.
inline StackValue evaluateModuloOperator(const StackValue &a,
                                         const StackValue &b,
                                         Opcode op,
                                         int pc) {
    const int divisor = as_int(b, op, pc);
    if (divisor == 0) {
        throw runtime_error_op(vietvm::messages::formatMessage(
            vietvm::messages::kVmModuloByZero), op, pc);
    }
    return make_int_value(as_int(a, op, pc) % divisor);
}

// Ghi thông tin debug của VM khi chế độ log được bật; hàm gom tham số thành một dòng nhưng không ảnh hưởng trạng thái thực thi.
inline void vmLog(const std::string &msg) {
    std::cerr << vietvm::messages::messageText(vietvm::messages::kVmLogPrefix)
              << msg << std::endl;
}
