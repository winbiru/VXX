#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>

#include "common/vm_low_level_http_server.h"

#include "common/vm_native_constants.h"
#include "common/vm_native_helpers.h"
#include "common/vm_native_http_helpers.h"
#include "vpp/core/message_constants.h"
#include "vpp/core/text.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace vietvm::helpers {

namespace {

#if defined(_WIN32)
using SocketHandle = SOCKET;
using SocketLength = int;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;

// Khởi tạo `sockets`; hàm chuẩn bị trạng thái/phụ thuộc theo thứ tự cần thiết trước khi cho phép phần còn lại sử dụng.
bool initializeSockets(std::string &err) {
    static std::once_flag initFlag;
    static int initResult = 0;
    std::call_once(initFlag, []() {
        WSADATA data{};
        initResult = WSAStartup(MAKEWORD(2, 2), &data);
    });
    if (initResult != 0) {
        err = vietvm::messages::formatMessage(
            vietvm::messages::kNativeHttpServerWsaStartupFailed);
        return false;
    }
    return true;
}

// Đóng socket hệ điều hành sau khi request/server kết thúc; wrapper chọn `closesocket` hoặc `close` theo nền tảng.
void closeSocket(SocketHandle socket) {
    if (socket != kInvalidSocket) closesocket(socket);
}

// Ngắt hai chiều truyền nhận trước khi đóng socket; wrapper gọi API shutdown phù hợp để peer nhận EOF sạch.
void shutdownSocket(SocketHandle socket) {
    if (socket != kInvalidSocket) shutdown(socket, SD_BOTH);
}
#else
using SocketHandle = int;
using SocketLength = socklen_t;
constexpr SocketHandle kInvalidSocket = -1;

// Khởi tạo `sockets`; hàm chuẩn bị trạng thái/phụ thuộc theo thứ tự cần thiết trước khi cho phép phần còn lại sử dụng.
bool initializeSockets(std::string &) { return true; }

// Đóng socket hệ điều hành sau khi request/server kết thúc; wrapper chọn `closesocket` hoặc `close` theo nền tảng.
void closeSocket(SocketHandle socket) {
    if (socket != kInvalidSocket) close(socket);
}

// Ngắt hai chiều truyền nhận trước khi đóng socket; wrapper gọi API shutdown phù hợp để peer nhận EOF sạch.
void shutdownSocket(SocketHandle socket) {
    if (socket != kInvalidSocket) shutdown(socket, SHUT_RDWR);
}
#endif

// Đọc HTTP request thô từ socket; hàm nhận dữ liệu cho tới khi đủ header/body theo `Content-Length` hoặc kết nối kết thúc.
bool recvHttpRequest(SocketHandle fd,
                     std::string &method,
                     std::string &target,
                     std::unordered_map<std::string, std::string> &headers,
                     std::string &body) {
    headers.clear();
    body.clear();
    method.clear();
    target.clear();

    std::string raw;
    raw.reserve(8192);
    char buf[4096];

    while (raw.find("\r\n\r\n") == std::string::npos) {
        int n = recv(fd, buf, static_cast<int>(sizeof(buf)), 0);
        if (n <= 0) return false;
        raw.append(buf, (size_t)n);
        if (raw.size() > 1024 * 1024) return false;
    }

    size_t headerEnd = raw.find("\r\n\r\n");
    std::string head = raw.substr(0, headerEnd);
    size_t bodyStart = headerEnd + 4;

    std::istringstream hs(head);
    std::string line;
    if (!std::getline(hs, line)) return false;
    if (!line.empty() && line.back() == '\r') line.pop_back();

    {
        std::istringstream rl(line);
        std::string version;
        if (!(rl >> method >> target >> version)) return false;
    }

    while (std::getline(hs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string k = vietvm::core::toLowerAscii(trimCopy(line.substr(0, colon)));
        std::string v = trimCopy(line.substr(colon + 1));
        headers[k] = v;
    }

    size_t contentLength = 0;
    auto it = headers.find("content-length");
    if (it != headers.end()) {
        try { contentLength = (size_t)std::stoul(it->second); }
        catch (...) { contentLength = 0; }
    }

    body = raw.substr(bodyStart);
    while (body.size() < contentLength) {
        int n = recv(fd, buf, static_cast<int>(sizeof(buf)), 0);
        if (n <= 0) break;
        body.append(buf, (size_t)n);
    }
    if (body.size() > contentLength) body.resize(contentLength);

    return true;
}

// Ánh xạ mã trạng thái HTTP sang reason phrase như `OK` hoặc `Not Found`; response builder dùng chuỗi này để tạo status line.
const char *httpStatusText(int code) {
    switch (code) {
        case vietvm::constants::kHttpStatusOk: return "OK";
        case 201: return "Created";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 404: return "Not Found";
        case 500: return "Internal Server Error";
        default: return "OK";
    }
}

// Gửi HTTP response JSON qua socket; hàm dựng status line/header `Content-Length` rồi truyền toàn bộ header và body.
bool sendHttpJsonResponse(SocketHandle fd, int status, const std::string &body) {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status << " " << httpStatusText(status) << "\r\n"
        << "Content-Type: application/json; charset=utf-8\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n\r\n"
        << body;

    std::string out = oss.str();
    size_t sent = 0;
    while (sent < out.size()) {
        int n = send(fd, out.data() + sent, static_cast<int>(out.size() - sent), 0);
        if (n <= 0) return false;
        sent += (size_t)n;
    }
    return true;
}

// Gom dữ liệu một HTTP request đã nhận như method, path, header/body và metadata transport để native API truy cập theo request id.
struct LowLevelHttpRequest {
    int serverId = 0;
    SocketHandle clientFd = kInvalidSocket;
    std::string fileResponsePath;
    std::string requestId;
    std::string method;
    std::string path;
    std::string query;
    std::string body;
    std::unordered_map<std::string, std::string> headers;
};

// Giữ trạng thái server HTTP mức thấp gồm socket/file transport, cổng và hàng đợi request đang chờ xử lý.
struct LowLevelHttpServer {
    int id = 0;
    SocketHandle listenFd = kInvalidSocket;
    bool fileTransport = false;
    int port = 0;
    std::string transportDir;
    std::atomic<bool> running{false};
    std::thread acceptThread;
    std::mutex mtx;
    std::condition_variable cv;
    std::deque<std::string> queue;
};

std::mutex gLowHttpMu;
int gLowHttpNextServerId = 1;
int gLowHttpNextReqSeed = 1;
std::unordered_map<int, std::shared_ptr<LowLevelHttpServer>> gLowHttpServers;
std::unordered_map<std::string, LowLevelHttpRequest> gLowHttpRequests;

// Tạo low HTTP req mã định danh; hàm dựng giá trị mới từ đầu vào theo định dạng runtime và trả kết quả cho caller.
std::string makeLowHttpReqId() {
    std::lock_guard<std::mutex> lk(gLowHttpMu);
    int seed = gLowHttpNextReqSeed++;
    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return "req-" + std::to_string(nowMs) + "-" + std::to_string(seed);
}

// Lấy low HTTP máy chủ; hàm đọc dữ liệu từ trạng thái hiện tại và trả về cho caller mà không chủ động thay đổi dữ liệu.
std::shared_ptr<LowLevelHttpServer> getLowHttpServer(int serverId) {
    std::lock_guard<std::mutex> lk(gLowHttpMu);
    auto it = gLowHttpServers.find(serverId);
    if (it == gLowHttpServers.end()) return nullptr;
    return it->second;
}

// Lấy low HTTP yêu cầu copy; hàm đọc dữ liệu từ trạng thái hiện tại và trả về cho caller mà không chủ động thay đổi dữ liệu.
bool getLowHttpRequestCopy(const std::string &reqId, LowLevelHttpRequest &out) {
    std::lock_guard<std::mutex> lk(gLowHttpMu);
    auto it = gLowHttpRequests.find(reqId);
    if (it == gLowHttpRequests.end()) return false;
    out = it->second;
    return true;
}

// Trả thư mục gốc cho HTTP file-transport fallback; hàm lấy cấu hình/env và tạo path ổn định cho request/response trao đổi qua file.
std::optional<std::filesystem::path> httpFileTransportRoot() {
    const char *value = std::getenv("VPP_HTTP_FILE_TRANSPORT_DIR");
    if (value == nullptr || *value == '\0') return std::nullopt;
    return std::filesystem::path(value);
}

// Tạo thư mục con của file transport theo cổng server; cách tách theo port ngăn request của nhiều server trộn lẫn.
std::filesystem::path httpFilePortDir(const std::filesystem::path &root, int port) {
    return root / ("port-" + std::to_string(port));
}

// Tạo tệp transport token; hàm dựng giá trị mới từ đầu vào theo định dạng runtime và trả kết quả cho caller.
std::string makeFileTransportToken() {
    static std::atomic<unsigned long long> counter{1};
    const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return std::to_string(now) + "-" + std::to_string(counter.fetch_add(1));
}

// Ghi atomic tệp; hàm tuần tự hóa hoặc chuyển dữ liệu đầu vào sang đích ghi tương ứng.
bool writeAtomicFile(const std::filesystem::path &path, const std::string &data) {
    const std::filesystem::path temporary = path.string() + ".tmp-" + makeFileTransportToken();
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!out.good()) {
            out.close();
            std::error_code removeError;
            std::filesystem::remove(temporary, removeError);
            return false;
        }
    }
    std::error_code renameError;
    std::filesystem::rename(temporary, path, renameError);
    if (renameError) {
        std::error_code removeError;
        std::filesystem::remove(temporary, removeError);
        return false;
    }
    return true;
}

