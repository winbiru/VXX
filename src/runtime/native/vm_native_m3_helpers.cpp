#include "common/vm_native_m3_helpers.h"
#include "vpp/bytecode/intrinsic.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/vm_native_helpers.h"
#include "vpp/core/text.h"

#if defined(_WIN32)
#ifndef SECURITY_WIN32
#define SECURITY_WIN32
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <security.h>
#include <schannel.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#include <Security/SecureTransport.h>
#else
#include <openssl/err.h>
#include <openssl/ssl.h>
#endif
#endif

namespace vietvm::helpers {
namespace {

bool requireString(const StackValue &value,
                   const std::string &fn,
                   const char *label,
                   std::string &out,
                   std::string &err) {
    if (!std::holds_alternative<std::string>(value)) {
        err = fn + ": " + label + " phải là chuỗi";
        return false;
    }
    out = std::get<std::string>(value);
    if (!vietvm::core::isValidUtf8(out)) {
        err = fn + ": " + label + " không phải UTF-8 hợp lệ";
        return false;
    }
    return true;
}

bool initializeNetwork(std::string &err) {
#if defined(_WIN32)
    static std::once_flag once;
    static int status = 0;
    std::call_once(once, []() {
        WSADATA data{};
        status = WSAStartup(MAKEWORD(2, 2), &data);
    });
    if (status != 0) {
        err = "dns: không khởi tạo được Winsock";
        return false;
    }
#else
    (void)err;
#endif
    return true;
}

#if defined(_WIN32)
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidNativeSocket = INVALID_SOCKET;
#else
using NativeSocket = int;
constexpr NativeSocket kInvalidNativeSocket = -1;
#endif

struct OpenSocket {
    NativeSocket handle = kInvalidNativeSocket;
    int type = SOCK_STREAM;
    bool listener = false;
    std::mutex ioMutex;
};

struct OpenTlsSocket {
    NativeSocket handle = kInvalidNativeSocket;
    std::mutex ioMutex;
#if defined(_WIN32)
    CredHandle credentials{};
    CtxtHandle context{};
    SecPkgContext_StreamSizes sizes{};
    bool credentialsReady = false;
    bool contextReady = false;
    std::vector<unsigned char> encrypted;
    std::vector<unsigned char> decrypted;
#elif defined(__APPLE__)
    SSLContextRef context = nullptr;
#else
    SSL_CTX *context = nullptr;
    SSL *session = nullptr;
#endif
};

std::mutex &socketMutex() {
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<int, std::shared_ptr<OpenSocket>> &openSockets() {
    static std::unordered_map<int, std::shared_ptr<OpenSocket>> sockets;
    return sockets;
}

std::unordered_map<int, std::shared_ptr<OpenTlsSocket>> &openTlsSockets() {
    static std::unordered_map<int, std::shared_ptr<OpenTlsSocket>> sockets;
    return sockets;
}

int &nextSocketId() {
    static int id = 1;
    return id;
}

void closeNativeSocket(NativeSocket socket);

void closeTlsSocket(OpenTlsSocket &tls) {
#if defined(_WIN32)
    if (tls.contextReady) {
        DeleteSecurityContext(&tls.context);
        tls.contextReady = false;
    }
    if (tls.credentialsReady) {
        FreeCredentialHandle(&tls.credentials);
        tls.credentialsReady = false;
    }
#elif defined(__APPLE__)
    if (tls.context != nullptr) {
        (void)SSLClose(tls.context);
        CFRelease(tls.context);
        tls.context = nullptr;
    }
#else
    if (tls.session != nullptr) {
        (void)SSL_shutdown(tls.session);
        SSL_free(tls.session);
        tls.session = nullptr;
    }
    if (tls.context != nullptr) {
        SSL_CTX_free(tls.context);
        tls.context = nullptr;
    }
#endif
    closeNativeSocket(tls.handle);
    tls.handle = kInvalidNativeSocket;
}

void closeNativeSocket(NativeSocket socket) {
    if (socket == kInvalidNativeSocket) return;
#if defined(_WIN32)
    closesocket(socket);
#else
    close(socket);
#endif
}

int lastSocketError() {
#if defined(_WIN32)
    return WSAGetLastError();
#else
    return errno;
#endif
}

bool socketWouldBlock(int code) {
#if defined(_WIN32)
    return code == WSAEWOULDBLOCK || code == WSAEINPROGRESS ||
           code == WSAEINVAL;
#else
    return code == EINPROGRESS || code == EWOULDBLOCK || code == EAGAIN;
#endif
}

bool setSocketBlocking(NativeSocket socket, bool blocking) {
#if defined(_WIN32)
    u_long mode = blocking ? 0UL : 1UL;
    return ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0) return false;
    const int desired = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
    return fcntl(socket, F_SETFL, desired) == 0;
#endif
}

bool setSocketTimeout(NativeSocket socket, int timeoutMs) {
#if defined(_WIN32)
    const DWORD timeout = static_cast<DWORD>(timeoutMs);
    return setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                      reinterpret_cast<const char *>(&timeout),
                      sizeof(timeout)) == 0 &&
           setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO,
                      reinterpret_cast<const char *>(&timeout),
                      sizeof(timeout)) == 0;
#else
    timeval timeout{};
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = (timeoutMs % 1000) * 1000;
    return setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                      sizeof(timeout)) == 0 &&
           setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout,
                      sizeof(timeout)) == 0;
#endif
}

