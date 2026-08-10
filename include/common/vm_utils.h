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
    oss << msg << " (op=" << vietvm::bytecode::opcodeName(op) << ")";
    if (pc >= 0) oss << " at pc=" << pc;
    return std::runtime_error(oss.str());
}

inline std::runtime_error runtime_error_op(const std::string &msg, Opcode op, std::size_t pc) {
    std::ostringstream oss;
    oss << msg << " (op=" << vietvm::bytecode::opcodeName(op) << ") at pc=" << pc;
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

inline void vmLog(const std::string &msg) {
    std::cerr << vietvm::messages::messageText(vietvm::messages::kVmLogPrefix)
              << msg << std::endl;
}
