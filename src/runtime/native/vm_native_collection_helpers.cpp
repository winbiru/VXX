#include "common/vm_native_collection_helpers.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "common/vm_native_constants.h"
#include "common/vm_native_helpers.h"
#include "vpp/runtime/collection.h"

namespace vietvm::helpers {

bool handleNativeCollectionFunction(const std::string &fn,
                                    const std::vector<StackValue> &args,
                                    StackValue &result,
                                    std::string &err) {
    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnLength)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        if (std::holds_alternative<std::string>(args[0])) {
            result = make_int_value(static_cast<int>(std::get<std::string>(args[0]).size()));
            return true;
        }
        if (std::holds_alternative<ListHandle>(args[0])) {
            const ListHandle &list = std::get<ListHandle>(args[0]);
            result = make_int_value(list == nullptr ? 0 : static_cast<int>(list->elements.size()));
            return true;
        }
        if (std::holds_alternative<TupleHandle>(args[0])) {
            const TupleHandle &tuple = std::get<TupleHandle>(args[0]);
            result = make_int_value(tuple == nullptr ? 0 : static_cast<int>(tuple->elements.size()));
            return true;
        }
        err = "độ dài chỉ nhận chuỗi, danh sách hoặc bộ";
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnListAppend)) {
        if (!requireNativeArgumentCount(args, fn, 2, err)) return true;
        ListHandle list;
        if (!getFirstListArgument(args, fn, list, err)) return true;
        list->elements.push_back(args[1]);
        result = make_null_value();
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnListRemoveAt)) {
        if (!requireNativeArgumentCount(args, fn, 2, err)) return true;
        ListHandle list;
        int index = 0;
        if (!getFirstListArgument(args, fn, list, err) ||
            !getNonNegativeListIndex(args[1], index, err)) return true;
        if (static_cast<std::size_t>(index) >= list->elements.size()) {
            err = "chỉ số danh sách vượt phạm vi";
            return true;
        }
        result = list->elements[static_cast<std::size_t>(index)];
        list->elements.erase(list->elements.begin() + index);
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnListReverse)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        if (std::holds_alternative<std::string>(args[0])) {
            std::string reversed = std::get<std::string>(args[0]);
            std::reverse(reversed.begin(), reversed.end());
            result = make_string_value(reversed);
            return true;
        }
        ListHandle list;
        if (!getFirstListArgument(args, fn, list, err)) return true;
        std::reverse(list->elements.begin(), list->elements.end());
        result = make_null_value();
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnListFindIndex)) {
        if (!requireNativeArgumentCount(args, fn, 2, err)) return true;
        ListHandle list;
        if (!getFirstListArgument(args, fn, list, err)) return true;
        const auto index = vietvm::runtime::findStackValueIndex(list->elements, args[1]);
        result = make_int_value(index.has_value() ? static_cast<int>(*index) : -1);
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnListUnique)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        ListHandle list;
        if (!getFirstListArgument(args, fn, list, err)) return true;
        list->elements = vietvm::runtime::uniqueStackValues(list->elements);
        result = make_null_value();
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnListSort)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        ListHandle list;
        if (!getFirstListArgument(args, fn, list, err)) return true;
        if (!vietvm::runtime::isUniformSortableStackValues(list->elements)) {
            err = "sắp xếp chỉ hỗ trợ danh sách toàn số hoặc toàn chuỗi";
            return true;
        }
        std::stable_sort(list->elements.begin(), list->elements.end(),
                         [](const StackValue &left, const StackValue &right) {
                             bool valid = false;
                             return stackValueLess(left, right, valid);
                         });
        result = make_null_value();
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnListSum)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        ListHandle list;
        if (!getFirstListArgument(args, fn, list, err)) return true;
        double total = 0.0;
        bool hasFloat = false;
        for (const StackValue &value : list->elements) {
            if (!isNumeric(value)) {
                err = "tổng danh sách chỉ hỗ trợ phần tử số";
                return true;
            }
            hasFloat = hasFloat || std::holds_alternative<double>(value);
            total += toDouble(value);
        }
        result = hasFloat ? make_float_value(total) : make_int_value(static_cast<int>(total));
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnListMin) ||
        vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnListMax)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        ListHandle list;
        if (!getFirstListArgument(args, fn, list, err)) return true;
        if (list->elements.empty()) {
            err = fn + " không nhận danh sách rỗng";
            return true;
        }
        if (!vietvm::runtime::isUniformSortableStackValues(list->elements)) {
            err = fn + " chỉ hỗ trợ danh sách toàn số hoặc toàn chuỗi";
            return true;
        }
        const bool wantMax = vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnListMax);
        StackValue selected = list->elements.front();
        for (std::size_t index = 1; index < list->elements.size(); ++index) {
            bool valid = false;
            const bool less = stackValueLess(list->elements[index], selected, valid);
            if (!valid) {
                err = fn + " chỉ hỗ trợ danh sách toàn số hoặc toàn chuỗi";
                return true;
            }
            if ((wantMax && !less && !sameStackValue(list->elements[index], selected)) ||
                (!wantMax && less)) {
                selected = list->elements[index];
            }
        }
        result = selected;
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnSetFromList)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        ListHandle source;
        if (!getListArgument(args, 0, fn, source, err)) return true;
        result = make_list_value(vietvm::runtime::uniqueStackValues(source->elements));
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnToTuple)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        if (std::holds_alternative<ListHandle>(args[0])) {
            const ListHandle &list = std::get<ListHandle>(args[0]);
            result = make_tuple_value(list == nullptr ? std::vector<StackValue>{} : list->elements);
            return true;
        }
        if (std::holds_alternative<TupleHandle>(args[0])) {
            result = args[0];
            return true;
        }
        err = "thành bộ chỉ nhận danh sách hoặc bộ";
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnToList)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        if (std::holds_alternative<TupleHandle>(args[0])) {
            const TupleHandle &tuple = std::get<TupleHandle>(args[0]);
            result = make_list_value(tuple == nullptr ? std::vector<StackValue>{} : tuple->elements);
            return true;
        }
        if (std::holds_alternative<ListHandle>(args[0])) {
            const ListHandle &list = std::get<ListHandle>(args[0]);
            result = make_list_value(list == nullptr ? std::vector<StackValue>{} : list->elements);
            return true;
        }
        err = "thành danh sách chỉ nhận danh sách hoặc bộ";
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnSetUnion) ||
        vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnSetIntersection) ||
        vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnSetDisjoint)) {
        if (!requireNativeArgumentCount(args, fn, 2, err)) return true;
        ListHandle left;
        ListHandle right;
        if (!getListArgument(args, 0, fn, left, err) ||
            !getListArgument(args, 1, fn, right, err)) return true;

        if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnSetDisjoint)) {
            result = make_int_value(vietvm::runtime::areStackValueCollectionsDisjoint(
                                        left->elements, right->elements) ? 1 : 0);
            return true;
        }
        if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnSetUnion)) {
            result = make_list_value(vietvm::runtime::unionStackValues(
                left->elements, right->elements));
            return true;
        }
        result = make_list_value(vietvm::runtime::intersectStackValues(
            left->elements, right->elements));
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnMapGet)) {
        if (!requireNativeArgumentCount(args, fn, 3, err)) return true;
        MapHandle map;
        if (!getFirstMapArgument(args, fn, map, err)) return true;
        const std::string key = argToRawString(args[1]);
        const auto found = map->entries.find(key);
        result = found == map->entries.end() ? args[2] : found->second;
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnMapSet)) {
        if (!requireNativeArgumentCount(args, fn, 3, err)) return true;
        MapHandle map;
        if (!getFirstMapArgument(args, fn, map, err)) return true;
        map->entries[argToRawString(args[1])] = args[2];
        result = make_null_value();
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnMapHasKey)) {
        if (!requireNativeArgumentCount(args, fn, 2, err)) return true;
        MapHandle map;
        if (!getFirstMapArgument(args, fn, map, err)) return true;
        result = make_int_value(map->entries.count(argToRawString(args[1])) != 0 ? 1 : 0);
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnMapRemove)) {
        if (!requireNativeArgumentCount(args, fn, 2, err)) return true;
        MapHandle map;
        if (!getFirstMapArgument(args, fn, map, err)) return true;
        const std::string key = argToRawString(args[1]);
        const auto found = map->entries.find(key);
        if (found == map->entries.end()) {
            result = make_null_value();
        } else {
            result = found->second;
            map->entries.erase(found);
        }
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnMapKeys)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        MapHandle map;
        if (!getFirstMapArgument(args, fn, map, err)) return true;
        std::vector<StackValue> keys;
        keys.reserve(map->entries.size());
        for (const auto &entry : map->entries) keys.push_back(make_string_value(entry.first));
        result = make_list_value(std::move(keys));
        return true;
    }

    return false;
}

} // namespace vietvm::helpers