bool connectWithTimeout(NativeSocket socket,
                        const sockaddr *address,
                        socklen_t addressLength,
                        int timeoutMs,
                        std::string &err) {
    if (!setSocketBlocking(socket, false)) {
        err = "socket: không đặt được chế độ non-blocking";
        return false;
    }
    if (::connect(socket, address, addressLength) == 0) {
        (void)setSocketBlocking(socket, true);
        return true;
    }
    const int connectError = lastSocketError();
    if (!socketWouldBlock(connectError)) {
        (void)setSocketBlocking(socket, true);
        return false;
    }

    fd_set writeSet;
    FD_ZERO(&writeSet);
    FD_SET(socket, &writeSet);
    timeval timeout{};
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = (timeoutMs % 1000) * 1000;
#if defined(_WIN32)
    const int selected = select(0, nullptr, &writeSet, nullptr, &timeout);
#else
    const int selected = select(socket + 1, nullptr, &writeSet, nullptr, &timeout);
#endif
    if (selected <= 0) {
        (void)setSocketBlocking(socket, true);
        if (selected == 0) err = "socket: hết thời gian chờ kết nối";
        return false;
    }

    int socketError = 0;
    socklen_t errorLength = static_cast<socklen_t>(sizeof(socketError));
#if defined(_WIN32)
    if (getsockopt(socket, SOL_SOCKET, SO_ERROR,
                   reinterpret_cast<char *>(&socketError), &errorLength) != 0) {
#else
    if (getsockopt(socket, SOL_SOCKET, SO_ERROR, &socketError, &errorLength) != 0) {
#endif
        (void)setSocketBlocking(socket, true);
        return false;
    }
    (void)setSocketBlocking(socket, true);
    return socketError == 0;
}

bool openConnectedNativeSocket(const std::string &fn,
                               const std::string &host,
                               int port,
                               int timeoutMs,
                               int socketType,
                               NativeSocket &connected,
                               std::string &err) {
    if (!initializeNetwork(err)) return false;

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = socketType;
    hints.ai_protocol = socketType == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP;
    addrinfo *head = nullptr;
    const std::string service = std::to_string(port);
    const int status = getaddrinfo(host.c_str(), service.c_str(), &hints, &head);
    if (status != 0 || head == nullptr) {
#if defined(_WIN32)
        err = fn + ": không phân giải được địa chỉ, mã " + std::to_string(status);
#else
        err = fn + ": không phân giải được địa chỉ: " + std::string(gai_strerror(status));
#endif
        if (head != nullptr) freeaddrinfo(head);
        return false;
    }

    connected = kInvalidNativeSocket;
    for (addrinfo *entry = head; entry != nullptr; entry = entry->ai_next) {
        NativeSocket candidate = ::socket(entry->ai_family, entry->ai_socktype,
                                          entry->ai_protocol);
        if (candidate == kInvalidNativeSocket) continue;
#if defined(SO_NOSIGPIPE)
        int noSigPipe = 1;
        (void)setsockopt(candidate, SOL_SOCKET, SO_NOSIGPIPE, &noSigPipe,
                         sizeof(noSigPipe));
#endif
        std::string connectError;
        const bool ok = connectWithTimeout(
            candidate, entry->ai_addr,
            static_cast<socklen_t>(entry->ai_addrlen), timeoutMs, connectError);
        if (ok && setSocketTimeout(candidate, timeoutMs)) {
            connected = candidate;
            break;
        }
        // Preserve the public operation for timeout/non-blocking failures too.
        if (!connectError.empty()) err = fn + ": " + connectError;
        closeNativeSocket(candidate);
    }
    freeaddrinfo(head);

    if (connected == kInvalidNativeSocket) {
        if (err.empty()) {
            err = fn + ": không kết nối được, mã " +
                  std::to_string(lastSocketError());
        }
        return false;
    }
    return true;
}

bool sendNativeAll(NativeSocket socket,
                   const unsigned char *data,
                   std::size_t size) {
    std::size_t sentTotal = 0;
    while (sentTotal < size) {
        const std::size_t remaining = size - sentTotal;
        const int chunk = static_cast<int>(
            remaining > static_cast<std::size_t>(INT_MAX) ? INT_MAX : remaining);
#if defined(MSG_NOSIGNAL)
        constexpr int flags = MSG_NOSIGNAL;
#else
        constexpr int flags = 0;
#endif
        const int sent = ::send(
            socket,
            reinterpret_cast<const char *>(data + sentTotal),
            chunk,
            flags);
        if (sent <= 0) return false;
        sentTotal += static_cast<std::size_t>(sent);
    }
    return true;
}

#if defined(_WIN32)

bool utf8ToWideHost(const std::string &host, std::wstring &wide) {
    const int count = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, host.data(),
        static_cast<int>(host.size()), nullptr, 0);
    if (count <= 0) return false;
    wide.assign(static_cast<std::size_t>(count), L'\0');
    return MultiByteToWideChar(
               CP_UTF8, MB_ERR_INVALID_CHARS, host.data(),
               static_cast<int>(host.size()), wide.data(), count) == count;
}

bool schannelSendToken(NativeSocket socket, SecBuffer &buffer) {
    if (buffer.pvBuffer == nullptr || buffer.cbBuffer == 0) return true;
    const bool ok = sendNativeAll(
        socket,
        static_cast<const unsigned char *>(buffer.pvBuffer),
        static_cast<std::size_t>(buffer.cbBuffer));
    FreeContextBuffer(buffer.pvBuffer);
    buffer.pvBuffer = nullptr;
    buffer.cbBuffer = 0;
    return ok;
}

