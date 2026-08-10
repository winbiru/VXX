#include "vpp/core/text.h"

namespace vietvm::core {

std::string trim(const std::string &value) {
    const size_t start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    const size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

std::string toLowerAscii(const std::string &value) {
    std::string result;
    result.reserve(value.size());
    for (unsigned char c : value) {
        if (c >= static_cast<unsigned char>('A') && c <= static_cast<unsigned char>('Z')) {
            result.push_back(static_cast<char>(c + ('a' - 'A')));
        } else {
            result.push_back(static_cast<char>(c));
        }
    }
    return result;
}

} // namespace vietvm::core
