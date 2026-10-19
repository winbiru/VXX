#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <utility>
#include "vpp/runtime/value.h"

// Use the same StackValue/Value type as your VM (adjust if you use different type)
using Value = StackValue;

// Lưu trạng thái một lời gọi hàm VM gồm biến cục bộ, tham số, receiver, defining class và địa chỉ quay về; stack frame được push/pop quanh mỗi call.
struct CallFrame {
    std::string functionName;                     // resolved function/method name for diagnostics
    std::vector<Value> args;                      // argument values, args[0] = first param
    InstanceHandle receiver;                      // bound instance for method calls
    ClassHandle methodOwnerClass;                 // class that supplied the active method
    std::vector<Value> localsVec;                 // indexed locals if used
    // A resized vector contains holes. Only slots explicitly initialized or
    // bound in this frame may shadow global variables with the same slot ID.
    std::vector<std::uint8_t> localsPresent;
    std::unordered_map<int, Value> localsMap;     // or map keyed by localId
    std::unordered_map<int, CellHandle> capturedCells; // shared cells inherited from/in use by closures
    bool localsIndexed = true;                     // true => use localsVec
    int returnPc = -1;                             // PC to return to after function ends
    int returnBytecodeOwner = -1;                  // optional owner id if multi-bytecode

    bool hasIndexedLocal(int id) const noexcept {
        return id >= 0 && static_cast<std::size_t>(id) < localsPresent.size() &&
               localsPresent[static_cast<std::size_t>(id)] != 0;
    }

    void setIndexedLocal(int id, Value value) {
        if (id < 0) return;
        const std::size_t slot = static_cast<std::size_t>(id);
        if (slot >= localsVec.size()) localsVec.resize(slot + 1);
        if (slot >= localsPresent.size()) localsPresent.resize(slot + 1, 0);
        localsVec[slot] = std::move(value);
        localsPresent[slot] = 1;
    }
};
