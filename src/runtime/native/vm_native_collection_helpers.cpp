#include "common/vm_native_collection_helpers.h"
#include "vpp/bytecode/intrinsic.h"

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

// Dispatch các primitive list/map theo opcode đã đăng ký; handler kiểm tra đối số và cập nhật giá trị VM.
bool handleNativeCollectionFunction(Opcode opcode,
                                    const std::vector<StackValue> &args,
                                    StackValue &result,
                                    std::string &err) {
    const auto *primitive = vietvm::bytecode::intrinsicByOpcode(opcode);
    if (primitive == nullptr) return false;
    const std::string fn(primitive->name);

    if (opcode == OP_VM_LENGTH) {
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

    if (opcode == OP_VM_LIST_APPEND) {
        if (!requireNativeArgumentCount(args, fn, 2, err)) return true;
        ListHandle list;
        if (!getFirstListArgument(args, fn, list, err)) return true;
        list->elements.push_back(args[1]);
        result = make_null_value();
        return true;
    }

    if (opcode == OP_VM_LIST_REMOVE) {
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

    if (opcode == OP_VM_MAP_HAS) {
        if (!requireNativeArgumentCount(args, fn, 2, err)) return true;
        MapHandle map;
        if (!getFirstMapArgument(args, fn, map, err)) return true;
        result = make_int_value(map->entries.count(argToRawString(args[1])) != 0 ? 1 : 0);
        return true;
    }

    if (opcode == OP_VM_MAP_REMOVE) {
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

    if (opcode == OP_VM_MAP_KEYS) {
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
