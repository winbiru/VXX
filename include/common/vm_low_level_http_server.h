#pragma once

#include <optional>
#include <string>

#include "vpp/runtime/value.h"

namespace vietvm::helpers {

bool tryLowLevelHttpFileTransportRequest(const std::string &method,
                                             const std::string &url,
                                             const std::optional<std::string> &payload,
                                             StackValue &result,
                                             std::string &err,
                                             bool &handled);

bool runLowLevelHttpServerOpen(int port, StackValue &result, std::string &err);
bool runLowLevelHttpServerNext(int serverId, StackValue &result, std::string &err);
bool runLowLevelHttpReqField(const std::string &reqId,
                             const std::string &field,
                             const std::optional<std::string> &key,
                             StackValue &result,
                             std::string &err);
bool runLowLevelHttpServerSend(const std::string &reqId,
                               int status,
                               const std::string &body,
                               StackValue &result,
                               std::string &err);
bool runLowLevelHttpServerClose(int serverId, StackValue &result, std::string &err);

} // namespace vietvm::helpers