bool initializeTlsClient(OpenTlsSocket &tls,
                         const std::string &host,
                         std::string &err) {
    std::wstring wideHost;
    if (!utf8ToWideHost(host, wideHost)) {
        err = "socket_tls_mo: tên máy UTF-8 không hợp lệ";
        return false;
    }

    SCHANNEL_CRED credentials{};
    credentials.dwVersion = SCHANNEL_CRED_VERSION;
    credentials.dwFlags = SCH_CRED_AUTO_CRED_VALIDATION |
                          SCH_CRED_NO_DEFAULT_CREDS |
                          SCH_USE_STRONG_CRYPTO;
    credentials.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT;
#if defined(SP_PROT_TLS1_3_CLIENT)
    credentials.grbitEnabledProtocols |= SP_PROT_TLS1_3_CLIENT;
#endif
    TimeStamp credentialExpiry{};
    SECURITY_STATUS status = AcquireCredentialsHandleW(
        nullptr,
        const_cast<wchar_t *>(UNISP_NAME_W),
        SECPKG_CRED_OUTBOUND,
        nullptr,
        &credentials,
        nullptr,
        nullptr,
        &tls.credentials,
        &credentialExpiry);
    if (status != SEC_E_OK) {
        err = "socket_tls_mo: SChannel không tạo được credential, mã " +
              std::to_string(static_cast<long>(status));
        return false;
    }
    tls.credentialsReady = true;

    constexpr DWORD requestFlags = ISC_REQ_SEQUENCE_DETECT |
                                   ISC_REQ_REPLAY_DETECT |
                                   ISC_REQ_CONFIDENTIALITY |
                                   ISC_REQ_EXTENDED_ERROR |
                                   ISC_REQ_ALLOCATE_MEMORY |
                                   ISC_REQ_STREAM;
    DWORD attributes = 0;
    TimeStamp contextExpiry{};
    SecBuffer outputBuffer{};
    outputBuffer.BufferType = SECBUFFER_TOKEN;
    SecBufferDesc outputDesc{};
    outputDesc.ulVersion = SECBUFFER_VERSION;
    outputDesc.cBuffers = 1;
    outputDesc.pBuffers = &outputBuffer;

    status = InitializeSecurityContextW(
        &tls.credentials,
        nullptr,
        const_cast<wchar_t *>(wideHost.c_str()),
        requestFlags,
        0,
        SECURITY_NATIVE_DREP,
        nullptr,
        0,
        &tls.context,
        &outputDesc,
        &attributes,
        &contextExpiry);
    if (status != SEC_I_CONTINUE_NEEDED && status != SEC_E_OK) {
        if (outputBuffer.pvBuffer != nullptr) FreeContextBuffer(outputBuffer.pvBuffer);
        err = "socket_tls_mo: SChannel handshake thất bại, mã " +
              std::to_string(static_cast<long>(status));
        return false;
    }
    tls.contextReady = true;
    if (!schannelSendToken(tls.handle, outputBuffer)) {
        err = "socket_tls_mo: gửi TLS handshake thất bại";
        return false;
    }

    std::vector<unsigned char> input;
    bool handshakeComplete = status == SEC_E_OK;
    while (!handshakeComplete) {
        unsigned char chunk[16384];
        const int received = ::recv(
            tls.handle, reinterpret_cast<char *>(chunk), sizeof(chunk), 0);
        if (received <= 0) {
            err = "socket_tls_mo: kết nối đóng trong TLS handshake";
            return false;
        }
        input.insert(input.end(), chunk, chunk + received);

        bool needNetworkInput = false;
        while (!handshakeComplete && !needNetworkInput) {
            SecBuffer inputBuffers[2]{};
            inputBuffers[0].BufferType = SECBUFFER_TOKEN;
            inputBuffers[0].pvBuffer = input.data();
            inputBuffers[0].cbBuffer = static_cast<unsigned long>(input.size());
            inputBuffers[1].BufferType = SECBUFFER_EMPTY;
            SecBufferDesc inputDesc{};
            inputDesc.ulVersion = SECBUFFER_VERSION;
            inputDesc.cBuffers = 2;
            inputDesc.pBuffers = inputBuffers;

            outputBuffer = {};
            outputBuffer.BufferType = SECBUFFER_TOKEN;
            outputDesc.pBuffers = &outputBuffer;
            const SECURITY_STATUS stepStatus = InitializeSecurityContextW(
                &tls.credentials,
                &tls.context,
                const_cast<wchar_t *>(wideHost.c_str()),
                requestFlags,
                0,
                SECURITY_NATIVE_DREP,
                &inputDesc,
                0,
                &tls.context,
                &outputDesc,
                &attributes,
                &contextExpiry);

            if (stepStatus == SEC_E_INCOMPLETE_MESSAGE) {
                if (outputBuffer.pvBuffer != nullptr) FreeContextBuffer(outputBuffer.pvBuffer);
                needNetworkInput = true;
                continue;
            }
            if (!schannelSendToken(tls.handle, outputBuffer)) {
                err = "socket_tls_mo: gửi TLS handshake thất bại";
                return false;
            }
            if (stepStatus != SEC_I_CONTINUE_NEEDED && stepStatus != SEC_E_OK) {
                err = "socket_tls_mo: SChannel handshake thất bại, mã " +
                      std::to_string(static_cast<long>(stepStatus));
                return false;
            }

            std::vector<unsigned char> extraInput;
            if (inputBuffers[1].BufferType == SECBUFFER_EXTRA &&
                inputBuffers[1].cbBuffer > 0) {
                const auto *extra = static_cast<const unsigned char *>(
                    inputBuffers[1].pvBuffer);
                extraInput.assign(extra, extra + inputBuffers[1].cbBuffer);
            }
            input = std::move(extraInput);
            if (stepStatus == SEC_E_OK) {
                handshakeComplete = true;
            } else if (input.empty()) {
                needNetworkInput = true;
            }
        }
    }

    status = QueryContextAttributesW(
        &tls.context, SECPKG_ATTR_STREAM_SIZES, &tls.sizes);
    if (status != SEC_E_OK) {
        err = "socket_tls_mo: không đọc được kích thước TLS stream";
        return false;
    }
    tls.encrypted = std::move(input);
    return true;
}

bool tlsWrite(OpenTlsSocket &tls,
              const std::string &payload,
              int &written,
              std::string &err) {
    written = 0;
    if (payload.empty()) return true;
    const std::size_t chunkSize = std::min<std::size_t>(
        payload.size(), static_cast<std::size_t>(tls.sizes.cbMaximumMessage));
    std::vector<unsigned char> buffer(
        static_cast<std::size_t>(tls.sizes.cbHeader) + chunkSize +
        static_cast<std::size_t>(tls.sizes.cbTrailer));
    std::memcpy(
        buffer.data() + tls.sizes.cbHeader,
        payload.data(),
        chunkSize);

    SecBuffer buffers[4]{};
    buffers[0].BufferType = SECBUFFER_STREAM_HEADER;
    buffers[0].pvBuffer = buffer.data();
    buffers[0].cbBuffer = tls.sizes.cbHeader;
    buffers[1].BufferType = SECBUFFER_DATA;
    buffers[1].pvBuffer = buffer.data() + tls.sizes.cbHeader;
    buffers[1].cbBuffer = static_cast<unsigned long>(chunkSize);
    buffers[2].BufferType = SECBUFFER_STREAM_TRAILER;
    buffers[2].pvBuffer = buffer.data() + tls.sizes.cbHeader + chunkSize;
    buffers[2].cbBuffer = tls.sizes.cbTrailer;
    buffers[3].BufferType = SECBUFFER_EMPTY;
    SecBufferDesc desc{};
    desc.ulVersion = SECBUFFER_VERSION;
    desc.cBuffers = 4;
    desc.pBuffers = buffers;
    const SECURITY_STATUS status = EncryptMessage(&tls.context, 0, &desc, 0);
    if (status != SEC_E_OK) {
        err = "socket_gui: SChannel EncryptMessage thất bại";
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        if (buffers[i].cbBuffer > 0 && !sendNativeAll(
                tls.handle,
                static_cast<const unsigned char *>(buffers[i].pvBuffer),
                buffers[i].cbBuffer)) {
            err = "socket_gui: gửi TLS thất bại";
            return false;
        }
    }
    written = static_cast<int>(chunkSize);
    return true;
}

