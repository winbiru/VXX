#pragma once

#include <string>
#include <vector>

#include "vpp/runtime/value.h"

namespace vietvm::helpers {

// Handles native stdlib functions whose behavior is centered on runtime
// collections (list/map/set/tuple), plus the shared length/reverse operations
// that accept both collections and strings. Returns false when fn is not part
// of this native surface.
bool handleNativeCollectionFunction(const std::string &fn,
                                    const std::vector<StackValue> &args,
                                    StackValue &result,
                                    std::string &err);

} // namespace vietvm::helpers

