#pragma once

#include <cmath>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>

namespace vietvm::runtime {

// Runtime values are deliberately independent from VM execution and bytecode.
// Native adapters may include this header without pulling in VM internals.
using ScalarValue = std::variant<int, double, std::string, std::monostate>;
using MapValue = std::map<std::string, ScalarValue>;
using StackValue = std::variant<int, double, std::string, std::monostate, MapValue>;

inline bool isNumeric(const StackValue &value) {
    return std::holds_alternative<int>(value) || std::holds_alternative<double>(value);
}

inline std::string scalar_to_string(const ScalarValue &value) {
    if (std::holds_alternative<int>(value)) return std::to_string(std::get<int>(value));
    if (std::holds_alternative<double>(value)) {
        std::ostringstream out;
        const double number = std::get<double>(value);
        if (number == std::floor(number) && !std::isinf(number)) {
            out << std::fixed;
            out.precision(1);
        } else {
            out.precision(10);
        }
        out << number;
        return out.str();
    }
    if (std::holds_alternative<std::string>(value)) {
        return std::string("\"") + std::get<std::string>(value) + "\"";
    }
    return "rỗng";
}

inline double toDouble(const StackValue &value) {
    if (std::holds_alternative<int>(value)) return static_cast<double>(std::get<int>(value));
    if (std::holds_alternative<double>(value)) return std::get<double>(value);
    throw std::runtime_error("toDouble: giá trị không phải số");
}

inline std::string sv_to_string(const StackValue &value) {
    if (std::holds_alternative<int>(value)) return std::to_string(std::get<int>(value));
    if (std::holds_alternative<double>(value)) {
        std::ostringstream out;
        const double number = std::get<double>(value);
        if (number == std::floor(number) && !std::isinf(number)) {
            out << std::fixed;
            out.precision(1);
        } else {
            out.precision(10);
        }
        out << number;
        return out.str();
    }
    if (std::holds_alternative<std::string>(value)) return std::get<std::string>(value);
    if (std::holds_alternative<std::monostate>(value)) return "rỗng";

    const auto &map = std::get<MapValue>(value);
    std::ostringstream out;
    out << "{";
    bool first = true;
    for (const auto &entry : map) {
        if (!first) out << ", ";
        first = false;
        out << "\"" << entry.first << "\": " << scalar_to_string(entry.second);
    }
    out << "}";
    return out.str();
}

inline StackValue make_int_value(int value) { return StackValue(value); }
inline StackValue make_float_value(double value) { return StackValue(value); }
inline StackValue make_string_value(const std::string &value) { return StackValue(value); }
inline StackValue make_null_value() { return StackValue(std::monostate{}); }
inline StackValue make_map_value(const MapValue &value) { return StackValue(value); }

} // namespace vietvm::runtime

// Compatibility aliases keep the existing VM/native implementation source
// stable while callers migrate to vietvm::runtime::*.
using ScalarValue = vietvm::runtime::ScalarValue;
using MapValue = vietvm::runtime::MapValue;
using StackValue = vietvm::runtime::StackValue;
using vietvm::runtime::isNumeric;
using vietvm::runtime::make_float_value;
using vietvm::runtime::make_int_value;
using vietvm::runtime::make_map_value;
using vietvm::runtime::make_null_value;
using vietvm::runtime::make_string_value;
using vietvm::runtime::scalar_to_string;
using vietvm::runtime::sv_to_string;
using vietvm::runtime::toDouble;