bool tlsRead(OpenTlsSocket &tls,
             int maximum,
             std::string &out,
             std::string &err) {
    if (!tls.decrypted.empty()) {
        const std::size_t count = std::min<std::size_t>(
            tls.decrypted.size(), static_cast<std::size_t>(maximum));
        out.assign(reinterpret_cast<const char *>(tls.decrypted.data()), count);
        tls.decrypted.erase(tls.decrypted.begin(), tls.decrypted.begin() + count);
        return true;
    }

    while (true) {
        if (tls.encrypted.empty()) {
            unsigned char chunk[16384];
            const int received = ::recv(
                tls.handle, reinterpret_cast<char *>(chunk), sizeof(chunk), 0);
            if (received == 0) {
                out.clear();
                return true;
            }
            if (received < 0) {
                err = "socket_nhan: nhận TLS thất bại, mã " +
                      std::to_string(lastSocketError());
                return false;
            }
            tls.encrypted.insert(tls.encrypted.end(), chunk, chunk + received);
        }

        SecBuffer buffers[4]{};
        buffers[0].BufferType = SECBUFFER_DATA;
        buffers[0].pvBuffer = tls.encrypted.data();
        buffers[0].cbBuffer = static_cast<unsigned long>(tls.encrypted.size());
        buffers[1].BufferType = SECBUFFER_EMPTY;
        buffers[2].BufferType = SECBUFFER_EMPTY;
        buffers[3].BufferType = SECBUFFER_EMPTY;
        SecBufferDesc desc{};
        desc.ulVersion = SECBUFFER_VERSION;
        desc.cBuffers = 4;
        desc.pBuffers = buffers;
        const SECURITY_STATUS status = DecryptMessage(&tls.context, &desc, 0, nullptr);
        if (status == SEC_E_INCOMPLETE_MESSAGE) {
            unsigned char chunk[16384];
            const int received = ::recv(
                tls.handle, reinterpret_cast<char *>(chunk), sizeof(chunk), 0);
            if (received <= 0) {
                err = "socket_nhan: kết nối đóng giữa TLS record";
                return false;
            }
            tls.encrypted.insert(tls.encrypted.end(), chunk, chunk + received);
            continue;
        }
        if (status == SEC_I_CONTEXT_EXPIRED) {
            out.clear();
            tls.encrypted.clear();
            return true;
        }
        if (status != SEC_E_OK && status != SEC_I_RENEGOTIATE) {
            err = "socket_nhan: SChannel DecryptMessage thất bại";
            return false;
        }

        std::vector<unsigned char> extra;
        for (SecBuffer &buffer : buffers) {
            if (buffer.BufferType == SECBUFFER_DATA && buffer.cbBuffer > 0) {
                const auto *data = static_cast<const unsigned char *>(buffer.pvBuffer);
                tls.decrypted.insert(tls.decrypted.end(), data, data + buffer.cbBuffer);
            } else if (buffer.BufferType == SECBUFFER_EXTRA && buffer.cbBuffer > 0) {
                const auto *data = static_cast<const unsigned char *>(buffer.pvBuffer);
                extra.assign(data, data + buffer.cbBuffer);
            }
        }
        tls.encrypted = std::move(extra);
        if (!tls.decrypted.empty()) {
            const std::size_t count = std::min<std::size_t>(
                tls.decrypted.size(), static_cast<std::size_t>(maximum));
            out.assign(reinterpret_cast<const char *>(tls.decrypted.data()), count);
            tls.decrypted.erase(tls.decrypted.begin(), tls.decrypted.begin() + count);
            return true;
        }
        if (status == SEC_I_RENEGOTIATE) {
            err = "socket_nhan: TLS renegotiation chưa được hỗ trợ";
            return false;
        }
    }
}

#elif defined(__APPLE__)

OSStatus secureTransportRead(SSLConnectionRef connection,
                             void *data,
                             std::size_t *dataLength) {
    const NativeSocket socket = static_cast<NativeSocket>(
        reinterpret_cast<intptr_t>(connection));
    const std::size_t wanted = *dataLength;
    const int chunk = static_cast<int>(
        wanted > static_cast<std::size_t>(INT_MAX) ? INT_MAX : wanted);
    const int received = ::recv(socket, static_cast<char *>(data), chunk, 0);
    if (received > 0) {
        *dataLength = static_cast<std::size_t>(received);
        return noErr;
    }
    *dataLength = 0;
    if (received == 0) return errSSLClosedGraceful;
    const int code = lastSocketError();
    if (code == EAGAIN || code == EWOULDBLOCK) return errSSLWouldBlock;
    return errSSLClosedAbort;
}

OSStatus secureTransportWrite(SSLConnectionRef connection,
                              const void *data,
                              std::size_t *dataLength) {
    const NativeSocket socket = static_cast<NativeSocket>(
        reinterpret_cast<intptr_t>(connection));
    const std::size_t requested = *dataLength;
    if (!sendNativeAll(
            socket, static_cast<const unsigned char *>(data), requested)) {
        *dataLength = 0;
        return errSSLClosedAbort;
    }
    *dataLength = requested;
    return noErr;
}

bool validateSecureTransportPeer(OpenTlsSocket &tls,
                                 const std::string &host,
                                 std::string &err) {
    SecTrustRef trust = nullptr;
    OSStatus status = SSLCopyPeerTrust(tls.context, &trust);
    if (status != noErr || trust == nullptr) {
        if (trust != nullptr) CFRelease(trust);
        err = "socket_tls_mo: không lấy được chứng chỉ máy chủ, mã " +
              std::to_string(status);
        return false;
    }

    CFStringRef peerName = CFStringCreateWithBytes(
        kCFAllocatorDefault,
        reinterpret_cast<const UInt8 *>(host.data()),
        static_cast<CFIndex>(host.size()),
        kCFStringEncodingUTF8,
        false);
    if (peerName == nullptr) {
        CFRelease(trust);
        err = "socket_tls_mo: tên máy TLS không hợp lệ";
        return false;
    }

    SecPolicyRef policy = SecPolicyCreateSSL(true, peerName);
    CFRelease(peerName);
    if (policy == nullptr) {
        CFRelease(trust);
        err = "socket_tls_mo: không tạo được chính sách xác thực TLS";
        return false;
    }

    status = SecTrustSetPolicies(trust, policy);
    CFRelease(policy);
    if (status != errSecSuccess) {
        CFRelease(trust);
        err = "socket_tls_mo: không cấu hình được xác thực chứng chỉ, mã " +
              std::to_string(status);
        return false;
    }

    CFErrorRef trustError = nullptr;
    const bool trusted = SecTrustEvaluateWithError(trust, &trustError);
    if (trustError != nullptr) CFRelease(trustError);
    CFRelease(trust);
    if (!trusted) {
        err = "socket_tls_mo: chứng chỉ TLS không tin cậy hoặc không khớp tên máy";
        return false;
    }
    return true;
}

bool initializeTlsClient(OpenTlsSocket &tls,
                         const std::string &host,
                         std::string &err) {
    tls.context = SSLCreateContext(
        kCFAllocatorDefault, kSSLClientSide, kSSLStreamType);
    if (tls.context == nullptr) {
        err = "socket_tls_mo: không tạo được SecureTransport context";
        return false;
    }
    OSStatus status = SSLSetIOFuncs(
        tls.context, secureTransportRead, secureTransportWrite);
    if (status == noErr) {
        status = SSLSetConnection(
            tls.context,
            reinterpret_cast<SSLConnectionRef>(
                static_cast<intptr_t>(tls.handle)));
    }
    if (status == noErr) {
        status = SSLSetPeerDomainName(tls.context, host.data(), host.size());
    }
    if (status == noErr) {
        status = SSLSetProtocolVersionMin(tls.context, kTLSProtocol12);
    }
    if (status != noErr) {
        err = "socket_tls_mo: cấu hình SecureTransport thất bại, mã " +
              std::to_string(status);
        return false;
    }
    do {
        status = SSLHandshake(tls.context);
    } while (status == errSSLWouldBlock);
    if (status != noErr) {
        err = "socket_tls_mo: TLS handshake thất bại, mã " +
              std::to_string(status);
        return false;
    }
    return validateSecureTransportPeer(tls, host, err);
}

bool tlsWrite(OpenTlsSocket &tls,
              const std::string &payload,
              int &written,
              std::string &err) {
    written = 0;
    if (payload.empty()) return true;
    std::size_t processed = 0;
    const OSStatus status = SSLWrite(
        tls.context, payload.data(), payload.size(), &processed);
    if (status != noErr && status != errSSLWouldBlock) {
        err = "socket_gui: TLS gửi thất bại, mã " + std::to_string(status);
        return false;
    }
    written = static_cast<int>(processed);
    return true;
}

