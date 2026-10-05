#include "common/vm_native_format_helpers.h"

#include <cmath>
#include <cstddef>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>

#include "common/vm_native_constants.h"
#include "common/vm_native_helpers.h"

namespace vietvm::helpers {
namespace {

constexpr std::size_t kFormatResourceLimit = 1024u * 1024u;

std::string formatFloatMagnitude(double magnitude,
                                 char conversion,
                                 std::size_t precision) {
    if (std::isnan(magnitude)) {
        return (conversion == 'E' || conversion == 'G') ? "NAN" : "NaN";
    }
    if (std::isinf(magnitude)) {
        return (conversion == 'E' || conversion == 'G') ? "INFINITY" : "Infinity";
    }

    std::ostringstream out;
    out.imbue(std::locale::classic());
    if (conversion == 'E' || conversion == 'G') out << std::uppercase;
    out << std::setprecision(static_cast<int>(precision));
    if (conversion == 'f') out << std::fixed;
    else if (conversion == 'e' || conversion == 'E') out << std::scientific;
    else out << std::defaultfloat;
    out << magnitude;
    return out.str();
}

} // namespace

bool handleNativeFormatFunction(const std::string &fn,
                                const std::vector<StackValue> &args,
                                StackValue &result,
                                std::string &err) {
    if (!vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnFormatFloatInternal)) {
        return false;
    }
    if (!requireNativeArgumentCount(args, fn, 3, err)) return true;
    if (!isNumeric(args[0]) || !std::holds_alternative<std::string>(args[1]) ||
        !std::holds_alternative<int>(args[2])) {
        err = fn + ": đối số không hợp lệ";
        return true;
    }

    const std::string &conversionText = std::get<std::string>(args[1]);
    const int precision = std::get<int>(args[2]);
    if (conversionText.size() != 1u || precision < 0 ||
        precision > static_cast<int>(kFormatResourceLimit)) {
        err = fn + ": đối số không hợp lệ";
        return true;
    }
    const char conversion = conversionText[0];
    if (conversion != 'f' && conversion != 'e' && conversion != 'E' &&
        conversion != 'g' && conversion != 'G') {
        err = fn + ": phép chuyển đổi không hợp lệ";
        return true;
    }

    const double number = toDouble(args[0]);
    const bool negative = std::signbit(number);
    MapValue map;
    map.entries.emplace("thân", make_string_value(formatFloatMagnitude(
        negative ? -number : number,
        conversion,
        static_cast<std::size_t>(precision))));
    map.entries.emplace("âm", make_int_value(negative ? 1 : 0));
    result = make_map_value(std::move(map));
    return true;
}

} // namespace vietvm::helpers
