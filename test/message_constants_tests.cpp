#include <iostream>
#include <string>
#include <string_view>

#include "vpp/core/message_constants.h"

namespace {

int failures = 0;

void expect(bool condition, const std::string &message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

void testRendering() {
    constexpr std::string_view sample = "Xin chào {0}; giá trị: {1}.";

    expect(vietvm::messages::messageText(sample, {"V++", "42"}) ==
               "Xin chào V++; giá trị: 42.",
           "message text substitutes numbered placeholders");
    expect(vietvm::messages::formatMessage(sample, {"V++", "42"}) ==
               "Xin chào V++; giá trị: 42.",
           "formatted message contains only the Vietnamese text");
}

void testCentralizedVietnameseCatalog() {
    expect(vietvm::messages::messageText(vietvm::messages::kCliFileOpenFailed,
                                         {"demo.vi"}) ==
               "Không thể mở tệp: demo.vi",
           "CLI file diagnostic uses the centralized Vietnamese catalog");
    expect(vietvm::messages::messageText(vietvm::messages::kSyntaxExpectedOpeningParen) ==
               "extractParens: thiếu dấu '(' mở",
           "compiler syntax diagnostic uses the centralized Vietnamese catalog");
    expect(vietvm::messages::messageText(vietvm::messages::kVmInvalidListIndex) ==
               "Lỗi: chỉ số danh sách không hợp lệ",
           "runtime collection diagnostic uses the centralized Vietnamese catalog");
    expect(vietvm::messages::formatMessage(vietvm::messages::kNativeArgumentCount,
                                           {"mang_http_get", "1"}) ==
               "mang_http_get yêu cầu 1 tham số",
           "native diagnostics render without a diagnostic-code prefix");
}

} // namespace

int main() {
    testRendering();
    testCentralizedVietnameseCatalog();

    if (failures != 0) {
        std::cerr << failures << " message constants test(s) failed\n";
        return 1;
    }
    std::cout << "message constants tests passed\n";
    return 0;
}
