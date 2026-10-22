#pragma once

#include "vpp/runtime/file_machine.h"

namespace vietvm::runtime::socket_machine {

// Socket options are an OS boundary. VM-owned tokens never cross this ABI.
file_machine::Result setTimeout(int descriptor, int milliseconds) noexcept;
file_machine::Result send(std::intptr_t descriptor, const void *bytes,
                          std::size_t count, int flags) noexcept;
file_machine::Result receive(std::intptr_t descriptor, void *bytes,
                             std::size_t count, int flags) noexcept;
// Accept returns a raw OS handle only to the VM adapter; V++ sees an owned token.
file_machine::Result accept(std::intptr_t descriptor) noexcept;
file_machine::Result suppressSigpipe(std::intptr_t descriptor) noexcept;
file_machine::Result listen(int port, int backlog) noexcept;
// A numeric IP is already parsed in V++ (four IPv4 or sixteen IPv6 bytes).
// The returned raw OS handle is immediately wrapped in a VM-owned token.
file_machine::Result connect(const unsigned char *ip, std::size_t length,
                             int port, bool datagram, int timeoutMs) noexcept;

} // namespace vietvm::runtime::socket_machine
