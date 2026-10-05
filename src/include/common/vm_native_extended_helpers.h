#pragma once

#include <string>
#include <vector>

#include "vpp/runtime/value.h"

namespace vietvm::helpers {

// Native backend cho các primitive M3 còn cần thư viện/hệ điều hành:
// backend regex, ZIP và tác vụ đồng thời. Public regex API/parser XML nằm ở V++.
bool handleNativeExtendedLibraryFunction(const std::string &fn,
                                         const std::vector<StackValue> &args,
                                         StackValue &result,
                                         std::string &err);

} // namespace vietvm::helpers
