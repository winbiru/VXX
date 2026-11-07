#pragma once

#include <cstdint>
#include <cstring>
#include "../vm/instruction.h"

// Phép bit i32: dịch lấy 5 bit thấp của số bước; dịch phải giữ dấu.
inline std::int32_t evaluateI32Bits(int op, std::int32_t left, std::int32_t right) {
    const auto a = static_cast<std::uint32_t>(left);
    const auto b = static_cast<std::uint32_t>(right);
    const auto count = b & 31u;
    std::uint32_t bits = 0;
    switch (op) {
        case OP_BIT_VA: bits = a & b; break;
        case OP_BIT_HOAC: bits = a | b; break;
        case OP_BIT_XOR: bits = a ^ b; break;
        case OP_DICH_TRAI: bits = a << count; break;
        case OP_DICH_PHAI:
            bits = a >> count;
            if (left < 0 && count != 0) bits |= ~std::uint32_t{0} << (32u - count);
            break;
        default: break;
    }
    std::int32_t result;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}
