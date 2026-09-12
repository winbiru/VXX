#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "vpp/runtime/value.h"

namespace vietvm::runtime {

inline bool containsStackValue(const std::vector<StackValue> &values,
                               const StackValue &target) {
    for (const StackValue &value : values) {
        if (sameStackValue(value, target)) return true;
    }
    return false;
}

inline std::optional<std::size_t> findStackValueIndex(
    const std::vector<StackValue> &values,
    const StackValue &target) {
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (sameStackValue(values[index], target)) return index;
    }
    return std::nullopt;
}

inline std::vector<StackValue> uniqueStackValues(
    const std::vector<StackValue> &values) {
    std::vector<StackValue> unique;
    unique.reserve(values.size());
    for (const StackValue &candidate : values) {
        if (!containsStackValue(unique, candidate)) unique.push_back(candidate);
    }
    return unique;
}

inline bool areStackValueCollectionsDisjoint(
    const std::vector<StackValue> &left,
    const std::vector<StackValue> &right) {
    for (const StackValue &candidate : left) {
        if (containsStackValue(right, candidate)) return false;
    }
    return true;
}

inline std::vector<StackValue> unionStackValues(
    const std::vector<StackValue> &left,
    const std::vector<StackValue> &right) {
    std::vector<StackValue> combined;
    combined.reserve(left.size() + right.size());
    combined.insert(combined.end(), left.begin(), left.end());
    combined.insert(combined.end(), right.begin(), right.end());
    return uniqueStackValues(combined);
}

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

