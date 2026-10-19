#include "common/vm_native_stdlib_helpers.h"
#include "vpp/bytecode/intrinsic.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "common/vm_native_constants.h"
#include "common/vm_native_helpers.h"
#include "vpp/core/message_constants.h"
#include "vpp/core/text.h"

namespace vietvm::helpers {

// Chuyển chuỗi UTF-8 thành `std::filesystem::path` ngay tại native boundary.
// Runtime chỉ nhận đường dẫn UTF-8 hợp lệ để Windows/Unix không diễn giải cùng
// một chuỗi đầu vào theo hai cách khác nhau.
bool nativeUtf8Path(const StackValue &value,
                    const std::string &operation,
                    std::filesystem::path &path,
                    std::string &err) {
    const std::string text = argToRawString(value);
    if (!vietvm::core::isValidUtf8(text)) {
        err = messages::formatMessage(messages::kNativePathUtf8Invalid, {operation});
        return false;
    }
    path = std::filesystem::u8path(text);
    return true;
}

namespace {

int foldPublicHash(std::uint32_t hash) noexcept {
    return static_cast<int>(hash & 0x7fffffffu);
}

template <typename Handle>
int identityHash(const Handle &handle) noexcept {
    if (handle == nullptr) return 0;
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(handle.get());
    std::uint32_t folded = static_cast<std::uint32_t>(address);
    if constexpr (sizeof(std::uintptr_t) > sizeof(std::uint32_t)) {
        folded ^= static_cast<std::uint32_t>(address >> 32u);
    }
    folded ^= 0x9e3779b9u;
    folded *= 16777619u;
    return foldPublicHash(folded);
}

// Chạy kiểu tên; hàm điều phối toàn bộ luồng xử lý của tác vụ, gọi các bước con theo thứ tự và trả mã/kết quả cuối cùng.
std::string runtimeTypeName(const StackValue &value) {
    if (std::holds_alternative<int>(value)) return "số nguyên";
    if (std::holds_alternative<AbiInteger>(value)) return "số nguyên 64-bit";
    if (std::holds_alternative<double>(value)) return "số thực";
    if (std::holds_alternative<std::string>(value)) return "chuỗi";
    if (std::holds_alternative<std::monostate>(value)) return "rỗng";
    if (std::holds_alternative<MapHandle>(value)) return "từ điển";
    if (std::holds_alternative<ListHandle>(value)) return "danh sách";
    if (std::holds_alternative<TupleHandle>(value)) return "tuple";
    if (std::holds_alternative<ClassHandle>(value)) return "lớp";
    if (std::holds_alternative<InstanceHandle>(value)) return "đối tượng";
    if (std::holds_alternative<ClosureHandle>(value)) return "hàm";
    return "không rõ";
}

// Trả tên văn bản ổn định cho nền tảng; hàm ánh xạ enum/giá trị nội bộ sang chuỗi để tooling, log hoặc test có thể hiển thị nhất quán.
std::string platformName() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "không rõ";
#endif
}

} // namespace

// Dispatch nhóm hàm native nền tảng/thư viện chuẩn; handler kiểm tra tên hàm và thực hiện filesystem, time, environment hoặc utility tương ứng.
bool handleNativeFoundationFunction(Opcode opcode,
                                    const std::vector<StackValue> &args,
                                    StackValue &result,
                                    std::string &err) {
    const auto *primitive = vietvm::bytecode::intrinsicByOpcode(opcode);
    if (primitive == nullptr) return false;
    const std::string fn(primitive->name);

    if (opcode == OP_VM_TYPE_OF) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        result = make_string_value(runtimeTypeName(args[0]));
        return true;
    }

    if (opcode == OP_VM_IDENTITY_HASH) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        if (std::holds_alternative<MapHandle>(args[0])) {
            result = make_int_value(identityHash(std::get<MapHandle>(args[0])));
        } else if (std::holds_alternative<ListHandle>(args[0])) {
            result = make_int_value(identityHash(std::get<ListHandle>(args[0])));
        } else if (std::holds_alternative<TupleHandle>(args[0])) {
            result = make_int_value(identityHash(std::get<TupleHandle>(args[0])));
        } else if (std::holds_alternative<ClassHandle>(args[0])) {
            result = make_int_value(identityHash(std::get<ClassHandle>(args[0])));
        } else if (std::holds_alternative<InstanceHandle>(args[0])) {
            result = make_int_value(identityHash(std::get<InstanceHandle>(args[0])));
        } else if (std::holds_alternative<ClosureHandle>(args[0])) {
            result = make_int_value(identityHash(std::get<ClosureHandle>(args[0])));
        } else {
            err = fn + ": chỉ nhận giá trị tham chiếu của VM";
        }
        return true;
    }

    if (opcode == OP_VM_TEN_NEN_TANG) {
        if (!requireNativeArgumentCount(args, fn, 0, err)) return true;
        result = make_string_value(platformName());
        return true;
    }

    return false;
}

} // namespace vietvm::helpers
