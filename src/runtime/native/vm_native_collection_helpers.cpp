#include "common/vm_native_collection_helpers.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "common/vm_native_constants.h"
#include "common/vm_native_helpers.h"
#include "vpp/core/message_constants.h"
#include "vpp/core/text.h"
#include "vpp/runtime/collection.h"

namespace vietvm::helpers {

// Dispatch các hàm native thao tác list/map/tập hợp theo tên; handler kiểm tra đối số, thực hiện phép toán collection và đẩy kết quả trở lại stack.
bool handleNativeCollectionFunction(const std::string &fn,
                                    const std::vector<StackValue> &args,
                                    StackValue &result,
                                    std::string &err) {
    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnLength)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        if (std::holds_alternative<std::string>(args[0])) {
            result = make_int_value(static_cast<int>(vietvm::core::utf8CodePointCount(
                std::get<std::string>(args[0]))));
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
        err = messages::messageText(messages::kNativeLengthTypeInvalid);
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
            err = messages::messageText(messages::kNativeListIndexOutOfRange);
            return true;
        }
        result = list->elements[static_cast<std::size_t>(index)];
        list->elements.erase(list->elements.begin() + index);
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
        err = messages::messageText(messages::kNativeToTupleTypeInvalid);
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
        for (const auto &entry : map->entries) {
            keys.push_back(make_string_value(entry.first));
        }
        result = make_list_value(std::move(keys));
        return true;
    }

    return false;
}

} // namespace vietvm::helpers