bool tlsRead(OpenTlsSocket &tls,
             int maximum,
             std::string &out,
             std::string &err) {
    std::string buffer(static_cast<std::size_t>(maximum), '\0');
    std::size_t processed = 0;
    OSStatus status = noErr;
    do {
        status = SSLRead(
            tls.context, buffer.data(), buffer.size(), &processed);
    } while (status == errSSLWouldBlock && processed == 0);
    if (processed > 0) {
        buffer.resize(processed);
        out = std::move(buffer);
        return true;
    }
    if (status == errSSLClosedGraceful || status == errSSLClosedNoNotify) {
        out.clear();
        return true;
    }
    if (status != noErr) {
        err = "socket_nhan: TLS nhận thất bại, mã " + std::to_string(status);
        return false;
    }
    out.clear();
    return true;
}

#else

std::string opensslErrorText() {
    const unsigned long code = ERR_get_error();
    if (code == 0) return "không rõ";
    char buffer[256]{};
    ERR_error_string_n(code, buffer, sizeof(buffer));
    return buffer;
}

bool isIpLiteralHost(const std::string &host) {
    in_addr ipv4{};
    in6_addr ipv6{};
    return inet_pton(AF_INET, host.c_str(), &ipv4) == 1 ||
           inet_pton(AF_INET6, host.c_str(), &ipv6) == 1;
}

bool initializeTlsClient(OpenTlsSocket &tls,
                         const std::string &host,
                         std::string &err) {
    tls.context = SSL_CTX_new(TLS_client_method());
    if (tls.context == nullptr) {
        err = "socket_tls_mo: không tạo được OpenSSL context: " + opensslErrorText();
        return false;
    }
    if (SSL_CTX_set_min_proto_version(tls.context, TLS1_2_VERSION) != 1 ||
        SSL_CTX_set_default_verify_paths(tls.context) != 1) {
        err = "socket_tls_mo: không cấu hình được OpenSSL: " + opensslErrorText();
        return false;
    }
    SSL_CTX_set_verify(tls.context, SSL_VERIFY_PEER, nullptr);
    tls.session = SSL_new(tls.context);
    if (tls.session == nullptr) {
        err = "socket_tls_mo: không tạo được OpenSSL session: " + opensslErrorText();
        return false;
    }
    const bool ipLiteral = isIpLiteralHost(host);
    if (SSL_set_fd(tls.session, tls.handle) != 1) {
        err = "socket_tls_mo: không gắn được TLS socket/SNI: " + opensslErrorText();
        return false;
    }
    if (!ipLiteral && SSL_set_tlsext_host_name(tls.session, host.c_str()) != 1) {
        err = "socket_tls_mo: không cấu hình được TLS SNI: " + opensslErrorText();
        return false;
    }
    X509_VERIFY_PARAM *params = SSL_get0_param(tls.session);
    const int verifyConfigured = params == nullptr
        ? 0
        : (ipLiteral
            ? X509_VERIFY_PARAM_set1_ip_asc(params, host.c_str())
            : X509_VERIFY_PARAM_set1_host(params, host.c_str(), 0));
    if (verifyConfigured != 1) {
        err = "socket_tls_mo: không cấu hình được xác minh hostname";
        return false;
    }
    if (SSL_connect(tls.session) != 1) {
        err = "socket_tls_mo: TLS handshake thất bại: " + opensslErrorText();
        return false;
    }
    return true;
}

bool tlsWrite(OpenTlsSocket &tls,
              const std::string &payload,
              int &written,
              std::string &err) {
    written = 0;
    if (payload.empty()) return true;
    const int chunk = static_cast<int>(
        payload.size() > static_cast<std::size_t>(INT_MAX) ? INT_MAX : payload.size());
    const int count = SSL_write(tls.session, payload.data(), chunk);
    if (count <= 0) {
        err = "socket_gui: TLS gửi thất bại: " + opensslErrorText();
        return false;
    }
    written = count;
    return true;
}

bool tlsRead(OpenTlsSocket &tls,
             int maximum,
             std::string &out,
             std::string &err) {
    std::string buffer(static_cast<std::size_t>(maximum), '\0');
    const int received = SSL_read(tls.session, buffer.data(), maximum);
    if (received > 0) {
        buffer.resize(static_cast<std::size_t>(received));
        out = std::move(buffer);
        return true;
    }
    const int sslError = SSL_get_error(tls.session, received);
    if (sslError == SSL_ERROR_ZERO_RETURN) {
        out.clear();
        return true;
    }
    err = "socket_nhan: TLS nhận thất bại: " + opensslErrorText();
    return false;
}

#endif

bool socketTlsOpen(const std::string &fn,
                   const std::vector<StackValue> &args,
                   StackValue &result,
                   std::string &err) {
    if (!requireNativeArgumentCount(args, fn, 3, err)) return true;
    std::string host;
    int port = 0;
    int timeoutMs = 0;
    if (!requireString(args[0], fn, "tên máy", host, err) ||
        !requireIntArgFromStack(args[1], fn, "cổng", port, err) ||
        !requireIntArgFromStack(args[2], fn, "thời gian chờ", timeoutMs, err)) {
        return true;
    }
    if (host.empty()) {
        err = fn + ": tên máy không được rỗng";
        return true;
    }
    if (port <= 0 || port > 65535) {
        err = fn + ": cổng phải trong 1..65535";
        return true;
    }
    if (timeoutMs <= 0 || timeoutMs > 300000) {
        err = fn + ": thời gian chờ phải trong 1..300000 ms";
        return true;
    }

    auto tls = std::make_shared<OpenTlsSocket>();
    if (!openConnectedNativeSocket(
            fn, host, port, timeoutMs, SOCK_STREAM, tls->handle, err)) return true;
    if (!initializeTlsClient(*tls, host, err)) {
        closeTlsSocket(*tls);
        return true;
    }

    std::lock_guard<std::mutex> lock(socketMutex());
    const int id = nextSocketId()++;
    openTlsSockets().emplace(id, std::move(tls));
    result = make_int_value(id);
    return true;
}

bool socketOpen(const std::string &fn,
                const std::vector<StackValue> &args,
                int socketType,
                StackValue &result,
                std::string &err) {
    if (!requireNativeArgumentCount(args, fn, 3, err)) return true;
    std::string host;
    int port = 0;
    int timeoutMs = 0;
    if (!requireString(args[0], fn, "tên máy", host, err) ||
        !requireIntArgFromStack(args[1], fn, "cổng", port, err) ||
        !requireIntArgFromStack(args[2], fn, "thời gian chờ", timeoutMs, err)) {
        return true;
    }
    if (host.empty()) {
        err = fn + ": tên máy không được rỗng";
        return true;
    }
    if (port <= 0 || port > 65535) {
        err = fn + ": cổng phải trong 1..65535";
        return true;
    }
    if (timeoutMs <= 0 || timeoutMs > 300000) {
        err = fn + ": thời gian chờ phải trong 1..300000 ms";
        return true;
    }
    NativeSocket connected = kInvalidNativeSocket;
    if (!openConnectedNativeSocket(
            fn, host, port, timeoutMs, socketType, connected, err)) return true;

    auto socket = std::make_shared<OpenSocket>();
    socket->handle = connected;
    socket->type = socketType;
    socket->listener = false;
    std::lock_guard<std::mutex> lock(socketMutex());
    const int id = nextSocketId()++;
    openSockets().emplace(id, std::move(socket));
    result = make_int_value(id);
    return true;
}

