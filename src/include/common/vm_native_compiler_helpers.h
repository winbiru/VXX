#pragma once

#include <string>
#include <vector>

#include "vpp/runtime/value.h"

namespace vietvm::helpers {

// Hai primitive compiler/runtime tối thiểu. Policy API, default path,
// contract thành công/thất bại và diễn giải snapshot thuộc thư viện V++.
bool compilerAnalyzePrimitive(const std::vector<StackValue> &args,
                              StackValue &result,
                              std::string &err);

bool embeddedVmRunPrimitive(const std::vector<StackValue> &args,
                            StackValue &result,
                            std::string &err);

} // namespace vietvm::helpers
