#pragma once
#include <variant>
#include <string>
#include <stdexcept>
#include <sstream>
#include "../instruction.h"

// Lưu ý: chỉnh đường dẫn "../instruction.h" nếu project dùng include khác.
// Hàm name_op(Opcode) đã có trong project (name_op.cpp) — link tự resolve tại link-time.
std::string name_op(Opcode op);

// StackValue typedef nếu chưa có chung (nếu đã có, bỏ hoặc đồng bộ)
using StackValue = std::variant<int, std::string>;

// Tạo lỗi runtime có ngữ cảnh (msg, opcode tên, program counter)
inline std::runtime_error runtime_error_op(const std::string &msg, Opcode op, int pc = -1) {
    std::ostringstream oss;
    oss << msg << " (op=" << name_op(op) << ")";
    if (pc >= 0) oss << " at pc=" << pc;
    return std::runtime_error(oss.str());
}

// Chuyển StackValue thành string (dùng cho logging)
inline std::string sv_to_string(const StackValue &v) {
    if (std::holds_alternative<int>(v)) return std::to_string(std::get<int>(v));
    return std::get<std::string>(v);
}

// Lấy int từ StackValue, ném runtime_error_op nếu không đúng kiểu
inline int as_int(const StackValue &v, Opcode op = (Opcode)0, int pc = -1) {
    if (!std::holds_alternative<int>(v)) {
        throw runtime_error_op("Lỗi: giá trị không phải số nguyên", op, pc);
    }
    return std::get<int>(v);
}

// Lấy string từ StackValue (chuyển số sang chuỗi nếu cần)
inline std::string as_string(const StackValue &v) {
    if (std::holds_alternative<int>(v)) return std::to_string(std::get<int>(v));
    return std::get<std::string>(v);
}

// Helper tạo giá trị số nguyên
inline StackValue make_int_value(int v) {
    return StackValue(v);
}

// Helper tạo chuỗi
inline StackValue make_string_value(const std::string &s) {
    return StackValue(s);
}

// Logging helper (tuỳ chọn)
inline void vmLog(const std::string &msg) {
    std::cerr << "[VM] " << msg << std::endl;
}
