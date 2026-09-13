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
// Kiểm tra điều kiện của `startsWith`.
bool startsWith(const std::string &value, const std::string &prefix);

// Tạo bản sao chuỗi đã bỏ whitespace ở hai đầu; hàm không sửa dữ liệu gốc nên phù hợp cho parser helper/native argument.
std::string trimCopy(const std::string &s);
// Chuyển một `StackValue` đối số thành chuỗi thô mà native helper cần; hàm giữ nội dung chuỗi nguyên bản và định dạng scalar theo quy tắc runtime.
std::string argToRawString(const StackValue &v);
// Giải mã đơn giản escapes; hàm đọc biểu diễn đã mã hóa, kiểm tra định dạng và dựng lại giá trị runtime tương ứng.
std::string decodeSimpleEscapes(const std::string &s);
// Phân tích thuộc tính phép gán; hàm duyệt đầu vào theo ngữ pháp/định dạng quy định, tạo cấu trúc kết quả và báo lỗi khi dữ liệu không hợp lệ.
std::optional<std::pair<std::string, std::string>> parsePropertyAssignment(
    const std::string &line);
// Đọc thuộc tính by khóa; hàm lấy nội dung từ nguồn tương ứng, kiểm tra lỗi cần thiết rồi trả dữ liệu đã đọc.
std::string readPropertyByKey(const std::string &filePath,
                              const std::string &key,
                              const std::string &fallback);

// Phân tích số nguyên arg from stack; hàm duyệt đầu vào theo ngữ pháp/định dạng quy định, tạo cấu trúc kết quả và báo lỗi khi dữ liệu không hợp lệ.
bool parseIntArgFromStack(const StackValue &arg,
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

// Chạy cơ sở dữ liệu connect; hàm điều phối toàn bộ luồng xử lý của tác vụ, gọi các bước con theo thứ tự và trả mã/kết quả cuối cùng.
bool runDbConnect(const std::string &driverClass,
                  const std::string &jdbcUrl,
                  const std::string &user,
                  const std::string &password,
                  StackValue &result,
                  std::string &err);

// Chạy cơ sở dữ liệu query; hàm điều phối toàn bộ luồng xử lý của tác vụ, gọi các bước con theo thứ tự và trả mã/kết quả cuối cùng.
bool runDbQuery(const std::string &driverClass,
                const std::string &jdbcUrl,
                const std::string &user,
                const std::string &password,
                const std::string &sql,
                StackValue &result,
                std::string &err);

// Chạy curl HTTP yêu cầu; hàm điều phối toàn bộ luồng xử lý của tác vụ, gọi các bước con theo thứ tự và trả mã/kết quả cuối cùng.
bool runCurlHttpRequest(const std::string &method,
                        const std::string &fnName,
                        const std::string &url,
                        const std::optional<std::string> &payload,
                        StackValue &result,
                        std::string &err);

} // namespace vietvm::helpers
