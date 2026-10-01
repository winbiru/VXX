#include "common/vm_native_format_helpers.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "common/vm_native_constants.h"
#include "common/vm_native_helpers.h"
#include "vpp/core/text.h"

namespace vietvm::helpers {
namespace {

constexpr std::size_t kFormatResourceLimit = 1024u * 1024u;

struct FormatSpec {
    std::optional<std::size_t> argumentIndex;
    std::string flags;
    std::optional<std::size_t> width;
    std::optional<std::size_t> precision;
    char conversion = '\0';
};

struct FormatPiece {
    std::string literal;
    std::optional<FormatSpec> spec;
};

StackValue formatResult(std::string value, std::string error = {}) {
    MapValue map;
    map.entries.emplace("giá trị", make_string_value(std::move(value)));
    map.entries.emplace("lỗi", make_string_value(std::move(error)));
    return make_map_value(std::move(map));
}

bool isFormatFlag(char c) noexcept {
    return c == '-' || c == '+' || c == '0' || c == ' ' || c == ',' || c == '(';
}

bool isKnownConversion(char c) noexcept {
    switch (c) {
        case 's': case 'b': case 'c': case 'd': case 'o': case 'x': case 'X':
        case 'f': case 'e': case 'E': case 'g': case 'G': case '%': case 'n':
            return true;
        default:
            return false;
    }
}

bool hasFlag(const FormatSpec &spec, char flag) noexcept {
    return spec.flags.find(flag) != std::string::npos;
}

bool parseBoundedUnsigned(const std::string &pattern,
                          std::size_t begin,
                          std::size_t end,
                          std::size_t &value) noexcept {
    value = 0;
    if (begin == end) return false;
    for (std::size_t index = begin; index < end; ++index) {
        const char c = pattern[index];
        if (c < '0' || c > '9') return false;
        const std::size_t digit = static_cast<std::size_t>(c - '0');
        if (value > (kFormatResourceLimit - digit) / 10u) return false;
        value = value * 10u + digit;
    }
    return true;
}

std::string validateFlags(const FormatSpec &spec) {
    const auto unsupported = [&](const std::string &allowed) {
        for (char flag : spec.flags) {
            if (allowed.find(flag) == std::string::npos) return true;
        }
        return false;
    };

    if (hasFlag(spec, '-') && hasFlag(spec, '0')) return "FORMAT_FLAG_MISMATCH";
    if (hasFlag(spec, '+') && hasFlag(spec, ' ')) return "FORMAT_FLAG_MISMATCH";
    if (hasFlag(spec, '(') && (hasFlag(spec, '+') || hasFlag(spec, ' '))) {
        return "FORMAT_FLAG_MISMATCH";
    }
    if ((hasFlag(spec, '-') || hasFlag(spec, '0')) && !spec.width.has_value()) {
        return "FORMAT_MISSING_WIDTH";
    }

    switch (spec.conversion) {
        case 's': case 'b':
            if (unsupported("-")) return "FORMAT_FLAG_MISMATCH";
            break;
        case 'c':
            if (unsupported("-") || spec.precision.has_value()) return "FORMAT_FLAG_MISMATCH";
            break;
        case 'd':
            if (unsupported("-+0 ,(") || spec.precision.has_value()) {
                return "FORMAT_FLAG_MISMATCH";
            }
            break;
        case 'o': case 'x': case 'X':
            if (unsupported("-0") || spec.precision.has_value()) {
                return "FORMAT_FLAG_MISMATCH";
            }
            break;
        case 'f':
            if (unsupported("-+0 ,(")) return "FORMAT_FLAG_MISMATCH";
            break;
        case 'e': case 'E':
            if (unsupported("-+0 (")) return "FORMAT_FLAG_MISMATCH";
            break;
        case 'g': case 'G':
            if (unsupported("-+0 ,(")) return "FORMAT_FLAG_MISMATCH";
            break;
        case '%':
            if (unsupported("-") || spec.precision.has_value()) return "FORMAT_FLAG_MISMATCH";
            break;
        case 'n':
            if (!spec.flags.empty() || spec.width.has_value() || spec.precision.has_value()) {
                return "FORMAT_FLAG_MISMATCH";
            }
            break;
        default:
            return "FORMAT_UNKNOWN_CONVERSION";
    }
    return {};
}

std::string parsePattern(const std::string &pattern,
                         std::vector<FormatPiece> &pieces) {
    if (pattern.size() > kFormatResourceLimit) return "FORMAT_RESOURCE_LIMIT";
    std::size_t cursor = 0;
    bool sawExplicit = false;
    bool sawImplicit = false;
    while (cursor < pattern.size()) {
        const std::size_t percent = pattern.find('%', cursor);
        if (percent == std::string::npos) {
            pieces.push_back({pattern.substr(cursor), std::nullopt});
            break;
        }
        if (percent > cursor) {
            pieces.push_back({pattern.substr(cursor, percent - cursor), std::nullopt});
        }

        std::size_t pos = percent + 1u;
        if (pos >= pattern.size()) return "FORMAT_INVALID";
        FormatSpec spec;

        const std::size_t numberStart = pos;
        while (pos < pattern.size() && pattern[pos] >= '0' && pattern[pos] <= '9') ++pos;
        if (pos > numberStart && pos < pattern.size() && pattern[pos] == '$') {
            std::size_t oneBased = 0;
            if (!parseBoundedUnsigned(pattern, numberStart, pos, oneBased) || oneBased == 0) {
                return "FORMAT_INVALID";
            }
            spec.argumentIndex = oneBased - 1u;
            ++pos;
        } else {
            pos = numberStart;
        }

        while (pos < pattern.size()) {
            const char c = pattern[pos];
            if (isFormatFlag(c)) {
                if (spec.flags.find(c) != std::string::npos) return "FORMAT_DUPLICATE_FLAG";
                spec.flags.push_back(c);
                ++pos;
                continue;
            }
            if (c == '#' || c == '<') return "FORMAT_UNKNOWN_FLAG";
            break;
        }

        const std::size_t widthStart = pos;
        while (pos < pattern.size() && pattern[pos] >= '0' && pattern[pos] <= '9') ++pos;
        if (pos > widthStart) {
            std::size_t width = 0;
            if (!parseBoundedUnsigned(pattern, widthStart, pos, width)) return "FORMAT_WIDTH";
            spec.width = width;
        }

        if (pos < pattern.size() && pattern[pos] == '.') {
            ++pos;
            const std::size_t precisionStart = pos;
            while (pos < pattern.size() && pattern[pos] >= '0' && pattern[pos] <= '9') ++pos;
            std::size_t precision = 0;
            if (precisionStart == pos ||
                !parseBoundedUnsigned(pattern, precisionStart, pos, precision)) {
                return "FORMAT_PRECISION";
            }
            spec.precision = precision;
        }

        if (pos >= pattern.size()) return "FORMAT_INVALID";
        spec.conversion = pattern[pos++];
        if (!isKnownConversion(spec.conversion)) return "FORMAT_UNKNOWN_CONVERSION";
        if ((spec.conversion == '%' || spec.conversion == 'n') && spec.argumentIndex.has_value()) {
            return "FORMAT_INVALID";
        }

        const std::string flagError = validateFlags(spec);
        if (!flagError.empty()) return flagError;

        if (spec.conversion != '%' && spec.conversion != 'n') {
            if (spec.argumentIndex.has_value()) sawExplicit = true;
            else sawImplicit = true;
            if (sawExplicit && sawImplicit) return "FORMAT_INVALID";
        }
        pieces.push_back({{}, std::move(spec)});
        cursor = pos;
    }
    return {};
}

std::string padText(std::string value,
                    const FormatSpec &spec,
                    bool numeric = false,
                    std::size_t prefixBytes = 0,
                    std::size_t suffixBytes = 0) {
    if (!spec.width.has_value()) return value;
    const std::size_t logicalLength = numeric
        ? value.size()
        : vietvm::core::utf8CodePointCount(value);
    if (*spec.width <= logicalLength) return value;
    const std::size_t amount = *spec.width - logicalLength;
    if (hasFlag(spec, '-')) {
        value.append(amount, ' ');
        return value;
    }
    if (numeric && hasFlag(spec, '0')) {
        const std::size_t safeSuffix = std::min(suffixBytes, value.size());
        const std::size_t insertAt = std::min(prefixBytes, value.size() - safeSuffix);
        value.insert(insertAt, amount, '0');
        return value;
    }
    value.insert(0, amount, ' ');
    return value;
}

std::string groupDecimalDigits(const std::string &digits) {
    std::string grouped;
    grouped.reserve(digits.size() + digits.size() / 3u);
    const std::size_t first = digits.size() % 3u == 0 ? 3u : digits.size() % 3u;
    for (std::size_t index = 0; index < digits.size(); ++index) {
        if (index != 0 && (index == first || (index > first && (index - first) % 3u == 0))) {
            grouped.push_back(',');
        }
        grouped.push_back(digits[index]);
    }
    return grouped;
}

std::string groupFloatBody(std::string body) {
    const std::size_t exponent = body.find_first_of("eE");
    const std::size_t decimal = body.find('.');
    const std::size_t endInteger = std::min(
        decimal == std::string::npos ? body.size() : decimal,
        exponent == std::string::npos ? body.size() : exponent);
    body.replace(0, endInteger, groupDecimalDigits(body.substr(0, endInteger)));
    return body;
}

std::string unsignedBase(std::uint64_t value, unsigned base, bool upper) {
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (value == 0) return "0";
    std::string output;
    while (value != 0) {
        output.push_back(digits[value % base]);
        value /= base;
    }
    std::reverse(output.begin(), output.end());
    return output;
}

std::string applyNumericSign(std::string body,
                             bool negative,
                             const FormatSpec &spec) {
    std::size_t prefix = 0;
    std::size_t suffix = 0;
    if (negative && hasFlag(spec, '(')) {
        body.insert(body.begin(), '(');
        body.push_back(')');
        prefix = 1;
        suffix = 1;
    } else if (negative) {
        body.insert(body.begin(), '-');
        prefix = 1;
    } else if (hasFlag(spec, '+')) {
        body.insert(body.begin(), '+');
        prefix = 1;
    } else if (hasFlag(spec, ' ')) {
        body.insert(body.begin(), ' ');
        prefix = 1;
    }
    return padText(std::move(body), spec, true, prefix, suffix);
}

bool appendUtf8(std::string &output, std::uint32_t codePoint) {
    if (codePoint > 0x10ffffu || (codePoint >= 0xd800u && codePoint <= 0xdfffu)) return false;
    if (codePoint <= 0x7fu) {
        output.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7ffu) {
        output.push_back(static_cast<char>(0xc0u | (codePoint >> 6u)));
        output.push_back(static_cast<char>(0x80u | (codePoint & 0x3fu)));
    } else if (codePoint <= 0xffffu) {
        output.push_back(static_cast<char>(0xe0u | (codePoint >> 12u)));
        output.push_back(static_cast<char>(0x80u | ((codePoint >> 6u) & 0x3fu)));
        output.push_back(static_cast<char>(0x80u | (codePoint & 0x3fu)));
    } else {
        output.push_back(static_cast<char>(0xf0u | (codePoint >> 18u)));
        output.push_back(static_cast<char>(0x80u | ((codePoint >> 12u) & 0x3fu)));
        output.push_back(static_cast<char>(0x80u | ((codePoint >> 6u) & 0x3fu)));
        output.push_back(static_cast<char>(0x80u | (codePoint & 0x3fu)));
    }
    return true;
}

std::string formatFloat(double number, const FormatSpec &spec) {
    const bool negative = std::signbit(number);
    const double magnitude = negative ? -number : number;
    std::string body;
    if (std::isnan(magnitude)) {
        body = (spec.conversion == 'E' || spec.conversion == 'G') ? "NAN" : "NaN";
    } else if (std::isinf(magnitude)) {
        body = (spec.conversion == 'E' || spec.conversion == 'G') ? "INFINITY" : "Infinity";
    } else {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        if (spec.conversion == 'E' || spec.conversion == 'G') out << std::uppercase;
        const std::size_t precision = spec.precision.value_or(6u);
        out << std::setprecision(static_cast<int>(precision));
        if (spec.conversion == 'f') out << std::fixed;
        else if (spec.conversion == 'e' || spec.conversion == 'E') out << std::scientific;
        else out << std::defaultfloat;
        out << magnitude;
        body = out.str();
        if (hasFlag(spec, ',') && (spec.conversion == 'f' ||
                                   spec.conversion == 'g' || spec.conversion == 'G')) {
            body = groupFloatBody(std::move(body));
        }
    }
    return applyNumericSign(std::move(body), negative, spec);
}

std::string renderValue(const StackValue &value,
                        const FormatSpec &spec,
                        std::string &error) {
    switch (spec.conversion) {
        case 's': {
            std::string text = sv_to_string(value);
            if (spec.precision.has_value() &&
                vietvm::core::utf8CodePointCount(text) > *spec.precision) {
                text = vietvm::core::utf8CodePointSlice(text, 0, *spec.precision);
            }
            return padText(std::move(text), spec);
        }
        case 'b': {
            std::string text = stackValueTruthy(value) ? "true" : "false";
            if (spec.precision.has_value() && text.size() > *spec.precision) {
                text.resize(*spec.precision);
            }
            return padText(std::move(text), spec);
        }
        case 'c': {
            std::string text;
            if (std::holds_alternative<int>(value)) {
                const int raw = std::get<int>(value);
                if (raw < 0 || !appendUtf8(text, static_cast<std::uint32_t>(raw))) {
                    error = "FORMAT_CODEPOINT";
                    return {};
                }
            } else if (std::holds_alternative<std::string>(value) &&
                       vietvm::core::utf8CodePointCount(std::get<std::string>(value)) == 1u) {
                text = std::get<std::string>(value);
            } else {
                error = "FORMAT_CONVERSION";
                return {};
            }
            return padText(std::move(text), spec);
        }
        case 'd': case 'o': case 'x': case 'X': {
            if (!std::holds_alternative<int>(value)) {
                error = "FORMAT_CONVERSION";
                return {};
            }
            const std::int64_t number = std::get<int>(value);
            const bool negative = number < 0;
            const std::uint64_t magnitude = static_cast<std::uint64_t>(negative ? -number : number);
            const unsigned base = spec.conversion == 'd' ? 10u : (spec.conversion == 'o' ? 8u : 16u);
            std::string body = unsignedBase(magnitude, base, spec.conversion == 'X');
            if (spec.conversion == 'd' && hasFlag(spec, ',')) body = groupDecimalDigits(body);
            return applyNumericSign(std::move(body), negative, spec);
        }
        case 'f': case 'e': case 'E': case 'g': case 'G': {
            if (!isNumeric(value)) {
                error = "FORMAT_CONVERSION";
                return {};
            }
            return formatFloat(toDouble(value), spec);
        }
        default:
            error = "FORMAT_UNKNOWN_CONVERSION";
            return {};
    }
}

std::string renderPattern(const std::vector<FormatPiece> &pieces,
                          const ListHandle &arguments,
                          std::string &error) {
    std::string output;
    std::size_t implicitIndex = 0;
    for (const FormatPiece &piece : pieces) {
        if (!piece.spec.has_value()) {
            output += piece.literal;
        } else {
            const FormatSpec &spec = *piece.spec;
            if (spec.conversion == '%') {
                output += padText("%", spec);
            } else if (spec.conversion == 'n') {
                output.push_back('\n');
            } else {
                const std::size_t index = spec.argumentIndex.value_or(implicitIndex++);
                if (arguments == nullptr || index >= arguments->elements.size()) {
                    error = "FORMAT_MISSING_ARGUMENT";
                    return {};
                }
                output += renderValue(arguments->elements[index], spec, error);
                if (!error.empty()) return {};
            }
        }
        if (output.size() > kFormatResourceLimit) {
            error = "FORMAT_RESOURCE_LIMIT";
            return {};
        }
    }
    return output;
}

} // namespace

bool handleNativeFormatFunction(const std::string &fn,
                                const std::vector<StackValue> &args,
                                StackValue &result,
                                std::string &err) {
    if (!vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnFormatInternal)) {
        return false;
    }
    if (!requireNativeArgumentCount(args, fn, 3, err)) return true;
    if (!std::holds_alternative<std::string>(args[0]) ||
        !std::holds_alternative<ListHandle>(args[1])) {
        result = formatResult({}, "FORMAT_INVALID");
        err.clear();
        return true;
    }
    if (!std::holds_alternative<std::monostate>(args[2])) {
        // Locale registry/symbol table chưa có trong runtime hiện tại.
        result = formatResult({}, "FORMAT_INVALID");
        err.clear();
        return true;
    }

    std::vector<FormatPiece> pieces;
    std::string formatError = parsePattern(std::get<std::string>(args[0]), pieces);
    if (!formatError.empty()) {
        result = formatResult({}, std::move(formatError));
        return true;
    }
    const ListHandle &arguments = std::get<ListHandle>(args[1]);
    std::string rendered = renderPattern(pieces, arguments, formatError);
    result = formatResult(std::move(rendered), std::move(formatError));
    return true;
}

} // namespace vietvm::helpers
