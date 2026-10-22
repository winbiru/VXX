#include "vpp/runtime/socket_machine.h"
#include "vpp/runtime/time_clock.h"

#if !defined(_WIN32)
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <climits>
#include <limits>

namespace vietvm::runtime::socket_machine {

#if defined(VPP_FILE_MACHINE_ASM)
extern "C" std::int64_t vpp_machine_file_syscall4(long,
    std::intptr_t, std::intptr_t, std::intptr_t, std::intptr_t, int *) noexcept;
extern "C" std::int64_t vpp_machine_socket_syscall5(long,
    std::intptr_t, std::intptr_t, std::intptr_t, std::intptr_t,
    std::intptr_t, int *) noexcept;
extern "C" std::int64_t vpp_machine_socket_syscall6(long,
    std::intptr_t, std::intptr_t, std::intptr_t, std::intptr_t,
    std::intptr_t, std::intptr_t, int *) noexcept;
#endif

namespace {
file_machine::Result option(int descriptor, int name,
                            const void *value, std::size_t size) noexcept {
#if defined(VPP_FILE_MACHINE_ASM)
    int error = 0;
    const auto result = vpp_machine_socket_syscall5(SYS_setsockopt,
        descriptor, SOL_SOCKET, name, reinterpret_cast<std::intptr_t>(value),
        size, &error);
    return {result, error};
#else
    const int result = ::setsockopt(descriptor, SOL_SOCKET, name, value,
                                    static_cast<socklen_t>(size));
    return {result, result == 0 ? 0 : errno};
#endif
}
} // namespace

file_machine::Result setTimeout(int descriptor, int milliseconds) noexcept {
    if (descriptor < 0 || milliseconds <= 0 || milliseconds > 300000)
        return {-1, EINVAL};
    timeval value{};
    value.tv_sec = milliseconds / 1000;
    value.tv_usec = (milliseconds % 1000) * 1000;
    const auto receive = option(descriptor, SO_RCVTIMEO, &value, sizeof(value));
    if (receive.value < 0) return receive;
    return option(descriptor, SO_SNDTIMEO, &value, sizeof(value));
}

file_machine::Result send(std::intptr_t descriptor, const void *bytes,
                          std::size_t count, int flags) noexcept {
    if (descriptor < 0 || descriptor > INT_MAX) return {-1, EBADF};
#if defined(MSG_NOSIGNAL)
    flags |= MSG_NOSIGNAL;
#endif
#if defined(VPP_FILE_MACHINE_ASM)
    int error = 0;
    const auto result = vpp_machine_socket_syscall6(SYS_sendto,
        descriptor, reinterpret_cast<std::intptr_t>(bytes), count,
        flags, 0, 0, &error);
    return {result, error};
#else
    const auto result = ::send(static_cast<int>(descriptor), bytes, count, flags);
    return {result, result < 0 ? errno : 0};
#endif
}

file_machine::Result receive(std::intptr_t descriptor, void *bytes,
                             std::size_t count, int flags) noexcept {
    if (descriptor < 0 || descriptor > INT_MAX) return {-1, EBADF};
#if defined(VPP_FILE_MACHINE_ASM)
    int error = 0;
    const auto result = vpp_machine_socket_syscall6(SYS_recvfrom,
        descriptor, reinterpret_cast<std::intptr_t>(bytes), count,
        flags, 0, 0, &error);
    return {result, error};
#else
    const auto result = ::recv(static_cast<int>(descriptor), bytes, count, flags);
    return {result, result < 0 ? errno : 0};
#endif
}

file_machine::Result accept(std::intptr_t descriptor) noexcept {
    if (descriptor < 0 || descriptor > INT_MAX) return {-1, EBADF};
#if defined(VPP_FILE_MACHINE_ASM)
    int error = 0;
#if defined(__linux__)
    // Linux AArch64 does not define SYS_accept; accept4(flags=0) is shared.
    const auto result = vpp_machine_file_syscall4(SYS_accept4,
        descriptor, 0, 0, 0, &error);
#else
    const auto result = vpp_machine_file_syscall4(SYS_accept,
        descriptor, 0, 0, 0, &error);
#endif
    return {result, error};
#else
    const int result = ::accept(static_cast<int>(descriptor), nullptr, nullptr);
    return {result, result < 0 ? errno : 0};
#endif
}

file_machine::Result suppressSigpipe(std::intptr_t descriptor) noexcept {
#if defined(SO_NOSIGPIPE)
    if (descriptor < 0 || descriptor > INT_MAX) return {-1, EBADF};
    const int enabled = 1;
    return option(static_cast<int>(descriptor), SO_NOSIGPIPE, &enabled, sizeof(enabled));
#else
    (void)descriptor;
    return {0, 0};
#endif
}

file_machine::Result listen(int port, int backlog) noexcept {
    if (port <= 0 || port > 65535 || backlog <= 0 || backlog > 1024)
        return {-1, EINVAL};
#if defined(VPP_FILE_MACHINE_ASM)
    const auto call = [](long syscall, std::intptr_t a = 0, std::intptr_t b = 0,
                         std::intptr_t c = 0) noexcept -> file_machine::Result {
        int error = 0;
        const auto result = vpp_machine_file_syscall4(syscall, a, b, c, 0, &error);
        return {result, error};
    };
    const auto opened = call(SYS_socket, AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
    const int fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    const file_machine::Result opened{fd, fd < 0 ? errno : 0};
#endif
    if (opened.value < 0) return opened;
    const int descriptor = static_cast<int>(opened.value);
    // Reuse is an optimization, matching the legacy socket adapter.
    const int reuse = 1;
    (void)option(descriptor, SO_REUSEADDR, &reuse, sizeof(reuse));
    const auto noSignal = suppressSigpipe(descriptor);
    if (noSignal.value < 0) {
        (void)file_machine::close(descriptor);
        return noSignal;
    }
    sockaddr_in address{};
#if defined(__APPLE__)
    address.sin_len = sizeof(address);
#endif
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    address.sin_addr.s_addr = htonl(INADDR_ANY);
#if defined(VPP_FILE_MACHINE_ASM)
    const auto bound = call(SYS_bind, descriptor,
        reinterpret_cast<std::intptr_t>(&address), sizeof(address));
#else
    const int bindResult = ::bind(descriptor,
        reinterpret_cast<const sockaddr *>(&address), sizeof(address));
    const file_machine::Result bound{bindResult, bindResult < 0 ? errno : 0};
#endif
    if (bound.value < 0) {
        (void)file_machine::close(descriptor);
        return bound;
    }
#if defined(VPP_FILE_MACHINE_ASM)
    const auto listening = call(SYS_listen, descriptor, backlog);
#else
    const int listenResult = ::listen(descriptor, backlog);
    const file_machine::Result listening{listenResult, listenResult < 0 ? errno : 0};
#endif
    if (listening.value < 0) {
        (void)file_machine::close(descriptor);
        return listening;
    }
    return {descriptor, 0};
}

file_machine::Result connect(const unsigned char *ip, std::size_t length,
                             int port, bool datagram, int timeoutMs) noexcept {
    if (!ip || (length != 4 && length != 16) || port < 1 || port > 65535 ||
        timeoutMs < 1 || timeoutMs > 300000)
        return {-1, EINVAL};
    sockaddr_storage address{};
    socklen_t addressSize = 0;
    if (length == 4) {
        sockaddr_in ipv4{};
#if defined(__APPLE__)
        ipv4.sin_len = sizeof(ipv4);
#endif
        ipv4.sin_family = AF_INET;
        ipv4.sin_port = htons(static_cast<std::uint16_t>(port));
        std::memcpy(&ipv4.sin_addr, ip, length);
        std::memcpy(&address, &ipv4, sizeof(ipv4));
        addressSize = sizeof(ipv4);
    } else {
        sockaddr_in6 ipv6{};
#if defined(__APPLE__)
        ipv6.sin6_len = sizeof(ipv6);
#endif
        ipv6.sin6_family = AF_INET6;
        ipv6.sin6_port = htons(static_cast<std::uint16_t>(port));
        std::memcpy(&ipv6.sin6_addr, ip, length);
        std::memcpy(&address, &ipv6, sizeof(ipv6));
        addressSize = sizeof(ipv6);
    }
    const int family = length == 4 ? AF_INET : AF_INET6;
    const int kind = datagram ? SOCK_DGRAM : SOCK_STREAM;
    const int protocol = datagram ? IPPROTO_UDP : IPPROTO_TCP;
#if defined(VPP_FILE_MACHINE_ASM)
    const auto call = [](long syscall, std::intptr_t a = 0,
                         std::intptr_t b = 0, std::intptr_t c = 0) noexcept {
        int error = 0;
        const auto result = vpp_machine_file_syscall4(syscall, a, b, c, 0, &error);
        return file_machine::Result{result, error};
    };
    const auto opened = call(SYS_socket, family, kind, protocol);
#else
    const int fd = ::socket(family, kind, protocol);
    const file_machine::Result opened{fd, fd < 0 ? errno : 0};
#endif
    if (opened.value < 0) return opened;
    const int fd = static_cast<int>(opened.value);
    const auto abort = [&](int error) noexcept -> file_machine::Result {
        (void)file_machine::close(fd);
        return {-1, error};
    };
    const auto noSignal = suppressSigpipe(fd);
    if (noSignal.value < 0) return abort(noSignal.error);
#if defined(VPP_FILE_MACHINE_ASM)
    const auto flags = call(SYS_fcntl, fd, F_GETFL, 0);
    if (flags.value < 0) return abort(flags.error);
    const auto nonblock = call(SYS_fcntl, fd, F_SETFL, flags.value | O_NONBLOCK);
    if (nonblock.value < 0) return abort(nonblock.error);
    auto status = call(SYS_connect, fd,
        reinterpret_cast<std::intptr_t>(&address), addressSize);
#else
    const int flagsValue = ::fcntl(fd, F_GETFL, 0);
    if (flagsValue < 0) return abort(errno);
    const file_machine::Result flags{flagsValue, 0};
    if (::fcntl(fd, F_SETFL, flags.value | O_NONBLOCK) < 0) return abort(errno);
    int result = ::connect(fd, reinterpret_cast<const sockaddr *>(&address), addressSize);
    file_machine::Result status{result, result < 0 ? errno : 0};
#endif
    if (status.value < 0 && (status.error == EINPROGRESS || status.error == EINTR ||
                             status.error == EWOULDBLOCK)) {
        std::int64_t started = 0;
        if (!machineMonotonicMilliseconds(started)) return abort(EIO);
        if (started > std::numeric_limits<std::int64_t>::max() - timeoutMs)
            return abort(EOVERFLOW);
        const auto deadline = started + timeoutMs;
        for (;;) {
            std::int64_t now = 0;
            if (!machineMonotonicMilliseconds(now)) return abort(EIO);
            const auto left = deadline - now;
            if (left <= 0) return abort(ETIMEDOUT);
            pollfd candidate{fd, POLLOUT, 0};
#if defined(VPP_FILE_MACHINE_ASM)
#if defined(__linux__)
            timespec duration{};
            duration.tv_sec = left / 1000;
            duration.tv_nsec = (left % 1000) * 1000000;
            const auto ready = call(SYS_ppoll,
                reinterpret_cast<std::intptr_t>(&candidate), 1,
                reinterpret_cast<std::intptr_t>(&duration));
#else
            const auto ready = call(SYS_poll,
                reinterpret_cast<std::intptr_t>(&candidate), 1, left);
#endif
#else
            const int n = ::poll(&candidate, 1, static_cast<int>(left));
            const file_machine::Result ready{n, n < 0 ? errno : 0};
#endif
            if (ready.value < 0 && ready.error == EINTR) continue;
            if (ready.value < 0) return abort(ready.error);
            if (ready.value == 0) return abort(ETIMEDOUT);
            int pending = 0;
            socklen_t pendingSize = sizeof(pending);
#if defined(VPP_FILE_MACHINE_ASM)
            int error = 0;
            const auto checked = vpp_machine_socket_syscall5(SYS_getsockopt,
                fd, SOL_SOCKET, SO_ERROR,
                reinterpret_cast<std::intptr_t>(&pending),
                reinterpret_cast<std::intptr_t>(&pendingSize), &error);
            if (checked < 0) return abort(error);
#else
            if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &pending, &pendingSize) < 0)
                return abort(errno);
#endif
            if (pending != 0) return abort(pending);
            status = {0, 0};
            break;
        }
    }
    if (status.value < 0) return abort(status.error);
#if defined(VPP_FILE_MACHINE_ASM)
    const auto restored = call(SYS_fcntl, fd, F_SETFL, flags.value);
    if (restored.value < 0) return abort(restored.error);
#else
    if (::fcntl(fd, F_SETFL, flags.value) < 0) return abort(errno);
#endif
    const auto timeout = setTimeout(fd, timeoutMs);
    if (timeout.value < 0) return abort(timeout.error);
    return {fd, 0};
}

} // namespace vietvm::runtime::socket_machine
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#include <chrono>
#include <cstring>

