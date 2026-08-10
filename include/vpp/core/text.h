#pragma once

#include <string>

namespace vietvm::core {

std::string trim(const std::string &value);

// Lowercase ASCII bytes while preserving UTF-8 bytes unchanged.
std::string toLowerAscii(const std::string &value);

} // namespace vietvm::core
