#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "vpp/runtime/value.h"

namespace vietvm::runtime {

// Kiểm tra điều kiện của `containsStackValue`.
inline bool containsStackValue(const std::vector<StackValue> &values,
                               const StackValue &target) {
    for (const StackValue &value : values) {
        if (sameStackValue(value, target)) return true;
    }
    return false;
}

// Tìm stack giá trị chỉ số; hàm tra cứu dữ liệu theo tiêu chí đầu vào và trả về vị trí hoặc phần tử phù hợp nếu có.
inline std::optional<std::size_t> findStackValueIndex(
    const std::vector<StackValue> &values,
    const StackValue &target) {
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (sameStackValue(values[index], target)) return index;
    }
    return std::nullopt;
}

// Loại phần tử trùng khỏi danh sách `StackValue`; hàm giữ lần xuất hiện đầu tiên theo equality runtime và bảo toàn thứ tự tương đối.
inline std::vector<StackValue> uniqueStackValues(
    const std::vector<StackValue> &values) {
    std::vector<StackValue> unique;
    unique.reserve(values.size());
    for (const StackValue &candidate : values) {
        if (!containsStackValue(unique, candidate)) unique.push_back(candidate);
    }
    return unique;
}

// Kiểm tra hai collection có giao nhau hay không; hàm so từng phần tử theo equality runtime và dừng ngay khi tìm thấy phần tử chung.
inline bool areStackValueCollectionsDisjoint(
    const std::vector<StackValue> &left,
    const std::vector<StackValue> &right) {
    for (const StackValue &candidate : left) {
        if (containsStackValue(right, candidate)) return false;
    }
    return true;
}

// Tạo hợp của hai dãy `StackValue`; hàm nối phần tử chưa xuất hiện để kết quả không chứa bản sao trùng theo equality runtime.
inline std::vector<StackValue> unionStackValues(
    const std::vector<StackValue> &left,
    const std::vector<StackValue> &right) {
    std::vector<StackValue> combined;
    combined.reserve(left.size() + right.size());
    combined.insert(combined.end(), left.begin(), left.end());
    combined.insert(combined.end(), right.begin(), right.end());
    return uniqueStackValues(combined);
}

// Tạo giao của hai dãy `StackValue`; hàm chỉ giữ phần tử xuất hiện ở cả hai phía và loại duplicate trong kết quả.
inline std::vector<StackValue> intersectStackValues(
    const std::vector<StackValue> &left,
    const std::vector<StackValue> &right) {
    std::vector<StackValue> intersection;
    intersection.reserve(left.size());
    for (const StackValue &candidate : left) {
        if (containsStackValue(right, candidate) &&
            !containsStackValue(intersection, candidate)) {
            intersection.push_back(candidate);
        }
    }
    return intersection;
}

// Kiểm tra điều kiện của `isUniformSortableStackValues`.
inline bool isUniformSortableStackValues(const std::vector<StackValue> &values) {
    if (values.empty()) return true;

    const bool numeric = isNumeric(values.front());
    const bool text = std::holds_alternative<std::string>(values.front());
    if (!numeric && !text) return false;

    for (const StackValue &value : values) {
        if (numeric) {
            if (!isNumeric(value)) return false;
        } else if (!std::holds_alternative<std::string>(value)) {
            return false;
        }
    }
    return true;
}

} // namespace vietvm::runtime

