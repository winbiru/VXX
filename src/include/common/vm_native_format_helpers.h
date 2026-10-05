#pragma once

#include <string>
#include <vector>

#include "vpp/runtime/value.h"

namespace vietvm::helpers {

// Primitive duy nhất còn native của formatter: chuyển IEEE-754 sang biểu diễn
// fixed/scientific/general ổn định. Parser, flags, grouping và padding ở V++.
bool handleNativeFormatFunction(const std::string &fn,
                                const std::vector<StackValue> &args,
                                StackValue &result,
                                std::string &err);

} // namespace vietvm::helpers
