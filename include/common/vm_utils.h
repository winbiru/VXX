#pragma once
#include <variant>
#include <string>
#include <stdexcept>
#include <sstream>
#include <cmath>
#include "../vm/instruction.h"
#include "../frontend/keywords.h"  // for name_op()

using StackValue = std::variant<int, double, std::string>;

inline std::runtime_error runtime_error_op(const std::string &msg, Opcode op, int pc = -1) {
    std::ostringstream oss;
    oss << msg << " (op=" << name_op(op) << ")";
    if (pc >= 0) oss << " at pc=" << pc;
    return std::runtime_error(oss.str());
}

// Check if value is numeric (int or double)
inline bool isNumeric(const StackValue &v) {
    return std::holds_alternative<int>(v) || std::holds_alternative<double>(v);
}

// Convert numeric StackValue to double
inline double toDouble(const StackValue &v) {
    if (std::holds_alternative<int>(v))    return static_cast<double>(std::get<int>(v));
    if (std::holds_alternative<double>(v)) return std::get<double>(v);
    throw std::runtime_error("toDouble: giá trị không phải số");
}

// Chuyển StackValue thành string
inline std::string sv_to_string(const StackValue &v) {
    if (std::holds_alternative<int>(v)) return std::to_string(std::get<int>(v));
    if (std::holds_alternative<double>(v)) {
        std::ostringstream oss;
        double d = std::get<double>(v);
        if (d == std::floor(d) && !std::isinf(d)) {
            oss << std::fixed;
            oss.precision(1);
        } else {
            oss.precision(10);
        }
        oss << d;
        return oss.str();
    }
    return std::get<std::string>(v);
}

// Lấy int từ StackValue
inline int as_int(const StackValue &v, Opcode op = (Opcode)0, int pc = -1) {
    if (std::holds_alternative<int>(v))    return std::get<int>(v);
    if (std::holds_alternative<double>(v)) return static_cast<int>(std::get<double>(v));
    throw runtime_error_op("Lỗi: giá trị không phải số nguyên", op, pc);
}

// Helper tạo giá trị số nguyên
inline StackValue make_int_value(int v)           { return StackValue(v); }
// Helper tạo giá trị số thực
inline StackValue make_float_value(double v)      { return StackValue(v); }
// Helper tạo chuỗi
inline StackValue make_string_value(const std::string &s) { return StackValue(s); }

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
    if (db == 0.0) throw runtime_error_op("Lỗi: chia cho 0", op, pc);
    if (std::holds_alternative<double>(a) || std::holds_alternative<double>(b))
        return toDouble(a) / db;
    int ib = std::get<int>(b);
    if (ib == 0) throw runtime_error_op("Lỗi: chia cho 0", op, pc);
    return std::get<int>(a) / ib;
}

inline void vmLog(const std::string &msg) {
    std::cerr << "[VM] " << msg << std::endl;
}