bool socketListen(const std::string &fn,
                  const std::vector<StackValue> &args,
                  StackValue &result,
                  std::string &err) {
    if (!requireNativeArgumentCount(args, fn, 2, err)) return true;
    int port = 0;
    int backlog = 0;
    if (!requireIntArgFromStack(args[0], fn, "cổng", port, err) ||
        !requireIntArgFromStack(args[1], fn, "backlog", backlog, err)) {
        return true;
    }
    if (port <= 0 || port > 65535) {
        err = fn + ": cổng phải trong 1..65535";
        return true;
    }
    if (backlog <= 0 || backlog > 1024) {
        err = fn + ": backlog phải trong 1..1024";
        return true;
    }
    if (!initializeNetwork(err)) return true;

    NativeSocket listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == kInvalidNativeSocket) {
        err = fn + ": không tạo được socket, mã " +
              std::to_string(lastSocketError());
        return true;
    }
#if defined(SO_NOSIGPIPE)
    int noSigPipe = 1;
    (void)setsockopt(listener, SOL_SOCKET, SO_NOSIGPIPE, &noSigPipe,
                     sizeof(noSigPipe));
#endif
    int reuse = 1;
#if defined(_WIN32)
    (void)setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char *>(&reuse), sizeof(reuse));
#else
    (void)setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(static_cast<unsigned short>(port));
    if (::bind(listener, reinterpret_cast<const sockaddr *>(&address),
               static_cast<socklen_t>(sizeof(address))) != 0) {
        const int code = lastSocketError();
        closeNativeSocket(listener);
        err = fn + ": bind thất bại, mã " + std::to_string(code);
        return true;
    }
    if (::listen(listener, backlog) != 0) {
        const int code = lastSocketError();
        closeNativeSocket(listener);
        err = fn + ": listen thất bại, mã " + std::to_string(code);
        return true;
    }

    auto socket = std::make_shared<OpenSocket>();
    socket->handle = listener;
    socket->type = SOCK_STREAM;
    socket->listener = true;
    std::lock_guard<std::mutex> lock(socketMutex());
    const int id = nextSocketId()++;
    openSockets().emplace(id, std::move(socket));
    result = make_int_value(id);
    return true;
}

bool socketAccept(const std::string &fn,
                  const std::vector<StackValue> &args,
                  StackValue &result,
                  std::string &err) {
    if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
    int id = 0;
    if (!requireIntArgFromStack(args[0], fn, "mã socket lắng nghe", id, err) || id <= 0) {
        if (err.empty()) err = fn + ": mã socket lắng nghe không hợp lệ";
        return true;
    }

    std::shared_ptr<OpenSocket> listenerEntry;
    {
        std::lock_guard<std::mutex> lock(socketMutex());
        const auto found = openSockets().find(id);
        if (found == openSockets().end() || !found->second->listener) {
            err = fn + ": socket lắng nghe đã đóng hoặc không tồn tại";
            return true;
        }
        listenerEntry = found->second;
    }
    NativeSocket listener = kInvalidNativeSocket;
    {
        std::lock_guard<std::mutex> ioLock(listenerEntry->ioMutex);
        if (listenerEntry->handle == kInvalidNativeSocket) {
            err = fn + ": socket lắng nghe đã đóng hoặc không tồn tại";
            return true;
        }
        listener = listenerEntry->handle;
    }

    sockaddr_storage peer{};
    socklen_t peerLength = static_cast<socklen_t>(sizeof(peer));
    NativeSocket accepted = ::accept(
        listener, reinterpret_cast<sockaddr *>(&peer), &peerLength);
    if (accepted == kInvalidNativeSocket) {
        err = fn + ": accept thất bại, mã " +
              std::to_string(lastSocketError());
        return true;
    }
#if defined(SO_NOSIGPIPE)
    int noSigPipe = 1;
    (void)setsockopt(accepted, SOL_SOCKET, SO_NOSIGPIPE, &noSigPipe,
                     sizeof(noSigPipe));
#endif
    if (!setSocketTimeout(accepted, 300000)) {
        closeNativeSocket(accepted);
        err = fn + ": không đặt được timeout cho socket đã accept";
        return true;
    }

    auto socket = std::make_shared<OpenSocket>();
    socket->handle = accepted;
    socket->type = SOCK_STREAM;
    socket->listener = false;
    std::lock_guard<std::mutex> lock(socketMutex());
    const int acceptedId = nextSocketId()++;
    openSockets().emplace(acceptedId, std::move(socket));
    result = make_int_value(acceptedId);
    return true;
}

