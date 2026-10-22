#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>
#include "vpp/bytecode/intrinsic.h"
#include "vpp/runtime/error.h"
#include "vpp/runtime/foreign.h"
#include "vpp/native/aot_runtime.h"

#if !defined(_WIN32)
#include <netinet/in.h>
#include <cerrno>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

using namespace vietvm;
namespace {
bool rejects(Opcode op, std::vector<StackValue> args,
             const std::unordered_set<std::string> &cap,
             runtime::ForeignFileState &state) {
    try { (void)runtime::executeSocketMachinePrimitive(op, args, cap, state); }
    catch (const runtime::RuntimeError &error) {
        return error.kind() == runtime::RuntimeErrorKind::CallBoundary;
    }
    catch (...) { return false; }
    return false;
}
bool rejectsAsLanguageError(Opcode op, std::vector<StackValue> args,
                            const std::unordered_set<std::string> &cap,
                            runtime::ForeignFileState &state) {
    try { (void)runtime::executeSocketMachinePrimitive(op, args, cap, state); }
    catch (const runtime::LanguageException &) { return true; }
    catch (...) { return false; }
    return false;
}
}

int main() {
    int failures = 0;
    const auto check = [&](bool ok, const char *label) {
        if (!ok) { std::cerr << label << '\n'; ++failures; }
    };
    const std::unordered_set<std::string> caps{
        "machine.net.socket.kind", "machine.net.socket.close", "machine.net.socket.timeout",
        "machine.net.socket.send", "machine.net.socket.recv",
        "machine.net.socket.accept", "machine.net.socket.listen",
        "machine.net.socket.connect"};
    constexpr int token = 536870912;
    runtime::ForeignFileState state;
    const auto kind = [&]() -> int {
        return std::get<int>(runtime::executeSocketMachinePrimitive(
            OP_VM_MACHINE_SOCKET_KIND, {token}, caps, state));
    };
    check(bytecode::intrinsicByOpcode(OP_VM_MACHINE_SOCKET_KIND) &&
          bytecode::intrinsicByOpcode(OP_VM_MACHINE_SOCKET_KIND)->arity == 1,
          "socket kind intrinsic missing");
    check(bytecode::intrinsicByOpcode(OP_VM_MACHINE_SOCKET_CLOSE) &&
          bytecode::intrinsicByOpcode(OP_VM_MACHINE_SOCKET_CLOSE)->arity == 1,
          "socket close intrinsic missing");
    check(bytecode::intrinsicByOpcode(OP_VM_MACHINE_SOCKET_TIMEOUT) &&
          bytecode::intrinsicByOpcode(OP_VM_MACHINE_SOCKET_TIMEOUT)->arity == 2,
          "socket timeout intrinsic missing");
    check(bytecode::intrinsicByOpcode(OP_VM_MACHINE_SOCKET_SEND) &&
          bytecode::intrinsicByOpcode(OP_VM_MACHINE_SOCKET_SEND)->arity == 4 &&
          bytecode::intrinsicByOpcode(OP_VM_MACHINE_SOCKET_RECV) &&
          bytecode::intrinsicByOpcode(OP_VM_MACHINE_SOCKET_RECV)->arity == 4,
          "socket I/O intrinsic missing");
    check(bytecode::intrinsicByOpcode(OP_VM_MACHINE_SOCKET_ACCEPT) &&
          bytecode::intrinsicByOpcode(OP_VM_MACHINE_SOCKET_ACCEPT)->arity == 1,
          "socket accept intrinsic missing");
    check(bytecode::intrinsicByOpcode(OP_VM_MACHINE_SOCKET_LISTEN) &&
          bytecode::intrinsicByOpcode(OP_VM_MACHINE_SOCKET_LISTEN)->arity == 2,
          "socket listen intrinsic missing");
    check(bytecode::intrinsicByOpcode(OP_VM_MACHINE_SOCKET_CONNECT) &&
          bytecode::intrinsicByOpcode(OP_VM_MACHINE_SOCKET_CONNECT)->arity == 4,
          "socket connect intrinsic missing");
    check(runtime::isSocketMachinePrimitive(OP_VM_MACHINE_SOCKET_KIND) &&
          runtime::isSocketMachinePrimitive(OP_VM_MACHINE_SOCKET_CLOSE) &&
          runtime::isSocketMachinePrimitive(OP_VM_MACHINE_SOCKET_TIMEOUT) &&
          runtime::isSocketMachinePrimitive(OP_VM_MACHINE_SOCKET_SEND) &&
          runtime::isSocketMachinePrimitive(OP_VM_MACHINE_SOCKET_RECV) &&
          runtime::isSocketMachinePrimitive(OP_VM_MACHINE_SOCKET_ACCEPT) &&
          runtime::isSocketMachinePrimitive(OP_VM_MACHINE_SOCKET_LISTEN) &&
          runtime::isSocketMachinePrimitive(OP_VM_MACHINE_SOCKET_CONNECT) &&
          !runtime::isSocketMachinePrimitive(OP_VM_SOCKET_DONG),
          "socket opcode collision");
    state.sockets.emplace(token, runtime::ForeignFileState::Socket{-1, false, false});
    check(rejects(OP_VM_MACHINE_SOCKET_KIND, {token}, {}, state), "kind bypasses capability");
    check(rejects(OP_VM_MACHINE_SOCKET_CLOSE, {token}, {"system.net.socket.close"}, state),
          "close accepted legacy FFI capability");
    check(rejects(OP_VM_MACHINE_SOCKET_TIMEOUT, {token, 1000},
                  {"system.net.socket.timeout"}, state),
          "timeout accepted legacy FFI capability");
    check(rejects(OP_VM_MACHINE_SOCKET_SEND, {token, make_list_value({65}), 1, 0},
                  {"system.net.socket.send"}, state),
          "send accepted legacy FFI capability");
    check(rejects(OP_VM_MACHINE_SOCKET_RECV, {token, make_list_value({0}), 1, 0},
                  {"system.net.socket.recv"}, state),
          "recv accepted legacy FFI capability");
    check(rejects(OP_VM_MACHINE_SOCKET_ACCEPT, {token},
                  {"system.net.socket.accept"}, state),
          "accept accepted legacy FFI capability");
    check(rejects(OP_VM_MACHINE_SOCKET_LISTEN, {8080, 4},
                  {"system.net.socket.listen"}, state),
          "listen accepted legacy FFI capability");
    const auto loopback = make_list_value({127, 0, 0, 1});
    const auto ip6Loopback = make_list_value({0, 0, 0, 0, 0, 0, 0, 0,
                                            0, 0, 0, 0, 0, 0, 0, 1});
    check(rejects(OP_VM_MACHINE_SOCKET_CONNECT, {loopback, 8080, 0, 100},
                  {"system.net.socket.connect"}, state),
          "connect accepted legacy FFI capability");
    check(rejects(OP_VM_MACHINE_SOCKET_CONNECT, {loopback, 8080, 0}, caps, state),
          "connect accepted missing timeout");
    check(rejects(OP_VM_MACHINE_SOCKET_CONNECT, {"127.0.0.1", 8080, 0, 100}, caps, state),
          "connect accepted unparsed IP string");
    check(rejects(OP_VM_MACHINE_SOCKET_CONNECT, {make_list_value({127, 0}), 8080, 0, 100}, caps, state),
          "connect accepted invalid IP length");
    check(rejects(OP_VM_MACHINE_SOCKET_CONNECT, {make_list_value({127, 0, -1, 1}), 8080, 0, 100}, caps, state),
          "connect accepted invalid IP byte");
    check(rejects(OP_VM_MACHINE_SOCKET_CONNECT, {ip6Loopback, 0, 0, 100}, caps, state),
          "connect accepted invalid port");
    check(rejects(OP_VM_MACHINE_SOCKET_CONNECT, {loopback, 8080, 2, 100}, caps, state),
          "connect accepted invalid UDP flag");
    check(rejects(OP_VM_MACHINE_SOCKET_CONNECT, {loopback, 8080, 0, 0}, caps, state),
          "connect accepted invalid timeout");
    check(rejects(OP_VM_MACHINE_SOCKET_LISTEN, {8080}, caps, state),
          "listen accepted missing backlog");
    check(rejects(OP_VM_MACHINE_SOCKET_LISTEN, {0, 4}, caps, state),
          "listen accepted invalid port");
    check(rejects(OP_VM_MACHINE_SOCKET_LISTEN, {8080, 0}, caps, state),
          "listen accepted invalid backlog");
    check(rejects(OP_VM_MACHINE_SOCKET_LISTEN, {"8080", 4}, caps, state),
          "listen accepted nonnumeric port");
    check(rejects(OP_VM_MACHINE_SOCKET_ACCEPT, {token, 5}, caps, state),
          "accept accepted extra argument");
    check(rejectsAsLanguageError(OP_VM_MACHINE_SOCKET_ACCEPT, {token + 99}, caps, state),
          "accept accepted token from another VM");
    check(rejects(OP_VM_MACHINE_SOCKET_KIND, {}, caps, state), "missing token accepted");
    check(rejects(OP_VM_MACHINE_SOCKET_CLOSE, {token, token}, caps, state),
          "extra token accepted");
    check(rejects(OP_VM_MACHINE_SOCKET_KIND, {"token"}, caps, state),
          "wrong token type accepted");
    check(rejectsAsLanguageError(OP_VM_MACHINE_SOCKET_KIND, {token + 1}, caps, state),
          "foreign token must be catchable by V++");
    check(rejects(OP_VM_MACHINE_SOCKET_TIMEOUT, {token}, caps, state),
          "timeout missing duration accepted");
    check(rejects(OP_VM_MACHINE_SOCKET_TIMEOUT, {token, "1000"}, caps, state),
          "timeout string duration accepted");
    check(rejects(OP_VM_MACHINE_SOCKET_TIMEOUT, {token, 0}, caps, state),
          "zero timeout accepted");
    check(rejects(OP_VM_MACHINE_SOCKET_TIMEOUT, {token, 300001}, caps, state),
          "out-of-range timeout accepted");
    check(rejects(OP_VM_MACHINE_SOCKET_SEND, {token, make_list_value({-1}), 1, 0}, caps, state),
          "send accepted negative byte");
    check(rejects(OP_VM_MACHINE_SOCKET_SEND, {token, make_list_value({256}), 1, 0}, caps, state),
          "send accepted overflowing byte");
    check(rejects(OP_VM_MACHINE_SOCKET_SEND, {token, make_list_value({1}), 2, 0}, caps, state),
          "send accepted incorrect buffer length");
    check(rejects(OP_VM_MACHINE_SOCKET_RECV, {token, make_list_value({0}), 1, 1}, caps, state),
          "recv accepted forbidden flags");
    check(rejects(OP_VM_MACHINE_SOCKET_RECV, {token, make_list_value({}), 0, 0}, caps, state),
          "recv accepted empty buffer");
    state.sockets.at(token).listener = true;
    check(rejects(OP_VM_MACHINE_SOCKET_SEND, {token, make_list_value({1}), 1, 0}, caps, state),
          "send accepted listener");
    check(rejectsAsLanguageError(OP_VM_MACHINE_SOCKET_ACCEPT, {token}, caps, state),
          "accept on invalid descriptor should be catchable by V++");
    state.sockets.at(token).listener = false;
    check(rejects(OP_VM_MACHINE_SOCKET_ACCEPT, {token}, caps, state),
          "accept accepted a non-listener socket");
    check(kind() == 0, "TCP kind incorrect");
    state.sockets.at(token).datagram = true;
    check(kind() == 1, "UDP kind incorrect");
    int tlsMarker = 0;
    state.sockets.at(token).tlsSession = &tlsMarker;
    check(kind() == 2, "TLS kind incorrect");
    state.sockets.at(token).tlsSession = nullptr;

#if !defined(_WIN32)
    int connected[2]{-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, connected) == 0) {
        constexpr int timeoutToken = token + 10;
        constexpr int peerToken = token + 11;
        state.sockets.emplace(timeoutToken,
            runtime::ForeignFileState::Socket{connected[0], false, false});
        state.sockets.emplace(peerToken,
            runtime::ForeignFileState::Socket{connected[1], false, false});
        check(std::get<int>(runtime::executeSocketMachinePrimitive(
            OP_VM_MACHINE_SOCKET_TIMEOUT, {timeoutToken, 1750}, caps, state)) == 1,
            "machine socket timeout failed");
        timeval received{};
        socklen_t size = sizeof(received);
        check(::getsockopt(connected[0], SOL_SOCKET, SO_RCVTIMEO,
              &received, &size) == 0 && received.tv_sec >= 1 && received.tv_sec <= 2,
              "socket receive timeout not installed");
        timeval sending{};
        size = sizeof(sending);
        check(::getsockopt(connected[0], SOL_SOCKET, SO_SNDTIMEO,
              &sending, &size) == 0 && sending.tv_sec >= 1 && sending.tv_sec <= 2,
              "socket send timeout not installed");
        const auto input = make_list_value({0, 255, 65, 66});
        const auto output = make_list_value({0, 0, 0, 0});
        check(std::get<int>(runtime::executeSocketMachinePrimitive(
            OP_VM_MACHINE_SOCKET_SEND, {timeoutToken, input, 4, 0}, caps, state)) == 4,
            "machine sendto syscall failed");
        check(std::get<int>(runtime::executeSocketMachinePrimitive(
            OP_VM_MACHINE_SOCKET_RECV, {peerToken, output, 4, 0}, caps, state)) == 4,
            "machine recvfrom syscall failed");
        const auto &receivedBytes = std::get<ListHandle>(output)->elements;
        check(std::get<int>(receivedBytes[0]) == 0 &&
              std::get<int>(receivedBytes[1]) == 255 &&
              std::get<int>(receivedBytes[2]) == 65 &&
              std::get<int>(receivedBytes[3]) == 66,
              "machine recv did not update V++ byte list");
        check(std::get<int>(runtime::executeSocketMachinePrimitive(
            OP_VM_MACHINE_SOCKET_CLOSE, {timeoutToken}, caps, state)) == 0,
            "machine timeout fixture close failed");
        check(std::get<int>(runtime::executeSocketMachinePrimitive(
            OP_VM_MACHINE_SOCKET_CLOSE, {peerToken}, caps, state)) == 0,
            "machine peer close failed");
    } else {
        check(false, "test socketpair creation failed");
    }
    int fds[2]{-1, -1};
    if (::pipe(fds) == 0) {
        state.sockets.at(token).descriptor = fds[0];
        check(std::get<int>(runtime::executeSocketMachinePrimitive(
            OP_VM_MACHINE_SOCKET_CLOSE, {token}, caps, state)) == 0,
            "machine close failed");
        check(::fcntl(fds[0], F_GETFD) == -1 && errno == EBADF,
              "machine syscall did not close descriptor");
        (void)::close(fds[1]);
    } else {
        check(false, "test pipe creation failed");
    }
