#pragma once
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <sstream>
#include <utility>
#include "../vm/instruction.h"
#include "vpp/bytecode/opcode.h"
#include "vpp/core/message_constants.h"
#include "vpp/runtime/error.h"
#include "vpp/runtime/value.h"

// Trả tên kiểu V++ dễ hiểu của một giá trị runtime để bộ chẩn đoán có thể giải
// thích giá trị thực tế mà không lộ tên kiểu C++ hay chi tiết variant nội bộ.
inline std::string runtime_value_type_name(const StackValue &value) {
    if (std::holds_alternative<int>(value)) return "số nguyên";
    if (std::holds_alternative<double>(value)) return "số thực";
    if (std::holds_alternative<std::string>(value)) return "chuỗi";
    if (std::holds_alternative<std::monostate>(value)) return "rỗng";
    if (std::holds_alternative<MapHandle>(value)) return "ánh xạ";
    if (std::holds_alternative<ListHandle>(value)) return "danh sách";
    if (std::holds_alternative<TupleHandle>(value)) return "bộ";
    if (std::holds_alternative<ClassHandle>(value)) return "lớp";
    if (std::holds_alternative<InstanceHandle>(value)) return "đối tượng";
    return "hàm đóng";
}

// Thu dữ kiện của hai giá trị đang tham gia một phép toán. Hàm chỉ mô tả trạng
// thái thực tế; bộ chẩn đoán sẽ tự quyết định trạng thái đó tương ứng lỗi nào.
inline vietvm::runtime::RuntimeDiagnosticContext runtime_binary_facts(
    const StackValue &left,
    const StackValue &right) {
    vietvm::runtime::RuntimeDiagnosticContext context;
    context.operandTexts = {sv_to_string(left), sv_to_string(right)};
    context.operandNumeric = {isNumeric(left), isNumeric(right)};
    return context;
}

// Tạo lỗi runtime và gắn opcode vào dữ kiện chẩn đoán. Nơi gọi có thể bổ sung
// giá trị thực tế nhưng không phải chỉ định trước tên hay mã của loại lỗi.
inline vietvm::runtime::RuntimeError runtime_error_op(
    const std::string &msg,
    Opcode op,
    int /*pc*/ = -1,
    vietvm::runtime::RuntimeDiagnosticContext context = {}) {
    context.opcode = static_cast<int>(op);
    return vietvm::runtime::RuntimeError(
        msg,
        vietvm::runtime::RuntimeErrorKind::VmFault,
        std::move(context));
}

// Overload cho program counter kiểu `size_t`; giữ cùng contract chẩn đoán và
// vị trí nguồn vẫn được stack trace bổ sung khi lỗi unwind khỏi VM.
inline vietvm::runtime::RuntimeError runtime_error_op(
    const std::string &msg,
    Opcode op,
    std::size_t /*pc*/,
    vietvm::runtime::RuntimeDiagnosticContext context = {}) {
    context.opcode = static_cast<int>(op);
    return vietvm::runtime::RuntimeError(
        msg,
        vietvm::runtime::RuntimeErrorKind::VmFault,
        std::move(context));
}

// Lấy số nguyên từ StackValue; nếu giá trị không phải số, hàm gửi kiểu thực tế
// cho bộ chẩn đoán để hệ thống tự nhận ra lỗi kiểu dữ liệu.
inline int as_int(const StackValue &v, Opcode op = (Opcode)0, int pc = -1) {
    if (std::holds_alternative<int>(v)) return std::get<int>(v);
    if (std::holds_alternative<double>(v)) return static_cast<int>(std::get<double>(v));
    vietvm::runtime::RuntimeDiagnosticContext context;
    context.expectsInteger = true;
    context.actualType = runtime_value_type_name(v);
    throw runtime_error_op(vietvm::messages::formatMessage(
        vietvm::messages::kVmInvalidIntegerValue), op, pc, std::move(context));
}

