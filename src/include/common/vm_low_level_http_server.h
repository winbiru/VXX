#pragma once

#include <functional>
#include <optional>
#include <string>

#include "vpp/runtime/value.h"

namespace vietvm::helpers {

using LowLevelHttpLogSink = std::function<void(const std::string&)>;

// Thử xử lý HTTP loopback qua cơ chế file transport; hàm đọc request đã mã hóa từ thư mục trao đổi và trả `false` nếu không có request phù hợp.
bool tryLowLevelHttpFileTransportRequest(const std::string &method,
                                             const std::string &url,
                                             const std::optional<std::string> &payload,
                                             StackValue &result,
                                             std::string &err,
                                             bool &handled);

// Chạy low level HTTP máy chủ open; hàm điều phối toàn bộ luồng xử lý của tác vụ, gọi các bước con theo thứ tự và trả mã/kết quả cuối cùng.
bool runLowLevelHttpServerOpen(int port,
                               StackValue &result,
                               std::string &err,
                               const LowLevelHttpLogSink &logSink);
// Chạy low level HTTP máy chủ next; hàm điều phối toàn bộ luồng xử lý của tác vụ, gọi các bước con theo thứ tự và trả mã/kết quả cuối cùng.
bool runLowLevelHttpServerNext(int serverId, StackValue &result, std::string &err);
// Chạy low level HTTP req trường; hàm điều phối toàn bộ luồng xử lý của tác vụ, gọi các bước con theo thứ tự và trả mã/kết quả cuối cùng.
bool runLowLevelHttpReqField(const std::string &reqId,
                             const std::string &field,
                             const std::optional<std::string> &key,
                             StackValue &result,
                             std::string &err);
// Chạy low level HTTP máy chủ send; hàm điều phối toàn bộ luồng xử lý của tác vụ, gọi các bước con theo thứ tự và trả mã/kết quả cuối cùng.
bool runLowLevelHttpServerSend(const std::string &reqId,
                               int status,
                               const std::string &body,
                               StackValue &result,
                               std::string &err);
// Chạy low level HTTP máy chủ close; hàm điều phối toàn bộ luồng xử lý của tác vụ, gọi các bước con theo thứ tự và trả mã/kết quả cuối cùng.
bool runLowLevelHttpServerClose(int serverId, StackValue &result, std::string &err);

} // namespace vietvm::helpers
