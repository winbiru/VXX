#pragma once
#include <vector>
#include <unordered_map>
#include "vpp/runtime/value.h"

// Use the same StackValue/Value type as your VM (adjust if you use different type)
using Value = StackValue;

// Lưu trạng thái một lời gọi hàm VM gồm biến cục bộ, tham số, receiver, defining class và địa chỉ quay về; stack frame được push/pop quanh mỗi call.
struct CallFrame {
    std::vector<Value> args;                      // argument values, args[0] = first param
    InstanceHandle receiver;                      // bound instance for method calls
    ClassHandle methodOwnerClass;                 // class that supplied the active method
    std::vector<Value> localsVec;                 // indexed locals if used
    std::unordered_map<int, Value> localsMap;     // or map keyed by localId
    bool localsIndexed = true;                     // true => use localsVec
    int returnPc = -1;                             // PC to return to after function ends
    int returnBytecodeOwner = -1;                  // optional owner id if multi-bytecode
};
