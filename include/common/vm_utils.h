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

inline std::runtime_error runtime_error_op(const std::string &msg, Opcode op, int pc = -1) {
    std::ostringstream oss;
    oss << msg << " (lệnh=" << vietvm::bytecode::opcodeName(op) << ")";
    if (pc >= 0) oss << " tại vị trí=" << pc;
    return std::runtime_error(oss.str());
}

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

inline int as_int(const StackValue &v, Opcode op, std::size_t pc) {
    if (std::holds_alternative<int>(v))    return std::get<int>(v);
    if (std::holds_alternative<double>(v)) return static_cast<int>(std::get<double>(v));
    throw runtime_error_op(vietvm::messages::formatMessage(
        vietvm::messages::kVmInvalidIntegerValue), op, pc);
}

// Arithmetic helper: promote to double if either is double, else int
inline StackValue numAdd(const StackValue &a, const StackValue &b) {
    if (std::holds_alternative<double>(a) || std::holds_alternative<double>(b))
        return toDouble(a) + toDouble(b);
    return std::get<int>(a) + std::get<int>(b);
}
inline StackValue numSub(const StackValue &a, const StackValue &b) {
    if (std::holds_alternative<double>(a) || std::holds_alternative<double>(b))
        return toDouble(a) - toDouble(b);
    return std::get<int>(a) - std::get<int>(b);
}
inline StackValue numMul(const StackValue &a, const StackValue &b) {
    if (std::holds_alternative<double>(a) || std::holds_alternative<double>(b))
        return toDouble(a) * toDouble(b);
    return std::get<int>(a) * std::get<int>(b);
}
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

// Keep the JIT and interpreter on one operator contract.  Callers handle
// stack underflow; this helper only evaluates already-popped operands.
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

inline void vmLog(const std::string &msg) {
    std::cerr << vietvm::messages::messageText(vietvm::messages::kVmLogPrefix)
              << msg << std::endl;
}
