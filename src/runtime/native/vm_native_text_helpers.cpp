#include "common/vm_native_text_helpers.h"

#include <string>

#include "common/vm_native_constants.h"
#include "common/vm_native_helpers.h"
#include "vpp/core/text.h"

namespace vietvm::helpers {

// Dispatch các hàm native xử lý chuỗi/văn bản; handler lấy đối số từ stack, gọi tiện ích text tương ứng rồi trả `StackValue` kết quả.
bool handleNativeTextFunction(const std::string &fn,
                              const std::vector<StackValue> &args,
                              StackValue &result,
                              std::string &err) {
    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnStringCountChar)) {
        if (!requireNativeArgumentCount(args, fn, 2, err)) return true;
        const std::string text = argToRawString(args[0]);
        const std::string needle = argToRawString(args[1]);
        if (needle.empty()) {
            err = "đếm ký tự cần ký tự không rỗng";
            return true;
        }
        result = make_int_value(static_cast<int>(
            vietvm::core::countVietnameseNormalizedSubstring(text, needle)));
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnStringContains)) {
        if (!requireNativeArgumentCount(args, fn, 2, err)) return true;
        result = make_int_value(vietvm::core::containsVietnameseNormalizedSubstring(
                                    argToRawString(args[0]),
                                    argToRawString(args[1])) ? 1 : 0);
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnStringReplace)) {
        if (!requireNativeArgumentCount(args, fn, 3, err)) return true;
        const std::string from = argToRawString(args[1]);
        if (from.empty()) {
            err = "thay thế không nhận chuỗi cần thay rỗng";
            return true;
        }
        result = make_string_value(vietvm::core::replaceVietnameseNormalizedAll(
            argToRawString(args[0]), from, argToRawString(args[2])));
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnStringSlice)) {
        if (!requireNativeArgumentCount(args, fn, 3, err)) return true;
        int start = 0;
        int count = 0;
        if (!parseIntArgFromStack(args[1], fn, "vị trí bắt đầu", start, err) ||
            !parseIntArgFromStack(args[2], fn, "độ dài", count, err)) {
            return true;
        }
        if (start < 0 || count < 0) {
            err = "cắt chuỗi không nhận vị trí hoặc độ dài âm";
            return true;
        }
        result = make_string_value(vietvm::core::utf8CodePointSlice(
            argToRawString(args[0]), static_cast<std::size_t>(start),
            static_cast<std::size_t>(count)));
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnStringLower) ||
        vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnStringUpper)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        const std::string text = argToRawString(args[0]);
        result = make_string_value(vietvm::constants::matchesAnyName(
            fn, vietvm::constants::kFnStringLower)
            ? vietvm::core::toLowerUtf8Vietnamese(text)
            : vietvm::core::toUpperUtf8Vietnamese(text));
        return true;
    }

    if (vietvm::constants::matchesAnyName(
            fn, vietvm::constants::kFnStringNormalizeUnicode)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        result = make_string_value(vietvm::core::normalizeUtf8VietnameseNfc(
            argToRawString(args[0])));
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnStringTrimSpaces)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        result = make_string_value(vietvm::core::joinWithSpaces(
            vietvm::core::splitAsciiWords(argToRawString(args[0]))));
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnStringWordCount)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        result = make_int_value(static_cast<int>(
            vietvm::core::splitAsciiWords(argToRawString(args[0])).size()));
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnStringLongestWord)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        result = make_string_value(vietvm::core::longestAsciiWord(argToRawString(args[0])));
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnStringTitle)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        result = make_string_value(
            vietvm::core::titleVietnameseWords(argToRawString(args[0])));
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnStringPalindrome)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        result = make_int_value(
            vietvm::core::isVietnameseCaseInsensitivePalindrome(
                argToRawString(args[0])) ? 1 : 0);
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnStringAnagram)) {
        if (!requireNativeArgumentCount(args, fn, 2, err)) return true;
        result = make_int_value(vietvm::core::areVietnameseAnagrams(
                                    argToRawString(args[0]),
                                    argToRawString(args[1])) ? 1 : 0);
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnStringCaesar)) {
        if (!requireNativeArgumentCount(args, fn, 2, err)) return true;
        int key = 0;
        if (!parseIntArgFromStack(args[1], fn, "khóa", key, err)) return true;
        result = make_string_value(vietvm::core::caesarAscii(
            argToRawString(args[0]), key));
        return true;
    }

    return false;
}

} // namespace vietvm::helpers