// Đọc toàn bộ tệp; hàm lấy nội dung từ nguồn tương ứng, kiểm tra lỗi cần thiết rồi trả dữ liệu đã đọc.
bool readWholeFile(const std::filesystem::path &path, std::string &data) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    data = buffer.str();
    return in.good() || in.eof();
}

// Phân tích loopback HTTP url; hàm duyệt đầu vào theo ngữ pháp/định dạng quy định, tạo cấu trúc kết quả và báo lỗi khi dữ liệu không hợp lệ.
bool parseLoopbackHttpUrl(const std::string &url, int &port, std::string &target) {
    constexpr std::string_view prefix = "http://";
    if (url.compare(0, prefix.size(), prefix) != 0) return false;
    const std::size_t authorityStart = prefix.size();
    const std::size_t slash = url.find('/', authorityStart);
    const std::string authority = url.substr(
        authorityStart, slash == std::string::npos ? std::string::npos : slash - authorityStart);
    target = slash == std::string::npos ? "/" : url.substr(slash);

    std::string host = authority;
    port = 80;
    const std::size_t colon = authority.rfind(':');
    if (colon != std::string::npos) {
        host = authority.substr(0, colon);
        try {
            port = std::stoi(authority.substr(colon + 1));
        } catch (...) {
            return false;
        }
    }
    return host == "127.0.0.1" || host == "localhost";
}