bool handleSockets(const std::string &fn,
                   const std::vector<StackValue> &args,
                   StackValue &result,
                   std::string &err) {
    if (fn == "socket_tcp_mo") {
        return socketOpen(fn, args, SOCK_STREAM, result, err);
    }
    if (fn == "socket_udp_mo") {
        return socketOpen(fn, args, SOCK_DGRAM, result, err);
    }
    if (fn == "socket_tls_mo") {
        return socketTlsOpen(fn, args, result, err);
    }
    if (fn == "socket_tcp_lang_nghe") {
        return socketListen(fn, args, result, err);
    }
    if (fn == "socket_chap_nhan") {
        return socketAccept(fn, args, result, err);
    }
    if (fn != "socket_gui" && fn != "socket_nhan" &&
        fn != "socket_dat_timeout" && fn != "socket_dong") {
        return false;
    }
    const int expected = fn == "socket_gui" || fn == "socket_nhan" ||
                                 fn == "socket_dat_timeout"
                             ? 2
                             : 1;
    if (!requireNativeArgumentCount(args, fn, expected, err)) return true;
    int id = 0;
    if (!requireIntArgFromStack(args[0], fn, "mã socket", id, err) || id <= 0) {
        if (err.empty()) err = fn + ": mã socket không hợp lệ";
        return true;
    }

    std::shared_ptr<OpenSocket> socketEntry;
    std::shared_ptr<OpenTlsSocket> tlsEntry;
    {
        std::lock_guard<std::mutex> lock(socketMutex());
        auto found = openSockets().find(id);
        if (found != openSockets().end()) {
            socketEntry = found->second;
            if (fn == "socket_dong") openSockets().erase(found);
        } else {
            auto tlsFound = openTlsSockets().find(id);
            if (tlsFound == openTlsSockets().end()) {
                err = fn + ": socket đã đóng hoặc không tồn tại";
                return true;
            }
            tlsEntry = tlsFound->second;
            if (fn == "socket_dong") openTlsSockets().erase(tlsFound);
        }
    }

    if (tlsEntry != nullptr) {
        std::lock_guard<std::mutex> ioLock(tlsEntry->ioMutex);
        OpenTlsSocket &tls = *tlsEntry;
        if (fn == "socket_dong") {
            closeTlsSocket(tls);
            result = make_int_value(1);
            return true;
        }
        if (tls.handle == kInvalidNativeSocket) {
            err = fn + ": socket đã đóng hoặc không tồn tại";
            return true;
        }
        if (fn == "socket_dat_timeout") {
            int timeoutMs = 0;
            if (!requireIntArgFromStack(args[1], fn, "thời gian chờ", timeoutMs, err)) {
                return true;
            }
            if (timeoutMs <= 0 || timeoutMs > 300000) {
                err = fn + ": thời gian chờ phải trong 1..300000 ms";
                return true;
            }
            if (!setSocketTimeout(tls.handle, timeoutMs)) {
                err = fn + ": không đặt được thời gian chờ";
                return true;
            }
            result = make_int_value(1);
            return true;
        }
        if (fn == "socket_gui") {
            std::string payload;
            if (!requireString(args[1], fn, "dữ liệu", payload, err)) return true;
            int written = 0;
            if (!tlsWrite(tls, payload, written, err)) return true;
            result = make_int_value(written);
            return true;
        }

        int maximum = 0;
        if (!requireIntArgFromStack(args[1], fn, "số byte tối đa", maximum, err)) {
            return true;
        }
        if (maximum <= 0 || maximum > 16 * 1024 * 1024) {
            err = fn + ": số byte tối đa phải trong 1..16777216";
            return true;
        }
        std::string data;
        if (!tlsRead(tls, maximum, data, err)) return true;
        result = make_string_value(std::move(data));
        return true;
    }

    std::lock_guard<std::mutex> ioLock(socketEntry->ioMutex);
    OpenSocket &socketState = *socketEntry;
    if (fn == "socket_dong") {
        closeNativeSocket(socketState.handle);
        socketState.handle = kInvalidNativeSocket;
        result = make_int_value(1);
        return true;
    }
    if (socketState.handle == kInvalidNativeSocket) {
        err = fn + ": socket đã đóng hoặc không tồn tại";
        return true;
    }
    NativeSocket socket = socketState.handle;
    if (fn == "socket_dat_timeout") {
        int timeoutMs = 0;
        if (!requireIntArgFromStack(args[1], fn, "thời gian chờ", timeoutMs, err)) {
            return true;
        }
        if (timeoutMs <= 0 || timeoutMs > 300000) {
            err = fn + ": thời gian chờ phải trong 1..300000 ms";
            return true;
        }
        if (!setSocketTimeout(socket, timeoutMs)) {
            err = fn + ": không đặt được thời gian chờ";
            return true;
        }
        result = make_int_value(1);
        return true;
    }
    if (fn == "socket_gui") {
        std::string payload;
        if (!requireString(args[1], fn, "dữ liệu", payload, err)) return true;
#if defined(MSG_NOSIGNAL)
        constexpr int flags = MSG_NOSIGNAL;
#else
        constexpr int flags = 0;
#endif
        const std::size_t capped = std::min<std::size_t>(
            payload.size(), static_cast<std::size_t>(INT_MAX));
        const int sent = ::send(socket, payload.data(), static_cast<int>(capped), flags);
        if (sent < 0) {
            err = fn + ": gửi thất bại, mã " +
                  std::to_string(lastSocketError());
            return true;
        }
        result = make_int_value(sent);
        return true;
    }

    int maximum = 0;
    if (!requireIntArgFromStack(args[1], fn, "số byte tối đa", maximum, err)) {
        return true;
    }
    if (maximum <= 0 || maximum > 16 * 1024 * 1024) {
        err = fn + ": số byte tối đa phải trong 1..16777216";
        return true;
    }
    std::string buffer(static_cast<std::size_t>(maximum), '\0');
    const int received = ::recv(socket, buffer.data(), maximum, 0);
    if (received < 0) {
        err = fn + ": nhận thất bại, mã " + std::to_string(lastSocketError());
        return true;
    }
    buffer.resize(static_cast<std::size_t>(received));
    result = make_string_value(buffer);
    return true;
}

bool handleDns(const std::string &fn,
               const std::vector<StackValue> &args,
               StackValue &result,
               std::string &err) {
    if (fn != "dns_phan_giai") return false;
    if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
    std::string host;
    if (!requireString(args[0], fn, "tên máy", host, err) || host.empty()) {
        if (err.empty()) err = fn + ": tên máy không được rỗng";
        return true;
    }
    if (!initializeNetwork(err)) return true;

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *head = nullptr;
    const int status = getaddrinfo(host.c_str(), nullptr, &hints, &head);
    if (status != 0 || head == nullptr) {
#if defined(_WIN32)
        err = fn + ": không phân giải được tên máy, mã " + std::to_string(status);
#else
        err = fn + ": không phân giải được tên máy: " + std::string(gai_strerror(status));
#endif
        if (head != nullptr) freeaddrinfo(head);
        return true;
    }
    std::vector<StackValue> addresses;
    for (addrinfo *entry = head; entry != nullptr; entry = entry->ai_next) {
        char buffer[NI_MAXHOST]{};
        if (getnameinfo(entry->ai_addr, static_cast<socklen_t>(entry->ai_addrlen),
                        buffer, sizeof(buffer), nullptr, 0, NI_NUMERICHOST) == 0) {
            addresses.push_back(make_string_value(buffer));
        }
    }
    freeaddrinfo(head);
    result = make_list_value(std::move(addresses));
    return true;
}