namespace vietvm::runtime::socket_machine {
file_machine::Result send(std::intptr_t descriptor, const void *bytes,
                          std::size_t count, int flags) noexcept {
    const int n = ::send(static_cast<SOCKET>(descriptor),
        static_cast<const char *>(bytes), static_cast<int>(count), flags);
    return {n, n == SOCKET_ERROR ? ::WSAGetLastError() : 0};
}
file_machine::Result receive(std::intptr_t descriptor, void *bytes,
                             std::size_t count, int flags) noexcept {
    const int n = ::recv(static_cast<SOCKET>(descriptor),
        static_cast<char *>(bytes), static_cast<int>(count), flags);
    return {n, n == SOCKET_ERROR ? ::WSAGetLastError() : 0};
}
file_machine::Result accept(std::intptr_t descriptor) noexcept {
    const SOCKET accepted = ::accept(static_cast<SOCKET>(descriptor), nullptr, nullptr);
    return {accepted == INVALID_SOCKET ? -1 : static_cast<std::int64_t>(accepted),
            accepted == INVALID_SOCKET ? ::WSAGetLastError() : 0};
}
file_machine::Result suppressSigpipe(std::intptr_t) noexcept { return {0, 0}; }
file_machine::Result listen(int port, int backlog) noexcept {
    const SOCKET descriptor = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (descriptor == INVALID_SOCKET) return {-1, ::WSAGetLastError()};
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<u_short>(port));
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(descriptor, reinterpret_cast<sockaddr *>(&address),
               sizeof(address)) != 0 || ::listen(descriptor, backlog) != 0) {
        const int error = ::WSAGetLastError();
        (void)::closesocket(descriptor);
        return {-1, error};
    }
    return {static_cast<std::int64_t>(descriptor), 0};
}
file_machine::Result connect(const unsigned char *ip, std::size_t length,
                             int port, bool datagram, int timeoutMs) noexcept {
    if (!ip || (length != 4 && length != 16) || port < 1 || port > 65535 ||
        timeoutMs < 1 || timeoutMs > 300000)
        return {-1, WSAEINVAL};
    sockaddr_storage address{};
    int size = 0;
    if (length == 4) {
        sockaddr_in ipv4{};
        ipv4.sin_family = AF_INET;
        ipv4.sin_port = htons(static_cast<u_short>(port));
        std::memcpy(&ipv4.sin_addr, ip, length);
        std::memcpy(&address, &ipv4, sizeof(ipv4));
        size = sizeof(ipv4);
    } else {
        sockaddr_in6 ipv6{};
        ipv6.sin6_family = AF_INET6;
        ipv6.sin6_port = htons(static_cast<u_short>(port));
        std::memcpy(&ipv6.sin6_addr, ip, length);
        std::memcpy(&address, &ipv6, sizeof(ipv6));
        size = sizeof(ipv6);
    }
    const SOCKET fd = ::socket(length == 4 ? AF_INET : AF_INET6,
        datagram ? SOCK_DGRAM : SOCK_STREAM,
        datagram ? IPPROTO_UDP : IPPROTO_TCP);
    if (fd == INVALID_SOCKET) return {-1, ::WSAGetLastError()};
    const auto abort = [&](int error) noexcept -> file_machine::Result {
        (void)::closesocket(fd);
        return {-1, error};
    };
    u_long enabled = 1;
    if (::ioctlsocket(fd, FIONBIO, &enabled) != 0) return abort(::WSAGetLastError());
    int status = ::connect(fd, reinterpret_cast<const sockaddr *>(&address), size);
    int error = status == 0 ? 0 : ::WSAGetLastError();
    if (status != 0 && (error == WSAEWOULDBLOCK || error == WSAEINPROGRESS ||
                        error == WSAEINTR)) {
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(timeoutMs);
        for (;;) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count();
            if (left <= 0) return abort(WSAETIMEDOUT);
            fd_set writable{};
            FD_SET(fd, &writable);
            timeval wait{static_cast<long>(left / 1000),
                         static_cast<long>((left % 1000) * 1000)};
            const int ready = ::select(0, nullptr, &writable, nullptr, &wait);
            if (ready == SOCKET_ERROR && ::WSAGetLastError() == WSAEINTR) continue;
            if (ready == SOCKET_ERROR) return abort(::WSAGetLastError());
            if (ready == 0) return abort(WSAETIMEDOUT);
            int pending = 0;
            int pendingSize = sizeof(pending);
            if (::getsockopt(fd, SOL_SOCKET, SO_ERROR,
                    reinterpret_cast<char *>(&pending), &pendingSize) != 0)
                return abort(::WSAGetLastError());
            if (pending != 0) return abort(pending);
            status = 0;
            break;
        }
    }
    if (status != 0) return abort(error);
    enabled = 0;
    if (::ioctlsocket(fd, FIONBIO, &enabled) != 0) return abort(::WSAGetLastError());
    const DWORD timeout = static_cast<DWORD>(timeoutMs);
    if (::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
            reinterpret_cast<const char *>(&timeout), sizeof(timeout)) != 0 ||
        ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO,
            reinterpret_cast<const char *>(&timeout), sizeof(timeout)) != 0)
        return abort(::WSAGetLastError());
    return {static_cast<std::int64_t>(fd), 0};
}
} // namespace vietvm::runtime::socket_machine
#endif
