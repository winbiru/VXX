#pragma once

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "vpp/core/message_constants.h"
#include "vpp/runtime/debug.h"
#include "vpp/runtime/diagnostic.h"
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
// message gốc và trạng thái thực tế để bộ chẩn đoán tự suy ra nguyên nhân khi hiển thị.
class RuntimeError : public std::runtime_error {
public:
    explicit RuntimeError(std::string message,
                          RuntimeErrorKind kind = RuntimeErrorKind::VmFault,
                          RuntimeDiagnosticContext diagnosticContext = {})
        : std::runtime_error(std::move(message)),
          kind_(kind),
          diagnosticContext_(std::move(diagnosticContext)) {}

    RuntimeErrorKind kind() const noexcept { return kind_; }

    // Trả các dữ kiện thực thi đã thu được để formatter hoặc IDE tự chẩn đoán
    // mà không phải phân tích câu chữ của `what()`.
    const RuntimeDiagnosticContext &diagnosticContext() const noexcept {
        return diagnosticContext_;
    }

    // Thêm một frame từ VM đang unwind. Runtime thêm frame từ nơi lỗi phát sinh
    // ra caller, vì vậy thứ tự vector chính là thứ tự stack trace cần hiển thị.
    void addFrame(RuntimeSourceLocation frame) {
        if (!frame.valid()) return;
        frames_.push_back(std::move(frame));
    }

    // Trả danh sách frame đã thu trong quá trình unwind để CLI/debugger có thể
    // định dạng hoặc kiểm tra stack trace mà không phải phân tích chuỗi `what()`.
    const std::vector<RuntimeSourceLocation> &frames() const noexcept {
        return frames_;
    }

private:
    RuntimeErrorKind kind_;
    RuntimeDiagnosticContext diagnosticContext_;
    std::vector<RuntimeSourceLocation> frames_;
};

// Render lỗi cho người dùng từ dữ kiện có cấu trúc. CLI không hiển thị mã lỗi;
// bộ chẩn đoán tự suy ra nguyên nhân rồi chỉ nói điều đã xảy ra và cách sửa.
inline std::string formatRuntimeError(const RuntimeError &error) {
    std::string result = error.what();
    const RuntimeDiagnosticInfo diagnostic = runtimeDiagnosticInfo(
        error.diagnosticContext());
    if (!diagnostic.explanation.empty()) {
        result += "\nĐiều đã xảy ra: " + diagnostic.explanation;
    }
    if (!diagnostic.suggestion.empty()) {
        result += "\nCách sửa: " + diagnostic.suggestion;
    }
    if (error.frames().empty()) return result;
    result += "\nDấu vết lỗi:";
    const auto sameFrame = [](const RuntimeSourceLocation &left,
                              const RuntimeSourceLocation &right) {
        return left.sourceFile == right.sourceFile &&
               left.moduleIdentity == right.moduleIdentity &&
               left.functionName == right.functionName &&
               left.line == right.line && left.column == right.column;
    };
    for (std::size_t index = 0; index < error.frames().size();) {
        const RuntimeSourceLocation &frame = error.frames()[index];
        std::size_t repeat = 1;
        while (index + repeat < error.frames().size() &&
               sameFrame(frame, error.frames()[index + repeat])) {
            ++repeat;
        }
        result += "\n  ở ";
        result += frame.functionName.empty() ? "<cấp cao nhất>" : frame.functionName;
        result += " (";
        result += frame.sourceFile.empty() ? "<bộ nhớ>" : frame.sourceFile;
        result += ":" + std::to_string(frame.line) + ":" +
                  std::to_string(frame.column) + ")";
        if (!frame.moduleIdentity.empty()) {
            result += " [mô đun=" + frame.moduleIdentity + "]";
        }
        if (repeat > 1) {
            result += " [lặp lại " + std::to_string(repeat) + " khung]";
        }
        index += repeat;
    }
    return result;
}

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