#else
    check(std::get<int>(runtime::executeSocketMachinePrimitive(
        OP_VM_MACHINE_SOCKET_CLOSE, {token}, caps, state)) == 0,
        "empty socket state close failed");
#endif
    check(state.sockets.empty(), "closed socket token still owned");
    check(rejectsAsLanguageError(OP_VM_MACHINE_SOCKET_CLOSE, {token}, caps, state),
          "double-close must be catchable by V++");

    native::aot::Program program;
    program.capabilities = caps;
    program.files.sockets.emplace(token + 1,
        runtime::ForeignFileState::Socket{-1, true, false});
    native::aot::Frame frame(program);
    frame.push(token + 1);
    frame.intrinsic(OP_VM_MACHINE_SOCKET_KIND);
    check(std::get<int>(frame.pop()) == 1, "native AOT kind parity failed");
    frame.push(token + 1);
    frame.intrinsic(OP_VM_MACHINE_SOCKET_CLOSE);
    check(std::get<int>(frame.pop()) == 0 && program.files.sockets.empty(),
          "native AOT close parity failed");
    program.capabilities.clear();
    program.files.sockets.emplace(token + 2,
        runtime::ForeignFileState::Socket{-1, false, false});
    bool aotRejected = false;
    try {
        frame.push(token + 2);
        frame.intrinsic(OP_VM_MACHINE_SOCKET_CLOSE);
    } catch (const runtime::RuntimeError &) { aotRejected = true; }
    check(aotRejected && program.files.sockets.count(token + 2) == 1,
          "native AOT bypassed revoked capability");
    program.capabilities = caps;
    frame.push(token + 2);
    frame.intrinsic(OP_VM_MACHINE_SOCKET_CLOSE);
    check(program.files.sockets.empty(), "AOT capability rejection corrupted socket state");
