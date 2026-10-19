#pragma once

#include "vpp/runtime/value.h"

namespace vietvm::runtime {
MapValue decodeMapFromStringPool(const std::string &encoded);
std::vector<StackValue> decodeListFromStringPool(const std::string &encoded);
} // namespace vietvm::runtime
