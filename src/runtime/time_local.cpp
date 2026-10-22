#include "vpp/runtime/time_local.h"

#include <array>
#include <cstdint>
#include <ctime>
#include <limits>

#include "vpp/runtime/error.h"

#if defined(VPP_FILE_MACHINE_ASM) && (defined(__APPLE__) || defined(__linux__))
extern "C" std::tm *vpp_machine_localtime_r(const std::time_t *, std::tm *) noexcept;
#endif

namespace vietvm::runtime {
namespace {

// Stable wire layout, independent of the host's struct tm padding/size.
// Windows has no tm_gmtoff; the V++ module calculates its offset using UTC.
void putI32(std::array<std::uint8_t, 64> &out, std::size_t offset, int value) {
    const auto bits = static_cast<std::uint32_t>(value);
    for (unsigned i = 0; i < 4; ++i)
        out[offset + i] = static_cast<std::uint8_t>(bits >> (i * 8));
}

void putI64(std::array<std::uint8_t, 64> &out, std::size_t offset,
            std::int64_t value) {
    const auto bits = static_cast<std::uint64_t>(value);
    for (unsigned i = 0; i < 8; ++i)
        out[offset + i] = static_cast<std::uint8_t>(bits >> (i * 8));
}

bool readLocal(std::time_t epoch, std::tm &out) noexcept {
#if defined(_WIN32)
    return ::localtime_s(&out, &epoch) == 0;
#elif defined(VPP_FILE_MACHINE_ASM) && (defined(__APPLE__) || defined(__linux__))
    // Assembly bridge calls the platform's public reentrant time API.
    return vpp_machine_localtime_r(&epoch, &out) != nullptr;
#else
    return ::localtime_r(&epoch, &out) != nullptr;
#endif
}

} // namespace

bool isTimeLocalPrimitive(int opcode) noexcept {
    return opcode == OP_VM_MACHINE_TIME_LOCAL;
}

StackValue executeTimeLocalPrimitive(
    Opcode opcode, const std::vector<StackValue> &arguments,
    const std::unordered_set<std::string> &capabilities) {
    if (!isTimeLocalPrimitive(opcode)) {
        throw RuntimeError("máy localtime: opcode không hợp lệ", RuntimeErrorKind::CallBoundary);
    }
    if (capabilities.count("system.time.local") == 0) {
        throw RuntimeError("máy localtime: thiếu quyền system.time.local",
                           RuntimeErrorKind::CallBoundary);
    }
    if (arguments.size() != 1) {
        throw RuntimeError("máy localtime: cần đúng 8 byte epoch",
                           RuntimeErrorKind::CallBoundary);
    }
    const auto *list = std::get_if<ListHandle>(&arguments[0]);
    if (!list || !*list || (*list)->elements.size() != 8) {
        throw RuntimeError("máy localtime: epoch phải là danh sách 8 byte",
                           RuntimeErrorKind::CallBoundary);
    }
    std::uint64_t bits = 0;
    for (unsigned i = 0; i < 8; ++i) {
        const auto *byte = std::get_if<int>(&(*list)->elements[i]);
        if (!byte || *byte < 0 || *byte > 255) {
            throw RuntimeError("máy localtime: byte không hợp lệ",
                               RuntimeErrorKind::CallBoundary);
        }
        bits |= static_cast<std::uint64_t>(*byte) << (i * 8);
    }
    static_assert(sizeof(std::time_t) == 8 && std::numeric_limits<std::time_t>::is_signed,
                  "V++ localtime ABI requires signed 64-bit time_t");
    // Avoid implementation-defined unsigned->signed conversion at INT64_MIN.
    const std::int64_t signedSeconds = (bits & (std::uint64_t{1} << 63)) != 0
        ? -1 - static_cast<std::int64_t>(~bits)
        : static_cast<std::int64_t>(bits);
    const auto epoch = static_cast<std::time_t>(signedSeconds);
    std::tm local{};
    if (!readLocal(epoch, local)) {
        throw RuntimeError("máy localtime: OS không thể đổi epoch",
                           RuntimeErrorKind::CallBoundary);
    }
    std::array<std::uint8_t, 64> wire{};
    putI32(wire, 0, local.tm_sec);
    putI32(wire, 4, local.tm_min);
    putI32(wire, 8, local.tm_hour);
    putI32(wire, 12, local.tm_mday);
    putI32(wire, 16, local.tm_mon);
    putI32(wire, 20, local.tm_year);
    putI32(wire, 24, local.tm_wday);
    putI32(wire, 28, local.tm_yday);
    putI32(wire, 32, local.tm_isdst);
#if !defined(_WIN32)
    putI64(wire, 40, static_cast<std::int64_t>(local.tm_gmtoff));
#endif
    std::vector<StackValue> bytes;
    bytes.reserve(wire.size());
    for (std::uint8_t byte : wire) bytes.push_back(make_int_value(byte));
    return make_list_value(std::move(bytes));
}

} // namespace vietvm::runtime