bool socketResolveVm(const std::vector<StackValue> &args,
                     StackValue &result,
                     std::string &err) {
    const std::string fn = "socket_phan_giai_vm";
    if (!requireNativeArgumentCount(args, fn, 3, err)) return true;
    std::string host;
    int port = 0;
    int datagram = 0;
    if (!requireString(args[0], fn, "tên máy", host, err) ||
        !requireIntArgFromStack(args[1], fn, "cổng", port, err) ||
        !requireIntArgFromStack(args[2], fn, "datagram", datagram, err)) {
        return true;
    }
    if (host.empty() || port <= 0 || port > 65535 ||
        (datagram != 0 && datagram != 1)) {
        err = fn + ": đối số không hợp lệ";
        return true;
    }
    if (!initializeNetwork(err)) return true;

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = datagram ? SOCK_DGRAM : SOCK_STREAM;
    hints.ai_protocol = datagram ? IPPROTO_UDP : IPPROTO_TCP;
    addrinfo *head = nullptr;
    const std::string service = std::to_string(port);
    const int status = getaddrinfo(host.c_str(), service.c_str(), &hints, &head);
    if (status != 0 || head == nullptr) {
#if defined(_WIN32)
        err = fn + ": không phân giải được địa chỉ, mã " + std::to_string(status);
#else
        err = fn + ": không phân giải được địa chỉ: " + std::string(gai_strerror(status));
#endif
        if (head != nullptr) freeaddrinfo(head);
        return true;
    }

    std::vector<StackValue> resolved;
    for (addrinfo *entry = head; entry != nullptr; entry = entry->ai_next) {
        char numericHost[NI_MAXHOST]{};
        char numericService[NI_MAXSERV]{};
        if (getnameinfo(entry->ai_addr, static_cast<socklen_t>(entry->ai_addrlen),
                        numericHost, sizeof(numericHost),
                        numericService, sizeof(numericService),
                        NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
            continue;
        }
        int resolvedPort = port;
        try {
            resolvedPort = std::stoi(numericService);
        } catch (...) {}
        resolved.push_back(make_list_value({
            make_string_value(numericHost),
            make_int_value(resolvedPort),
        }));
    }
    freeaddrinfo(head);
    result = make_list_value(std::move(resolved));
    return true;
}

bool socketOpenResolvedVm(const std::string &fn,
                          const std::vector<StackValue> &args,
                          int socketType,
                          StackValue &result,
                          std::string &err) {
    if (!requireNativeArgumentCount(args, fn, 2, err)) return true;
    if (!std::holds_alternative<ListHandle>(args[0]) ||
        std::get<ListHandle>(args[0]) == nullptr) {
        err = fn + ": địa chỉ phân giải không hợp lệ";
        return true;
    }
    const ListHandle address = std::get<ListHandle>(args[0]);
    if (address->elements.size() != 2 ||
        !std::holds_alternative<std::string>(address->elements[0]) ||
        !std::holds_alternative<int>(address->elements[1])) {
        err = fn + ": địa chỉ phân giải không hợp lệ";
        return true;
    }
    std::vector<StackValue> legacyArgs{
        address->elements[0], address->elements[1], args[1],
    };
    return socketOpen(fn, legacyArgs, socketType, result, err);
}

bool socketTlsUpgradeVm(const std::vector<StackValue> &args,
                        StackValue &result,
                        std::string &err) {
    const std::string fn = "socket_tls_nang_cap_vm";
    if (!requireNativeArgumentCount(args, fn, 2, err)) return true;
    int id = 0;
    std::string host;
    if (!requireIntArgFromStack(args[0], fn, "mã socket", id, err) || id <= 0 ||
        !requireString(args[1], fn, "tên máy", host, err) || host.empty()) {
        if (err.empty()) err = fn + ": đối số không hợp lệ";
        return true;
    }

    std::shared_ptr<OpenSocket> raw;
    {
        std::lock_guard<std::mutex> lock(socketMutex());
        const auto found = openSockets().find(id);
        if (found == openSockets().end() || found->second->listener ||
            found->second->type != SOCK_STREAM) {
            err = fn + ": socket TCP đã đóng hoặc không tồn tại";
            return true;
        }
        raw = found->second;
    }

    std::lock_guard<std::mutex> ioLock(raw->ioMutex);
    if (raw->handle == kInvalidNativeSocket) {
        err = fn + ": socket TCP đã đóng hoặc không tồn tại";
        return true;
    }
    auto tls = std::make_shared<OpenTlsSocket>();
    tls->handle = raw->handle;
    if (!initializeTlsClient(*tls, host, err)) {
        closeTlsSocket(*tls);
        raw->handle = kInvalidNativeSocket;
        std::lock_guard<std::mutex> lock(socketMutex());
        openSockets().erase(id);
        return true;
    }

    raw->handle = kInvalidNativeSocket;
    {
        std::lock_guard<std::mutex> lock(socketMutex());
        openSockets().erase(id);
        openTlsSockets()[id] = std::move(tls);
    }
    result = make_int_value(id);
    return true;
}

bool socketSendVm(const std::vector<StackValue> &args,
                  StackValue &result,
                  std::string &err) {
    const std::string fn = "socket_gui_vm";
    if (!requireNativeArgumentCount(args, fn, 3, err)) return true;
    int offset = 0;
    if (!std::holds_alternative<std::string>(args[1]) ||
        !requireIntArgFromStack(args[2], fn, "độ lệch byte", offset, err)) {
        if (err.empty()) err = fn + ": dữ liệu phải là chuỗi";
        return true;
    }
    const std::string &payload = std::get<std::string>(args[1]);
    const int total = static_cast<int>(payload.size());
    if (offset < 0 || offset > total) {
        err = fn + ": độ lệch byte ngoài phạm vi";
        return true;
    }
    if (offset == total) {
        result = make_list_value({make_int_value(0), make_int_value(total), make_int_value(1)});
        return true;
    }
    StackValue sentResult = make_int_value(0);
    std::vector<StackValue> legacyArgs{
        args[0], make_string_value(payload.substr(static_cast<std::size_t>(offset))),
    };
    if (!handleSockets("socket_gui", legacyArgs, sentResult, err)) return false;
    if (!err.empty()) return true;
    if (!std::holds_alternative<int>(sentResult)) {
        err = fn + ": kết quả gửi nội bộ không hợp lệ";
        return true;
    }
    const int sent = std::get<int>(sentResult);
    result = make_list_value({
        make_int_value(sent),
        make_int_value(total),
        make_int_value(offset + sent >= total ? 1 : 0),
    });
    return true;
}

} // namespace

bool handleNativeM3LibraryFunction(Opcode opcode,
                                   const std::vector<StackValue> &args,
                                   StackValue &result,
                                   std::string &err) {
    const auto *primitive = vietvm::bytecode::intrinsicByOpcode(opcode);
    if (primitive == nullptr) return false;
    const std::string fn(primitive->name);

    if (opcode == OP_VM_SOCKET_PHAN_GIAI) return socketResolveVm(args, result, err);
    if (opcode == OP_VM_SOCKET_TCP_MO) {
        return socketOpenResolvedVm(fn, args, SOCK_STREAM, result, err);
    }
    if (opcode == OP_VM_SOCKET_UDP_MO) {
        return socketOpenResolvedVm(fn, args, SOCK_DGRAM, result, err);
    }
    if (opcode == OP_VM_SOCKET_TLS_NANG_CAP) return socketTlsUpgradeVm(args, result, err);
    if (opcode == OP_VM_SOCKET_GUI) return socketSendVm(args, result, err);
    if (opcode == OP_VM_SOCKET_TCP_LANG_NGHE) {
        return socketListen("socket_tcp_lang_nghe", args, result, err);
    }
    if (opcode == OP_VM_SOCKET_CHAP_NHAN) {
        return socketAccept("socket_chap_nhan", args, result, err);
    }
    if (opcode == OP_VM_SOCKET_DAT_TIMEOUT) {
        return handleSockets("socket_dat_timeout", args, result, err);
    }
    if (opcode == OP_VM_SOCKET_NHAN) {
        return handleSockets("socket_nhan", args, result, err);
    }
    if (opcode == OP_VM_SOCKET_DONG) {
        return handleSockets("socket_dong", args, result, err);
    }
    if (opcode == OP_VM_DNS_PHAN_GIAI) {
        return handleDns("dns_phan_giai", args, result, err);
    }
    return false;
}

} // namespace vietvm::helpers
