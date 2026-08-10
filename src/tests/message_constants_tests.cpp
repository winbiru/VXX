#include <array>
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
    constexpr vietvm::messages::MessageDefinition sample{
        "VPP-TEST-001", "Xin chào {0}; giá trị: {1}."
    };

    expect(vietvm::messages::messageText(sample, {"V++", "42"}) == "Xin chào V++; giá trị: 42.",
           "message text substitutes numbered placeholders");
    expect(vietvm::messages::formatMessage(sample, {"V++", "42"}) ==
               "[VPP-TEST-001] Xin chào V++; giá trị: 42.",
           "formatted diagnostics retain code and text");
    expect(vietvm::messages::hasMessageCode("[VPP-TEST-001] Xin chào"),
           "formatted diagnostic is recognized as coded");
    expect(vietvm::messages::messageCodeFromFormatted("[VPP-TEST-001] Xin chào") ==
               "VPP-TEST-001",
           "formatted diagnostic exposes its machine-readable code");
    expect(!vietvm::messages::hasMessageCode("Xin chào"),
           "plain informational text is not mistaken for a diagnostic code");
}

void testCodeUniqueness() {
    constexpr std::array<std::string_view, 4> codes = {
        vietvm::messages::kCliFileOpenFailed.code,
        vietvm::messages::kLexerUnclosedString.code,
        vietvm::messages::kVmInvalidIntegerValue.code,
        vietvm::messages::kNativeArgumentCount.code
    };
    for (std::size_t i = 0; i < codes.size(); ++i) {
        expect(codes[i].rfind("VPP-", 0) == 0 && codes[i].size() > 5,
               "message codes use the VPP- prefix");
        for (std::size_t j = i + 1; j < codes.size(); ++j) {
            expect(codes[i] != codes[j], "sample message codes are unique");
        }
    }

    expect(vietvm::messages::formatMessage(vietvm::messages::kNativeArgumentCount,
                                            {"mang_http_get", "1"}) ==
               "[VPP-NATIVE-ARG-001] mang_http_get yêu cầu 1 tham số",
           "catalog entries render their public diagnostic code and text");
}

} // namespace

int main() {
    testRendering();
    testCodeUniqueness();

    if (failures != 0) {
        std::cerr << failures << " message constants test(s) failed\n";
        return 1;
    }
    std::cout << "message constants tests passed\n";
    return 0;
}
