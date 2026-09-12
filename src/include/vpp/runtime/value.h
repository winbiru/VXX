#pragma once

#include <cmath>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "vpp/core/message_constants.h"

namespace vietvm::runtime {

// Runtime values are deliberately independent from VM execution and bytecode.
// Native adapters may include this header without pulling in VM internals.
using ScalarValue = std::variant<int, double, std::string, std::monostate>;
struct MapValue;
struct ListValue;
struct TupleValue;
using MapHandle = std::shared_ptr<MapValue>;
using ListHandle = std::shared_ptr<ListValue>;
using TupleHandle = std::shared_ptr<TupleValue>;
using StackValue = std::variant<int, double, std::string, std::monostate, MapHandle, ListHandle, TupleHandle>;

// Handles make recursive collection values representable by std::variant while
// preserving reference semantics for mutable V++ collections.
struct MapValue {
    std::map<std::string, StackValue> entries;
};

struct ListValue {
    std::vector<StackValue> elements;
};

struct TupleValue {
    std::vector<StackValue> elements;
};

inline bool isNumeric(const StackValue &value) {
    return std::holds_alternative<int>(value) || std::holds_alternative<double>(value);
}

// Keep every runtime surface (printing, string interpolation, and native
// conversion) on the same floating-point spelling.  This used to be copied
// between scalar_to_string and sv_to_string, which made future formatting
// fixes unnecessarily easy to apply to only one path.
inline std::string formatRuntimeFloat(double number) {
    std::ostringstream out;
    if (number == std::floor(number) && !std::isinf(number)) {
        out << std::fixed;
        out.precision(1);
    } else {
        out.precision(10);
    }
    out << number;
    return out.str();
}

inline std::string scalar_to_string(const ScalarValue &value) {
    if (std::holds_alternative<int>(value)) return std::to_string(std::get<int>(value));
    if (std::holds_alternative<double>(value)) return formatRuntimeFloat(std::get<double>(value));
    if (std::holds_alternative<std::string>(value)) {
        return std::string("\"") + std::get<std::string>(value) + "\"";
    }
    return "rỗng";
}

inline double toDouble(const StackValue &value) {
    if (std::holds_alternative<int>(value)) return static_cast<double>(std::get<int>(value));
    if (std::holds_alternative<double>(value)) return std::get<double>(value);
    throw std::runtime_error(std::string(vietvm::messages::kRuntimeValueNotNumeric));
}

// Equality used by collection operations.  Numeric values compare by value so
// that 1 and 1.0 remain interchangeable, while mutable/reference collections
// intentionally retain identity semantics.  Keeping this beside StackValue
// prevents list, set, and future collection APIs from growing subtly different
// equality rules.
inline bool sameStackValue(const StackValue &left, const StackValue &right) {
    if (isNumeric(left) && isNumeric(right)) return toDouble(left) == toDouble(right);
    if (left.index() != right.index()) return false;
    if (std::holds_alternative<std::string>(left)) {
        return std::get<std::string>(left) == std::get<std::string>(right);
    }
    if (std::holds_alternative<std::monostate>(left)) return true;
    if (std::holds_alternative<MapHandle>(left)) {
        return std::get<MapHandle>(left) == std::get<MapHandle>(right);
    }
    if (std::holds_alternative<ListHandle>(left)) {
        return std::get<ListHandle>(left) == std::get<ListHandle>(right);
    }
    if (std::holds_alternative<TupleHandle>(left)) {
        return std::get<TupleHandle>(left) == std::get<TupleHandle>(right);
    }
    return false;
}

// Returns whether `left` sorts before `right`.  `comparable` is false for
// mixed or unsupported types, which lets callers report a stable API error
// instead of imposing an accidental variant-index ordering.
inline bool stackValueLess(const StackValue &left,
                           const StackValue &right,
                           bool &comparable) {
    comparable = true;
    if (isNumeric(left) && isNumeric(right)) return toDouble(left) < toDouble(right);
    if (std::holds_alternative<std::string>(left) &&
        std::holds_alternative<std::string>(right)) {
        return std::get<std::string>(left) < std::get<std::string>(right);
    }
    comparable = false;
    return false;
}

namespace detail {

inline std::string stackValueToString(
    const StackValue &value,
    std::unordered_set<const void *> &activeCollections) {
    if (std::holds_alternative<int>(value)) return std::to_string(std::get<int>(value));
    if (std::holds_alternative<double>(value)) return formatRuntimeFloat(std::get<double>(value));
    if (std::holds_alternative<std::string>(value)) return std::get<std::string>(value);
    if (std::holds_alternative<std::monostate>(value)) return "rỗng";

    if (std::holds_alternative<ListHandle>(value)) {
        const ListHandle &list = std::get<ListHandle>(value);
        if (list != nullptr && !activeCollections.insert(list.get()).second) {
            return "<cycle>";
        }
        std::ostringstream out;
        out << "[";
        if (list != nullptr) {
            for (std::size_t index = 0; index < list->elements.size(); ++index) {
                if (index != 0) out << ", ";
                out << stackValueToString(list->elements[index], activeCollections);
            }
            activeCollections.erase(list.get());
        }
        out << "]";
        return out.str();
    }

    if (std::holds_alternative<TupleHandle>(value)) {
        const TupleHandle &tuple = std::get<TupleHandle>(value);
        if (tuple != nullptr && !activeCollections.insert(tuple.get()).second) {
            return "<cycle>";
        }
        std::ostringstream out;
        out << "(";
        if (tuple != nullptr) {
            for (std::size_t index = 0; index < tuple->elements.size(); ++index) {
                if (index != 0) out << ", ";
                out << stackValueToString(tuple->elements[index], activeCollections);
            }
            if (tuple->elements.size() == 1) out << ",";
            activeCollections.erase(tuple.get());
        }
        out << ")";
        return out.str();
    }

    const MapHandle &map = std::get<MapHandle>(value);
    if (map != nullptr && !activeCollections.insert(map.get()).second) {
        return "<cycle>";
    }
    std::ostringstream out;
    out << "{";
    bool first = true;
    if (map != nullptr) {
        for (const auto &entry : map->entries) {
            if (!first) out << ", ";
            first = false;
            out << "\"" << entry.first << "\": ";
            if (std::holds_alternative<std::string>(entry.second)) {
                // Maps retain their object-like representation for scalar
                // strings, while recursive collections continue through the
                // general StackValue renderer.
                out << "\"" << std::get<std::string>(entry.second) << "\"";
            } else {
                out << stackValueToString(entry.second, activeCollections);
            }
        }
        activeCollections.erase(map.get());
    }
    out << "}";
    return out.str();
}

} // namespace detail

// Collections are reference values and may be made cyclic by native mutation
// (`thêm(ds, ds)` or `đặt map(m, "self", m)`).  Keep a per-render active set
// so printing remains finite while repeated, non-cyclic references still
// render normally each time.
inline std::string sv_to_string(const StackValue &value) {
    std::unordered_set<const void *> activeCollections;
    return detail::stackValueToString(value, activeCollections);
}

inline StackValue make_int_value(int value) { return StackValue(value); }
inline StackValue make_float_value(double value) { return StackValue(value); }
inline StackValue make_string_value(const std::string &value) { return StackValue(value); }
inline StackValue make_null_value() { return StackValue(std::monostate{}); }
inline StackValue make_map_value(MapValue value) {
    return StackValue(std::make_shared<MapValue>(std::move(value)));
}
inline StackValue make_list_value(std::vector<StackValue> value) {
    return StackValue(std::make_shared<ListValue>(ListValue{std::move(value)}));
}
inline StackValue make_tuple_value(std::vector<StackValue> value) {
    return StackValue(std::make_shared<TupleValue>(TupleValue{std::move(value)}));
}

} // namespace vietvm::runtime

// Compatibility aliases keep the existing VM/native implementation source
// stable while callers migrate to vietvm::runtime::*.
using ScalarValue = vietvm::runtime::ScalarValue;
using MapValue = vietvm::runtime::MapValue;
using MapHandle = vietvm::runtime::MapHandle;
using TupleHandle = vietvm::runtime::TupleHandle;
using ListHandle = vietvm::runtime::ListHandle;
using StackValue = vietvm::runtime::StackValue;
using vietvm::runtime::isNumeric;
using vietvm::runtime::formatRuntimeFloat;
using vietvm::runtime::make_float_value;
using vietvm::runtime::make_int_value;
using vietvm::runtime::make_map_value;
using vietvm::runtime::make_list_value;
using vietvm::runtime::make_tuple_value;
using vietvm::runtime::make_null_value;
using vietvm::runtime::make_string_value;
using vietvm::runtime::scalar_to_string;
using vietvm::runtime::sameStackValue;
using vietvm::runtime::stackValueLess;
using vietvm::runtime::sv_to_string;
using vietvm::runtime::toDouble;
