#pragma once

#include <string>
#include <vector>

#include "vpp/runtime/value.h"

namespace vietvm::helpers {

bool handleNativeTextFunction(const std::string &fn,
                              const std::vector<StackValue> &args,
                              StackValue &result,
                              std::string &err);

} // namespace vietvm::helpers