// Chuyển `StackValue` số sang `int`; overload này giữ program counter `size_t`
// nhưng thu cùng dữ kiện kiểu thực tế như overload dùng `int`.
inline int as_int(const StackValue &v, Opcode op, std::size_t pc) {
    if (std::holds_alternative<int>(v)) return std::get<int>(v);
    if (std::holds_alternative<double>(v)) return static_cast<int>(std::get<double>(v));
    vietvm::runtime::RuntimeDiagnosticContext context;
    context.expectsInteger = true;
    context.actualType = runtime_value_type_name(v);
    throw runtime_error_op(vietvm::messages::formatMessage(
        vietvm::messages::kVmInvalidIntegerValue), op, pc, std::move(context));
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

// Chia hai giá trị số runtime; khi số chia bằng 0, hàm gửi hai giá trị thật cho
// bộ chẩn đoán và không gắn nhãn `DivisionByZero` tại vị trí này.
inline StackValue numDiv(const StackValue &a, const StackValue &b, Opcode op, int pc) {
    double db = toDouble(b);
    if (db == 0.0) {
        throw runtime_error_op(vietvm::messages::formatMessage(
            vietvm::messages::kVmDivisionByZero,
            {sv_to_string(a), sv_to_string(b)}), op, pc, runtime_binary_facts(a, b));
    }
    if (std::holds_alternative<double>(a) || std::holds_alternative<double>(b))
        return toDouble(a) / db;
    return std::get<int>(a) / std::get<int>(b);
}

// Chia hai giá trị số runtime; overload `size_t` dùng cùng cơ chế suy luận từ
// hai toán hạng thực tế như overload `int`.
inline StackValue numDiv(const StackValue &a, const StackValue &b, Opcode op, std::size_t pc) {
    double db = toDouble(b);
    if (db == 0.0) {
        throw runtime_error_op(vietvm::messages::formatMessage(
            vietvm::messages::kVmDivisionByZero,
            {sv_to_string(a), sv_to_string(b)}), op, pc, runtime_binary_facts(a, b));
    }
    if (std::holds_alternative<double>(a) || std::holds_alternative<double>(b))
        return toDouble(a) / db;
    return std::get<int>(a) / std::get<int>(b);
}

// Thực thi toán tử nhị phân của VM; khi kiểu dữ liệu không phù hợp, hàm gửi các
// giá trị thực tế để bộ chẩn đoán tự phân biệt lỗi phép tính và lỗi so sánh.
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
                    vietvm::messages::kVmNumericOnlyOperator), op, pc,
                    runtime_binary_facts(a, b));
            }
            if (op == OP_TRU) return numSub(a, b);
            if (op == OP_NHAN) return numMul(a, b);
            return numDiv(a, b, op, pc);

        case OP_Logic_VA:
        case OP_Logic_HOAC: {
            const bool left = stackValueTruthy(a);
            const bool right = stackValueTruthy(b);
            return make_int_value(op == OP_Logic_VA ? (left && right ? 1 : 0)
                                                     : (left || right ? 1 : 0));
        }

        case OP_SO_SANH_BANG:
        case OP_KHAC_BANG: {
            const bool equal = sameStackValue(a, b);
            return make_int_value(op == OP_SO_SANH_BANG ? (equal ? 1 : 0)
                                                         : (equal ? 0 : 1));
        }

        case OP_LON_HON:
        case OP_NHO_HON:
        case OP_LON_HON_HOAC_BANG:
        case OP_NHO_HON_HOAC_BANG: {
            int result = 0;
            if (isNumeric(a) && isNumeric(b)) {
                const double left = toDouble(a);
                const double right = toDouble(b);
                switch (op) {
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
                    case OP_LON_HON: result = left > right ? 1 : 0; break;
                    case OP_NHO_HON: result = left < right ? 1 : 0; break;
                    case OP_LON_HON_HOAC_BANG: result = left >= right ? 1 : 0; break;
                    case OP_NHO_HON_HOAC_BANG: result = left <= right ? 1 : 0; break;
                    default: break;
                }
                return make_int_value(result);
            }
            throw runtime_error_op(vietvm::messages::formatMessage(
                vietvm::messages::kVmCannotCompareDifferentTypes), op, pc,
                runtime_binary_facts(a, b));
        }

        default: {
            vietvm::runtime::RuntimeDiagnosticContext context;
            context.internalInvariantChecked = true;
            context.internalInvariantValid = false;
            throw runtime_error_op(vietvm::messages::formatMessage(
                vietvm::messages::kVmUnknownOperator), op, pc, std::move(context));
        }
    }
}

// Thực thi phép chia lấy dư; khi số chia bằng 0, hai toán hạng thật được chuyển
// cho bộ chẩn đoán để hệ thống tự nhận diện lỗi số học.
inline StackValue evaluateModuloOperator(const StackValue &a,
                                         const StackValue &b,
                                         Opcode op,
                                         int pc) {
    auto strictInteger = [&](const StackValue &value) -> int {
        if (std::holds_alternative<int>(value)) return std::get<int>(value);
        if (std::holds_alternative<double>(value)) {
            const double number = std::get<double>(value);
            if (std::isfinite(number) && std::trunc(number) == number &&
                number >= static_cast<double>(std::numeric_limits<int>::min()) &&
                number <= static_cast<double>(std::numeric_limits<int>::max())) {
                return static_cast<int>(number);
            }
        }
        vietvm::runtime::RuntimeDiagnosticContext context;
        context.expectsInteger = true;
        context.actualType = runtime_value_type_name(value);
        throw runtime_error_op(vietvm::messages::formatMessage(
            vietvm::messages::kVmInvalidIntegerValue), op, pc, std::move(context));
    };

    const int divisor = strictInteger(b);
    if (divisor == 0) {
        throw runtime_error_op(vietvm::messages::formatMessage(
            vietvm::messages::kVmModuloByZero), op, pc, runtime_binary_facts(a, b));
    }
    return make_int_value(strictInteger(a) % divisor);
}

// Ghi thông tin debug của VM khi chế độ log được bật; hàm gom tham số thành một dòng nhưng không ảnh hưởng trạng thái thực thi.
inline void vmLog(const std::string &msg) {
    std::cerr << vietvm::messages::messageText(vietvm::messages::kVmLogPrefix)
              << msg << std::endl;
}
