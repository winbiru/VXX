#pragma once

#include <stdexcept>
#include <string>
#include <utility>

#include "vpp/core/message_constants.h"
#include "vpp/runtime/value.h"

namespace vietvm::runtime {

// Phân loại lỗi phát sinh từ VM để caller/test có thể phân biệt lỗi thực thi
// với giá trị được chương trình chủ động `ném`.
enum class RuntimeErrorKind {
    VmFault,
    CallBoundary,
    ModuleInitialization,
};

// Biểu diễn lỗi thực thi không thể bắt bằng `bắt lỗi` của V++ 1.0. Lỗi giữ
// message ổn định cho CLI đồng thời mang loại lỗi để runtime/test kiểm tra contract.
class RuntimeError : public std::runtime_error {
public:
    explicit RuntimeError(std::string message,
                          RuntimeErrorKind kind = RuntimeErrorKind::VmFault)
        : std::runtime_error(std::move(message)), kind_(kind) {}

    RuntimeErrorKind kind() const noexcept { return kind_; }

private:
    RuntimeErrorKind kind_;
};

// Mang giá trị do câu lệnh `ném` phát sinh qua ranh giới child VM/function.
// Khi không còn `bắt lỗi`, `what()` cung cấp diagnostic host ổn định; khi còn
// handler, runtime lấy lại nguyên StackValue để bind vào biến catch.
class LanguageException : public std::exception {
public:
    explicit LanguageException(StackValue value)
        : value_(std::move(value)),
          message_(vietvm::messages::formatMessage(
              vietvm::messages::kVmUncaughtException,
              {sv_to_string(value_)})) {}

    const StackValue &value() const noexcept { return value_; }
    const char *what() const noexcept override { return message_.c_str(); }

private:
    StackValue value_;
    std::string message_;
};

} // namespace vietvm::runtime
