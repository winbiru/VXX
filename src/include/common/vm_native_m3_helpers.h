#pragma once

#include <string>
#include <vector>

#include "vpp/runtime/value.h"
#include "vpp/bytecode/instruction.h"

namespace vietvm::helpers {

// Primitive native M3 còn lại: bridge byte UTF-8, socket và DNS.
// Chuẩn hóa/chuyển đổi bảng mã và locale policy được triển khai trong V++.
bool handleNativeM3LibraryFunction(Opcode opcode,
                                   const std::vector<StackValue> &args,
                                   StackValue &result,
                                   std::string &err);

#if !defined(_WIN32)
// POSIX System FFI TLS provider: session pointers stay inside the owning VM.
// Creation consumes fd even on handshake failure; close consumes session.
// Certificate chain and hostname verification use the existing platform TLS
// provider and must never be bypassed by the foreign-call adapter.
void *createForeignTlsClient(int fd, const std::string &hostname,
                             std::string &error);
bool writeForeignTlsClient(void *session, const std::string &payload,
                           int &written, std::string &error);
bool readForeignTlsClient(void *session, int maximum,
                          std::string &payload, std::string &error);
void closeForeignTlsClient(void *session) noexcept;
#endif

} // namespace vietvm::helpers
