#pragma once

#include <string>
#include <vector>

#include "vpp/runtime/value.h"
#include "vpp/bytecode/instruction.h"

namespace vietvm::helpers {

// Dispatch các hàm native thao tác list/map/tập hợp theo tên; handler kiểm tra đối số, thực hiện phép toán collection và đẩy kết quả trở lại stack.
bool handleNativeCollectionFunction(Opcode opcode,
                                    const std::vector<StackValue> &args,
                                    StackValue &result,
                                    std::string &err);

} // namespace vietvm::helpers