#if !defined(_WIN32)
    // UDP connect needs no listener. Verify VM-owned IPv4/IPv6 descriptors
    // through the same dispatcher used by Native AOT programs.
    const auto checkUdpConnect = [&](const StackValue &ip, const char *family) {
        int connected = -1;
        try {
            frame.push(ip);
            frame.push(9);
            frame.push(1);
            frame.push(1000);
            frame.intrinsic(OP_VM_MACHINE_SOCKET_CONNECT);
            connected = std::get<int>(frame.pop());
        } catch (const runtime::LanguageException &error) {
            const std::string message = sv_to_string(error.value());
            if (message == "FFI: socket_connect OS=1" ||
                message == "FFI: socket_connect OS=13" ||
                (std::string(family) == "IPv6" &&
                 (message == "FFI: socket_connect OS=47" ||
                  message == "FFI: socket_connect OS=97"))) {
                std::cout << "SKIP AOT UDP " << family << ": " << message << '\n';
                return;
            }
            check(false, "AOT UDP machine connect failed unexpectedly");
            return;
        }
        check(program.files.sockets.count(connected) == 1,
              "AOT UDP connect did not register token");
        const int fd = static_cast<int>(program.files.sockets.at(connected).descriptor);
        frame.push(connected);
        frame.intrinsic(OP_VM_MACHINE_SOCKET_KIND);
        check(std::get<int>(frame.pop()) == 1, "AOT UDP kind mismatch");
        frame.push(connected);
        frame.intrinsic(OP_VM_MACHINE_SOCKET_CLOSE);
        check(std::get<int>(frame.pop()) == 0 &&
              program.files.sockets.count(connected) == 0,
              "AOT UDP close left a VM token");
        check(::fcntl(fd, F_GETFD) == -1 && errno == EBADF,
              "AOT UDP descriptor leaked after close");
    };
    checkUdpConnect(make_list_value({127, 0, 0, 1}), "IPv4");
    checkUdpConnect(ip6Loopback, "IPv6");

    // Exercise the native AOT Frame dispatcher rather than calling the
    // shared executor directly. Its byte list must mutate in place.
    int aotPair[2]{-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, aotPair) == 0) {
        constexpr int aotSend = token + 21;
        constexpr int aotRecv = token + 22;
        program.files.sockets.emplace(aotSend,
            runtime::ForeignFileState::Socket{aotPair[0], false, false});
        program.files.sockets.emplace(aotRecv,
            runtime::ForeignFileState::Socket{aotPair[1], false, false});
        frame.push(aotRecv);
        frame.push(1000);
        frame.intrinsic(OP_VM_MACHINE_SOCKET_TIMEOUT);
        check(std::get<int>(frame.pop()) == 1, "AOT timeout parity failed");

        program.capabilities.erase("machine.net.socket.send");
        frame.push(aotSend);
        frame.push(make_list_value({0, 255, 65, 66}));
        frame.push(4);
        frame.push(0);
        bool sendRejected = false;
        try { frame.intrinsic(OP_VM_MACHINE_SOCKET_SEND); }
        catch (const runtime::RuntimeError &) { sendRejected = true; }
        check(sendRejected, "AOT send bypassed revoked capability");
        program.capabilities = caps;
        const auto bytes = make_list_value({0, 255, 65, 66});
        frame.push(aotSend);
        frame.push(bytes);
        frame.push(4);
        frame.push(0);
        frame.intrinsic(OP_VM_MACHINE_SOCKET_SEND);
        check(std::get<int>(frame.pop()) == 4, "AOT send parity failed");
        const auto received = make_list_value({0, 0, 0, 0});
        frame.push(aotRecv);
        frame.push(received);
        frame.push(4);
        frame.push(0);
        frame.intrinsic(OP_VM_MACHINE_SOCKET_RECV);
        check(std::get<int>(frame.pop()) == 4, "AOT recv parity failed");
        const auto &aotBytes = std::get<ListHandle>(received)->elements;
        check(std::get<int>(aotBytes[0]) == 0 &&
              std::get<int>(aotBytes[1]) == 255 &&
              std::get<int>(aotBytes[2]) == 65 &&
              std::get<int>(aotBytes[3]) == 66,
              "AOT recv did not mutate byte list");
        for (int owned : {aotSend, aotRecv}) {
            frame.push(owned);
            frame.intrinsic(OP_VM_MACHINE_SOCKET_CLOSE);
            check(std::get<int>(frame.pop()) == 0, "AOT I/O socket close failed");
        }
        check(program.files.sockets.empty(), "AOT socket tokens leaked");
    } else {
        check(false, "AOT socketpair creation failed");
    }

    // On runners that permit loopback networking, validate the actual accept
    // syscall and token ownership through the AOT dispatcher. Some macOS
    // sandboxes prohibit bind() entirely; report that separately as SKIP.
    const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener >= 0) {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (::bind(listener, reinterpret_cast<sockaddr *>(&address),
                   sizeof(address)) == 0) {
            check(::listen(listener, 1) == 0, "AOT accept fixture listen failed");
            socklen_t length = sizeof(address);
            check(::getsockname(listener, reinterpret_cast<sockaddr *>(&address),
                                &length) == 0,
                  "AOT accept fixture getsockname failed");
            // The client must be opened by the AOT machine intrinsic. The
            // previous fixture used libc connect and only tested AOT accept.
            int clientToken = -1;
            bool connectionRestricted = false;
            program.capabilities.erase("machine.net.socket.connect");
            frame.push(make_list_value({127, 0, 0, 1}));
            frame.push(static_cast<int>(ntohs(address.sin_port)));
            frame.push(0);
            frame.push(1000);
            bool connectRejected = false;
            try { frame.intrinsic(OP_VM_MACHINE_SOCKET_CONNECT); }
            catch (const runtime::RuntimeError &error) {
                connectRejected = error.kind() == runtime::RuntimeErrorKind::CallBoundary;
            }
            check(connectRejected, "AOT connect bypassed revoked capability");
            program.capabilities = caps;
            try {
                frame.push(make_list_value({127, 0, 0, 1}));
                frame.push(static_cast<int>(ntohs(address.sin_port)));
                frame.push(0);
                frame.push(1000);
                frame.intrinsic(OP_VM_MACHINE_SOCKET_CONNECT);
                clientToken = std::get<int>(frame.pop());
            } catch (const runtime::LanguageException &error) {
                const auto message = sv_to_string(error.value());
                connectionRestricted = message == "FFI: socket_connect OS=1" ||
                    message == "FFI: socket_connect OS=13";
                if (!connectionRestricted) check(false, "AOT connect fixture failed");
            }
            if (clientToken >= 0) {
                    check(program.files.sockets.count(clientToken) == 1,
                          "AOT connect did not register VM-owned token");
                    frame.push(clientToken);
                    frame.intrinsic(OP_VM_MACHINE_SOCKET_KIND);
                    check(std::get<int>(frame.pop()) == 0,
                          "AOT connected socket kind should be TCP");
                    constexpr int listenerToken = token + 30;
                    program.files.sockets.emplace(listenerToken,
                        runtime::ForeignFileState::Socket{listener, false, true});
                    program.capabilities.erase("machine.net.socket.accept");
                    frame.push(listenerToken);
                    bool acceptRejected = false;
                    try { frame.intrinsic(OP_VM_MACHINE_SOCKET_ACCEPT); }
                    catch (const runtime::RuntimeError &) { acceptRejected = true; }
                    check(acceptRejected, "AOT accept bypassed revoked capability");
                    program.capabilities = caps;
                    frame.push(listenerToken);
                    frame.intrinsic(OP_VM_MACHINE_SOCKET_ACCEPT);
                    const int accepted = std::get<int>(frame.pop());
                    check(accepted >= token && accepted != listenerToken &&
                          program.files.sockets.count(accepted) == 1,
                          "AOT accept did not allocate a separate owned token");
                    frame.push(accepted);
                    frame.intrinsic(OP_VM_MACHINE_SOCKET_KIND);
                    check(std::get<int>(frame.pop()) == 0,
                          "AOT accepted socket kind should be TCP");
                    frame.push(accepted);
                    frame.intrinsic(OP_VM_MACHINE_SOCKET_CLOSE);
                    check(std::get<int>(frame.pop()) == 0, "AOT accepted socket close failed");
                    frame.push(listenerToken);
                    frame.intrinsic(OP_VM_MACHINE_SOCKET_CLOSE);
                    check(std::get<int>(frame.pop()) == 0, "AOT listener close failed");
                    frame.push(clientToken);
                    frame.intrinsic(OP_VM_MACHINE_SOCKET_CLOSE);
                    check(std::get<int>(frame.pop()) == 0, "AOT connected socket close failed");
                    check(program.files.sockets.empty(), "AOT accepted token leaked");
            } else {
                if (connectionRestricted)
                    std::cout << "SKIP AOT loopback accept: connect prohibited\n";
                (void)::close(listener);
            }
        } else {
            if (errno == EPERM || errno == EACCES)
                std::cout << "SKIP AOT loopback accept: bind prohibited\n";
            else check(false, "AOT accept fixture bind failed");
            (void)::close(listener);
        }
    } else if (errno == EPERM || errno == EACCES) {
        std::cout << "SKIP AOT loopback accept: socket prohibited\n";
    } else {
        check(false, "AOT accept fixture socket failed");
    }

    // Exercise IPv6 TCP through the machine connect path too. IPv4 listener
    // coverage above cannot detect sockaddr_in6 ABI or address packing bugs.
    const int listener6 = ::socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (listener6 >= 0) {
        sockaddr_in6 addr6{};
        addr6.sin6_family = AF_INET6;
        addr6.sin6_addr = in6addr_loopback;
        socklen_t size6 = sizeof(addr6);
        if (::bind(listener6, reinterpret_cast<sockaddr *>(&addr6), size6) == 0 &&
            ::listen(listener6, 1) == 0 &&
            ::getsockname(listener6, reinterpret_cast<sockaddr *>(&addr6),
                          &size6) == 0) {
            int client6 = -1;
            bool restricted = false;
            try {
                frame.push(ip6Loopback);
                frame.push(static_cast<int>(ntohs(addr6.sin6_port)));
                frame.push(0);
                frame.push(1000);
                frame.intrinsic(OP_VM_MACHINE_SOCKET_CONNECT);
                client6 = std::get<int>(frame.pop());
            } catch (const runtime::LanguageException &error) {
                const auto message = sv_to_string(error.value());
                restricted = message == "FFI: socket_connect OS=1" ||
                    message == "FFI: socket_connect OS=13";
                if (!restricted) check(false, "AOT IPv6 TCP connect failed");
            }
            if (client6 >= 0) {
                check(program.files.sockets.count(client6) == 1,
                      "AOT IPv6 TCP token missing");
                const int accepted6 = ::accept(listener6, nullptr, nullptr);
                check(accepted6 >= 0, "AOT IPv6 TCP accept failed");
                if (accepted6 >= 0) (void)::close(accepted6);
                frame.push(client6);
                frame.intrinsic(OP_VM_MACHINE_SOCKET_CLOSE);
                check(std::get<int>(frame.pop()) == 0 &&
                      program.files.sockets.count(client6) == 0,
                      "AOT IPv6 TCP token leaked");
            } else if (restricted) {
                std::cout << "SKIP AOT IPv6 TCP: connect prohibited\n";
            }
        } else {
            const int error = errno;
            if (error == EPERM || error == EACCES || error == EADDRNOTAVAIL)
                std::cout << "SKIP AOT IPv6 TCP: bind prohibited/unavailable\n";
            else check(false, "AOT IPv6 TCP fixture setup failed");
        }
        (void)::close(listener6);
    } else if (errno == EPERM || errno == EACCES || errno == EAFNOSUPPORT ||
               errno == EPROTONOSUPPORT) {
        std::cout << "SKIP AOT IPv6 TCP: IPv6 socket unavailable\n";
    } else {
        check(false, "AOT IPv6 TCP fixture socket failed");
    }
#endif
    if (failures == 0) std::cout << "machine socket hardening passed\n";
    return failures == 0 ? 0 : 1;
}
