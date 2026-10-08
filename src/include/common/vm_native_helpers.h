#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "vpp/runtime/value.h"

namespace vietvm::helpers {

// Kiểm tra điều kiện của `hasEnvVar`.
bool hasEnvVar(const char *name);
// Lấy env var; hàm đọc dữ liệu từ trạng thái hiện tại và trả về cho caller mà không chủ động thay đổi dữ liệu.
std::optional<std::string> getEnvVar(const char *name);
// Chuyển một `StackValue` đối số thành chuỗi thô mà native helper cần; hàm giữ nội dung chuỗi nguyên bản và định dạng scalar theo quy tắc runtime.
std::string argToRawString(const StackValue &v);
// Đọc đối số số nguyên đã được wrapper V++ chuẩn hóa. Native boundary không
// thực hiện parsing/coercion từ chuỗi hoặc số thực.
bool requireIntArgFromStack(const StackValue &arg,
                            const std::string &fn,
                            const std::string &label,
                            int &out,
                            std::string &err);

// Tạo exception/thông báo lỗi khi số đối số native không đúng; hàm đóng gói tên hàm và arity mong đợi vào diagnostic thống nhất.
std::string nativeArgumentCountError(const std::string &fn, int expectedCount);
// Xác minh số đối số trên stack đúng arity yêu cầu; nếu sai hàm ném lỗi chuẩn trước khi native handler đọc tham số.
bool requireNativeArgumentCount(const std::vector<StackValue> &args,
                                const std::string &fn,
                                int expectedCount,
                                std::string &err);

// Lấy first danh sách đối số; hàm đọc dữ liệu từ trạng thái hiện tại và trả về cho caller mà không chủ động thay đổi dữ liệu.
bool getFirstListArgument(const std::vector<StackValue> &args,
                          const std::string &fn,
                          ListHandle &out,
                          std::string &err);
// Lấy danh sách đối số; hàm đọc dữ liệu từ trạng thái hiện tại và trả về cho caller mà không chủ động thay đổi dữ liệu.
bool getListArgument(const std::vector<StackValue> &args,
                     std::size_t index,
                     const std::string &fn,
                     ListHandle &out,
                     std::string &err);
// Lấy first ánh xạ đối số; hàm đọc dữ liệu từ trạng thái hiện tại và trả về cho caller mà không chủ động thay đổi dữ liệu.
bool getFirstMapArgument(const std::vector<StackValue> &args,
                         const std::string &fn,
                         MapHandle &out,
                         std::string &err);
// Lấy non negative danh sách chỉ số; hàm đọc dữ liệu từ trạng thái hiện tại và trả về cho caller mà không chủ động thay đổi dữ liệu.
bool getNonNegativeListIndex(const StackValue &value, int &index, std::string &err);

// Primitive tiến trình cho thư viện chuẩn. V++ chịu trách nhiệm validate argv
// và environment; helper chỉ spawn, capture stdout/stderr và chờ exit status.
bool handleNativeProcessPrimitive(const std::vector<StackValue> &args,
                                  StackValue &result,
                                  std::string &err);

} // namespace vietvm::helpers