// Giải mã tệp transport yêu cầu; hàm đọc biểu diễn đã mã hóa, kiểm tra định dạng và dựng lại giá trị runtime tương ứng.
bool decodeFileTransportRequest(const std::filesystem::path &path,
                                std::string &method,
                                std::string &target,
                                std::string &body) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;
    std::string bodySizeText;
    if (!std::getline(in, method) || !std::getline(in, target) || !std::getline(in, bodySizeText)) {
        return false;
    }
    std::size_t bodySize = 0;
    try {
        bodySize = static_cast<std::size_t>(std::stoull(bodySizeText));
    } catch (...) {
        return false;
    }
    body.assign(bodySize, '\0');
    if (bodySize != 0) {
        in.read(body.data(), static_cast<std::streamsize>(bodySize));
        if (static_cast<std::size_t>(in.gcount()) != bodySize) return false;
    }
    return true;
}

// Đưa request file-transport kế tiếp vào hàng đợi server; hàm quét file mới, decode nội dung và tạo record request runtime.
bool enqueueNextFileTransportRequest(const std::shared_ptr<LowLevelHttpServer> &server,
                                     StackValue &result) {
    const std::filesystem::path requestsDir =
        std::filesystem::path(server->transportDir) / "requests";
    while (server->running.load()) {
        std::filesystem::path requestPath;
        std::error_code iterateError;
        for (const auto &entry : std::filesystem::directory_iterator(requestsDir, iterateError)) {
            if (iterateError) break;
            if (!entry.is_regular_file() || entry.path().extension() != ".req") continue;
            if (requestPath.empty() || entry.path().filename() < requestPath.filename()) {
                requestPath = entry.path();
            }
        }
        if (!requestPath.empty()) {
            std::string method, target, body;
            if (!decodeFileTransportRequest(requestPath, method, target, body)) {
                std::error_code removeError;
                std::filesystem::remove(requestPath, removeError);
                continue;
            }
            std::string path, query;
            splitPathAndQuery(target, path, query);
            const std::string requestId = requestPath.stem().string();

            LowLevelHttpRequest req;
            req.serverId = server->id;
            req.requestId = requestId;
            req.method = method;
            req.path = path;
            req.query = query;
            req.body = body;
            if (!body.empty()) req.headers["content-type"] = "application/json";
            req.fileResponsePath =
                (std::filesystem::path(server->transportDir) / "responses" /
                 (requestId + ".resp")).string();
            {
                std::lock_guard<std::mutex> lk(gLowHttpMu);
                gLowHttpRequests[requestId] = std::move(req);
            }
            std::error_code removeError;
            std::filesystem::remove(requestPath, removeError);
            result = make_string_value(requestId);
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    result = make_string_value("");
    return true;
}

// Khởi tạo server HTTP giả lập bằng filesystem; hàm tạo thư mục/metadata cổng để client và server trao đổi request mà không cần socket.
bool openFileTransportServer(const std::filesystem::path &root,
                             int port,
                             StackValue &result,
                             std::string &err) {
    std::error_code fsError;
    const std::filesystem::path portDir = httpFilePortDir(root, port);
    std::filesystem::create_directories(portDir / "requests", fsError);
    if (!fsError) std::filesystem::create_directories(portDir / "responses", fsError);
    if (fsError) {
        err = vietvm::messages::formatMessage(
            vietvm::messages::kNativeHttpServerBindFailed, {std::to_string(fsError.value())});
        return true;
    }

    auto server = std::make_shared<LowLevelHttpServer>();
    {
        std::lock_guard<std::mutex> lk(gLowHttpMu);
        server->id = gLowHttpNextServerId++;
        server->fileTransport = true;
        server->port = port;
        server->transportDir = portDir.string();
        server->running = true;
        gLowHttpServers[server->id] = server;
    }
    if (!writeAtomicFile(portDir / "ready", std::to_string(server->id))) {
        std::lock_guard<std::mutex> lk(gLowHttpMu);
        gLowHttpServers.erase(server->id);
        err = vietvm::messages::formatMessage(
            vietvm::messages::kNativeHttpServerBindFailed, {"file"});
        return true;
    }
    result = make_int_value(server->id);
    return true;
}

} // namespace

// Thử xử lý HTTP loopback qua cơ chế file transport; hàm đọc request đã mã hóa từ thư mục trao đổi và trả `false` nếu không có request phù hợp.
bool tryLowLevelHttpFileTransportRequest(const std::string &method,
                                         const std::string &url,
                                         const std::optional<std::string> &payload,
                                         StackValue &result,
                                         std::string &err,
                                         bool &handled) {
    handled = false;
    const auto root = httpFileTransportRoot();
    if (!root.has_value()) return false;

    int port = 0;
    std::string target;
    if (!parseLoopbackHttpUrl(url, port, target)) return false;
    handled = true;

    const std::filesystem::path portDir = httpFilePortDir(*root, port);
    const std::filesystem::path ready = portDir / "ready";
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!std::filesystem::exists(ready)) {
        if (std::chrono::steady_clock::now() >= deadline) {
            err = vietvm::messages::formatMessage(
                vietvm::messages::kNativeHttpCurlFailed, {"http-file-transport"});
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    const std::string requestId = "req-file-" + makeFileTransportToken();
    const std::filesystem::path requestPath = portDir / "requests" / (requestId + ".req");
    const std::filesystem::path responsePath = portDir / "responses" / (requestId + ".resp");
    const std::string body = payload.value_or("");
    const std::string requestData = method + "\n" + target + "\n" +
                                    std::to_string(body.size()) + "\n" + body;
    if (!writeAtomicFile(requestPath, requestData)) {
        err = vietvm::messages::formatMessage(
            vietvm::messages::kNativeHttpCurlFailed, {"http-file-transport"});
        return true;
    }

    while (!std::filesystem::exists(responsePath)) {
        if (std::chrono::steady_clock::now() >= deadline) {
            std::error_code removeError;
            std::filesystem::remove(requestPath, removeError);
            err = vietvm::messages::formatMessage(
                vietvm::messages::kNativeHttpCurlFailed, {"http-file-transport"});
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    std::string responseData;
    if (!readWholeFile(responsePath, responseData)) {
        err = vietvm::messages::formatMessage(
            vietvm::messages::kNativeHttpCurlFailed, {"http-file-transport"});
        return true;
    }
    std::error_code removeError;
    std::filesystem::remove(responsePath, removeError);
    const std::size_t newline = responseData.find('\n');
    if (newline == std::string::npos) {
        err = vietvm::messages::formatMessage(
            vietvm::messages::kNativeHttpCurlFailed, {"http-file-transport"});
        return true;
    }
    result = make_string_value(responseData.substr(newline + 1));
    return true;
}

// Chạy low level HTTP máy chủ open; hàm điều phối toàn bộ luồng xử lý của tác vụ, gọi các bước con theo thứ tự và trả mã/kết quả cuối cùng.
bool runLowLevelHttpServerOpen(int port,
                               StackValue &result,
                               std::string &err,
                               const LowLevelHttpLogSink &logSink) {
    if (port <= 0 || port > 65535) {
        err = vietvm::messages::formatMessage(
            vietvm::messages::kNativeHttpServerInvalidPort);
        return true;
    }

    if (const auto fileRoot = httpFileTransportRoot(); fileRoot.has_value()) {
        return openFileTransportServer(*fileRoot, port, result, err);
    }

    if (!initializeSockets(err)) return true;

    SocketHandle listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd == kInvalidSocket) {
        err = vietvm::messages::formatMessage(
            vietvm::messages::kNativeHttpServerSocketCreateFailed);
        return true;
    }

#if defined(_WIN32)
    // SO_REUSEADDR has different semantics on Winsock: another process can
    // bind the same address/port and receive connections unpredictably.  The
    // low-level server owns its port, so make that ownership explicit.
    BOOL exclusive = TRUE;
    if (setsockopt(listenFd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                   reinterpret_cast<const char *>(&exclusive), sizeof(exclusive)) == SOCKET_ERROR) {
        const int socketError = WSAGetLastError();
        closeSocket(listenFd);
        err = vietvm::messages::formatMessage(
            vietvm::messages::kNativeHttpServerExclusivePortFailed,
            {std::to_string(socketError)});
        return true;
    }
#else
    int reuse = 1;
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char *>(&reuse), sizeof(reuse));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(listenFd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
#if defined(_WIN32)
        const int bindError = WSAGetLastError();
#else
        const int bindError = errno;
#endif
        closeSocket(listenFd);
        err = vietvm::messages::formatMessage(
            vietvm::messages::kNativeHttpServerBindFailed,
            {std::to_string(bindError)});
        return true;
    }

    if (listen(listenFd, 64) < 0) {
        closeSocket(listenFd);
        err = vietvm::messages::formatMessage(
            vietvm::messages::kNativeHttpServerListenFailed);
        return true;
    }

    auto server = std::make_shared<LowLevelHttpServer>();
    {
        std::lock_guard<std::mutex> lk(gLowHttpMu);
        server->id = gLowHttpNextServerId++;
    }
    server->listenFd = listenFd;
    server->running = true;

    server->acceptThread = std::thread([server]() {
        while (server->running.load()) {
            sockaddr_in clientAddr{};
            SocketLength clientLen = sizeof(clientAddr);
            SocketHandle clientFd = accept(server->listenFd, reinterpret_cast<sockaddr *>(&clientAddr), &clientLen);
            if (clientFd == kInvalidSocket) {
                if (!server->running.load()) break;
                continue;
            }

            std::string method, target, body;
            std::unordered_map<std::string, std::string> headers;
            if (!recvHttpRequest(clientFd, method, target, headers, body)) {
                closeSocket(clientFd);
                continue;
            }

            std::string path, query;
            splitPathAndQuery(target, path, query);

            std::string reqId = makeLowHttpReqId();

            LowLevelHttpRequest req;
            req.serverId = server->id;
            req.clientFd = clientFd;
            req.requestId = reqId;
            req.method = method;
            req.path = path;
            req.query = query;
            req.body = body;
            req.headers = headers;

            {
                std::lock_guard<std::mutex> g(gLowHttpMu);
                gLowHttpRequests[reqId] = std::move(req);
            }
            {
                std::lock_guard<std::mutex> lk(server->mtx);
                server->queue.push_back(reqId);
            }
            server->cv.notify_one();
        }
    });

    {
        std::lock_guard<std::mutex> lk(gLowHttpMu);
        gLowHttpServers[server->id] = server;
    }

    if (logSink) {
        logSink(vietvm::messages::messageText(
            vietvm::messages::kNativeHttpServerListening, {std::to_string(port)}) + "\n");
    }
    result = make_int_value(server->id);
    return true;
}

// Chạy low level HTTP máy chủ next; hàm điều phối toàn bộ luồng xử lý của tác vụ, gọi các bước con theo thứ tự và trả mã/kết quả cuối cùng.
bool runLowLevelHttpServerNext(int serverId, StackValue &result, std::string &err) {
    auto server = getLowHttpServer(serverId);
    if (!server) {
        err = vietvm::messages::formatMessage(
            vietvm::messages::kNativeHttpServerNotFound);
        return true;
    }

    if (server->fileTransport) {
        return enqueueNextFileTransportRequest(server, result);
    }

    std::unique_lock<std::mutex> lk(server->mtx);
    server->cv.wait(lk, [&]() {
        return !server->running.load() || !server->queue.empty();
    });

    if (!server->running.load() && server->queue.empty()) {
        result = make_string_value("");
        return true;
    }

    std::string reqId = server->queue.front();
    server->queue.pop_front();
    result = make_string_value(reqId);
    return true;
}

// Chạy low level HTTP req trường; hàm điều phối toàn bộ luồng xử lý của tác vụ, gọi các bước con theo thứ tự và trả mã/kết quả cuối cùng.
bool runLowLevelHttpReqField(const std::string &reqId,
                             const std::string &field,
                             const std::optional<std::string> &key,
                             StackValue &result,
                             std::string &err) {
    LowLevelHttpRequest req;
    if (!getLowHttpRequestCopy(reqId, req)) {
        err = vietvm::messages::formatMessage(
            vietvm::messages::kNativeHttpRequestNotFound, {"mang_http_req_field"});
        return true;
    }

    if (field == vietvm::constants::kReqFieldMethod) result = make_string_value(req.method);
    else if (field == vietvm::constants::kReqFieldPath) result = make_string_value(req.path);
    else if (field == vietvm::constants::kReqFieldQuery) result = make_string_value(req.query);
    else if (field == vietvm::constants::kReqFieldBody) result = make_string_value(req.body);
    else if (field == vietvm::constants::kReqFieldHeader) {
        std::string k = key.has_value() ? vietvm::core::toLowerAscii(*key) : "";
        auto it = req.headers.find(k);
        result = make_string_value(it == req.headers.end() ? "" : it->second);
    } else if (field == vietvm::constants::kReqFieldQueryParam) {
        std::string k = key.has_value() ? *key : "";
        result = make_string_value(queryParam(req.query, k));
    } else if (field == vietvm::constants::kReqFieldJsonField) {
        std::string k = key.has_value() ? *key : "";
        result = make_string_value(extractSimpleJsonStringField(req.body, k));
    } else if (field == vietvm::constants::kReqFieldPathSuffix) {
        std::string prefix = key.has_value() ? *key : "";
        if (!startsWith(req.path, prefix)) result = make_string_value("");
        else result = make_string_value(req.path.substr(prefix.size()));
    } else {
        err = vietvm::messages::formatMessage(
            vietvm::messages::kNativeHttpRequestFieldInvalid);
    }

    return true;
}

// Chạy low level HTTP máy chủ send; hàm điều phối toàn bộ luồng xử lý của tác vụ, gọi các bước con theo thứ tự và trả mã/kết quả cuối cùng.
bool runLowLevelHttpServerSend(const std::string &reqId,
                               int status,
                               const std::string &body,
                               StackValue &result,
                               std::string &err) {
    LowLevelHttpRequest req;
    {
        std::lock_guard<std::mutex> lk(gLowHttpMu);
        auto it = gLowHttpRequests.find(reqId);
        if (it == gLowHttpRequests.end()) {
            err = vietvm::messages::formatMessage(
                vietvm::messages::kNativeHttpRequestNotFound, {"mang_http_server_send"});
            return true;
        }
        req = it->second;
        gLowHttpRequests.erase(it);
    }

    if (!req.fileResponsePath.empty()) {
        const std::string response = std::to_string(status) + "\n" + body;
        if (!writeAtomicFile(req.fileResponsePath, response)) {
            err = vietvm::messages::formatMessage(
                vietvm::messages::kNativeHttpResponseSendFailed);
            return true;
        }
        result = make_int_value(1);
        return true;
    }

    if (!sendHttpJsonResponse(req.clientFd, status, body)) {
        closeSocket(req.clientFd);
        err = vietvm::messages::formatMessage(
            vietvm::messages::kNativeHttpResponseSendFailed);
        return true;
    }
    closeSocket(req.clientFd);
    result = make_int_value(1);
    return true;
}

// Chạy low level HTTP máy chủ close; hàm điều phối toàn bộ luồng xử lý của tác vụ, gọi các bước con theo thứ tự và trả mã/kết quả cuối cùng.
bool runLowLevelHttpServerClose(int serverId, StackValue &result, std::string &err) {
    (void)err;
    auto server = getLowHttpServer(serverId);
    if (!server) {
        result = make_int_value(0);
        return true;
    }

    server->running = false;
    if (server->fileTransport) {
        std::error_code removeError;
        std::filesystem::remove(std::filesystem::path(server->transportDir) / "ready", removeError);
    } else {
        shutdownSocket(server->listenFd);
        closeSocket(server->listenFd);
        server->cv.notify_all();
        if (server->acceptThread.joinable()) server->acceptThread.join();
    }

    {
        std::lock_guard<std::mutex> lk(gLowHttpMu);
        gLowHttpServers.erase(serverId);
        for (auto it = gLowHttpRequests.begin(); it != gLowHttpRequests.end();) {
            if (it->second.serverId == serverId) {
                closeSocket(it->second.clientFd);
                it = gLowHttpRequests.erase(it);
            } else {
                ++it;
            }
        }
    }

    result = make_int_value(1);
    return true;
}

} // namespace vietvm::helpers
