#include "common/vm_native_text_helpers.h"

#include <string>

#include "common/vm_native_constants.h"
#include "common/vm_native_helpers.h"
#include "vpp/core/text.h"

namespace vietvm::helpers {

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
        result = make_int_value(static_cast<int>(vietvm::core::countSubstring(text, needle)));
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnStringContains)) {
        if (!requireNativeArgumentCount(args, fn, 2, err)) return true;
        result = make_int_value(argToRawString(args[0]).find(argToRawString(args[1])) !=
                                        std::string::npos ? 1 : 0);
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnStringReplace)) {
        if (!requireNativeArgumentCount(args, fn, 3, err)) return true;
        const std::string from = argToRawString(args[1]);
        if (from.empty()) {
            err = "thay thế không nhận chuỗi cần thay rỗng";
            return true;
        }
        result = make_string_value(vietvm::core::replaceAll(
            argToRawString(args[0]), from, argToRawString(args[2])));
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnStringLower) ||
        vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnStringUpper)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        const std::string text = argToRawString(args[0]);
        result = make_string_value(vietvm::constants::matchesAnyName(
            fn, vietvm::constants::kFnStringLower) ? vietvm::core::toLowerAscii(text)
                                                    : vietvm::core::toUpperAscii(text));
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
        result = make_string_value(vietvm::core::titleAsciiWords(argToRawString(args[0])));
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnStringPalindrome)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        result = make_int_value(vietvm::core::isAsciiCaseInsensitivePalindrome(
                                    argToRawString(args[0])) ? 1 : 0);
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnStringAnagram)) {
        if (!requireNativeArgumentCount(args, fn, 2, err)) return true;
        result = make_int_value(vietvm::core::areAsciiAnagrams(
                                    argToRawString(args[0]), argToRawString(args[1])) ? 1 : 0);
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnStringCaesar)) {
        if (!requireNativeArgumentCount(args, fn, 2, err)) return true;
        if (!std::holds_alternative<int>(args[1])) {
            err = "mã hóa caesar cần khóa số nguyên";
            return true;
        }
        result = make_string_value(vietvm::core::caesarAscii(
            argToRawString(args[0]), std::get<int>(args[1])));
        return true;
    }

    return false;
}

} // namespace vietvm::helpers

