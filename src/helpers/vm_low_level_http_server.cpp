#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>

#include "common/vm_low_level_http_server.h"

#include "common/vm_native_helpers.h"
#include "common/vm_native_http_helpers.h"

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace vietvm::helpers {

namespace {

bool recvHttpRequest(int fd,
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
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
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
        std::string k = toLowerAscii(trimCopy(line.substr(0, colon)));
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
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        body.append(buf, (size_t)n);
    }
    if (body.size() > contentLength) body.resize(contentLength);

    return true;
}

const char *httpStatusText(int code) {
    switch (code) {
        case 200: return "OK";
        case 201: return "Created";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 404: return "Not Found";
        case 500: return "Internal Server Error";
        default: return "OK";
    }
}

bool sendHttpJsonResponse(int fd, int status, const std::string &body) {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status << " " << httpStatusText(status) << "\r\n"
        << "Content-Type: application/json; charset=utf-8\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n\r\n"
        << body;

    std::string out = oss.str();
    size_t sent = 0;
    while (sent < out.size()) {
        ssize_t n = send(fd, out.data() + sent, out.size() - sent, 0);
        if (n <= 0) return false;
        sent += (size_t)n;
    }
    return true;
}

struct LowLevelHttpRequest {
    int serverId = 0;
    int clientFd = -1;
    std::string requestId;
    std::string method;
    std::string path;
    std::string query;
    std::string body;
    std::unordered_map<std::string, std::string> headers;
};

struct LowLevelHttpServer {
    int id = 0;
    int listenFd = -1;
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

std::string makeLowHttpReqId() {
    std::lock_guard<std::mutex> lk(gLowHttpMu);
    int seed = gLowHttpNextReqSeed++;
    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return "req-" + std::to_string(nowMs) + "-" + std::to_string(seed);
}

std::shared_ptr<LowLevelHttpServer> getLowHttpServer(int serverId) {
    std::lock_guard<std::mutex> lk(gLowHttpMu);
    auto it = gLowHttpServers.find(serverId);
    if (it == gLowHttpServers.end()) return nullptr;
    return it->second;
}

bool getLowHttpRequestCopy(const std::string &reqId, LowLevelHttpRequest &out) {
    std::lock_guard<std::mutex> lk(gLowHttpMu);
    auto it = gLowHttpRequests.find(reqId);
    if (it == gLowHttpRequests.end()) return false;
    out = it->second;
    return true;
}

} // namespace

bool runLowLevelHttpServerOpen(int port, StackValue &result, std::string &err) {
#if defined(_WIN32)
    (void)port;
    (void)result;
    err = "mang_http_server_open: Windows chưa hỗ trợ low-level listen socket trong bản này";
    return true;
#else
    if (port <= 0 || port > 65535) {
        err = "mang_http_server_open: cổng không hợp lệ";
        return true;
    }

    int listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd < 0) {
        err = "mang_http_server_open: không tạo được socket";
        return true;
    }

    int reuse = 1;
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(listenFd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        close(listenFd);
        err = "mang_http_server_open: bind thất bại";
        return true;
    }

    if (listen(listenFd, 64) < 0) {
        close(listenFd);
        err = "mang_http_server_open: listen thất bại";
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
            socklen_t clientLen = sizeof(clientAddr);
            int clientFd = accept(server->listenFd, reinterpret_cast<sockaddr *>(&clientAddr), &clientLen);
            if (clientFd < 0) {
                if (!server->running.load()) break;
                continue;
            }

            std::string method, target, body;
            std::unordered_map<std::string, std::string> headers;
            if (!recvHttpRequest(clientFd, method, target, headers, body)) {
                close(clientFd);
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

    std::cout << "[HTTP] low-level server listening on 0.0.0.0:" << port << std::endl;
    result = make_int_value(server->id);
    return true;
#endif
}

bool runLowLevelHttpServerNext(int serverId, StackValue &result, std::string &err) {
    auto server = getLowHttpServer(serverId);
    if (!server) {
        err = "mang_http_server_next: server không tồn tại";
        return true;
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

bool runLowLevelHttpReqField(const std::string &reqId,
                             const std::string &field,
                             const std::optional<std::string> &key,
                             StackValue &result,
                             std::string &err) {
    LowLevelHttpRequest req;
    if (!getLowHttpRequestCopy(reqId, req)) {
        err = "mang_http_req_field: request không tồn tại";
        return true;
    }

    if (field == "method") result = make_string_value(req.method);
    else if (field == "path") result = make_string_value(req.path);
    else if (field == "query") result = make_string_value(req.query);
    else if (field == "body") result = make_string_value(req.body);
    else if (field == "header") {
        std::string k = key.has_value() ? toLowerAscii(*key) : "";
        auto it = req.headers.find(k);
        result = make_string_value(it == req.headers.end() ? "" : it->second);
    } else if (field == "query_param") {
        std::string k = key.has_value() ? *key : "";
        result = make_string_value(queryParam(req.query, k));
    } else if (field == "json_field") {
        std::string k = key.has_value() ? *key : "";
        result = make_string_value(extractSimpleJsonStringField(req.body, k));
    } else if (field == "path_suffix") {
        std::string prefix = key.has_value() ? *key : "";
        if (!startsWith(req.path, prefix)) result = make_string_value("");
        else result = make_string_value(req.path.substr(prefix.size()));
    } else {
        err = "mang_http_req_field: field không hợp lệ";
    }

    return true;
}

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
            err = "mang_http_server_send: request không tồn tại";
            return true;
        }
        req = it->second;
        gLowHttpRequests.erase(it);
    }

    if (!sendHttpJsonResponse(req.clientFd, status, body)) {
        close(req.clientFd);
        err = "mang_http_server_send: gửi phản hồi thất bại";
        return true;
    }
    close(req.clientFd);
    result = make_int_value(1);
    return true;
}

bool runLowLevelHttpServerClose(int serverId, StackValue &result, std::string &err) {
    (void)err;
    auto server = getLowHttpServer(serverId);
    if (!server) {
        result = make_int_value(0);
        return true;
    }

    server->running = false;
#if !defined(_WIN32)
    shutdown(server->listenFd, SHUT_RDWR);
    close(server->listenFd);
#endif
    server->cv.notify_all();
    if (server->acceptThread.joinable()) server->acceptThread.join();

    {
        std::lock_guard<std::mutex> lk(gLowHttpMu);
        gLowHttpServers.erase(serverId);
        for (auto it = gLowHttpRequests.begin(); it != gLowHttpRequests.end();) {
            if (it->second.serverId == serverId) {
                if (it->second.clientFd >= 0) close(it->second.clientFd);
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
