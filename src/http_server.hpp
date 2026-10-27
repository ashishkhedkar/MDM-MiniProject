// Lightweight, dependency-free HTTP/1.1 server (POSIX sockets, one thread per connection).
// Binds to 127.0.0.1 only. Supports GET and POST with Content-Length bodies.
#pragma once
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <thread>

namespace http {

constexpr size_t kMaxBody = 8 * 1024 * 1024;  // 8 MB upload limit

struct Request {
    std::string method, path, query, body;
    std::map<std::string, std::string> headers;  // lower-case names
    std::string header(const std::string& k) const {
        auto it = headers.find(k);
        return it == headers.end() ? "" : it->second;
    }
};
struct Response {
    int status = 200;
    std::string contentType = "text/plain; charset=utf-8";
    std::string body;
    std::map<std::string, std::string> headers;
};

inline const char* reason(int s) {
    switch (s) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 431: return "Request Header Fields Too Large";
        default: return "Internal Server Error";
    }
}

inline std::string urlDecode(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() + 0 && std::isxdigit((unsigned char)s[i + 1]) && std::isxdigit((unsigned char)s[i + 2])) {
            o += static_cast<char>(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else if (s[i] == '+') o += ' ';
        else o += s[i];
    }
    return o;
}

inline void sendAll(int fd, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, 0);
        if (n <= 0) return;
        sent += static_cast<size_t>(n);
    }
}

// returns 0 on success, otherwise an HTTP status to answer with (or -1 = just close)
inline int readRequest(int fd, Request& req) {
    std::string buf;
    char tmp[8192];
    size_t headerEnd;
    while ((headerEnd = buf.find("\r\n\r\n")) == std::string::npos) {
        if (buf.size() > 65536) return 431;
        ssize_t n = ::recv(fd, tmp, sizeof tmp, 0);
        if (n <= 0) return -1;
        buf.append(tmp, static_cast<size_t>(n));
    }
    std::string head = buf.substr(0, headerEnd);
    size_t lineEnd = head.find("\r\n");
    std::string first = head.substr(0, lineEnd);
    size_t sp1 = first.find(' '), sp2 = first.rfind(' ');
    if (sp1 == std::string::npos || sp2 == sp1) return 400;
    req.method = first.substr(0, sp1);
    std::string target = first.substr(sp1 + 1, sp2 - sp1 - 1);
    size_t q = target.find('?');
    req.path = urlDecode(target.substr(0, q));
    if (q != std::string::npos) req.query = target.substr(q + 1);

    size_t pos = lineEnd == std::string::npos ? head.size() : lineEnd + 2;
    while (pos < head.size()) {
        size_t e = head.find("\r\n", pos);
        if (e == std::string::npos) e = head.size();
        std::string line = head.substr(pos, e - pos);
        size_t c = line.find(':');
        if (c != std::string::npos) {
            std::string k = line.substr(0, c), v = line.substr(c + 1);
            for (auto& ch : k) ch = static_cast<char>(std::tolower((unsigned char)ch));
            size_t a = v.find_first_not_of(" \t");
            req.headers[k] = a == std::string::npos ? "" : v.substr(a);
        }
        pos = e + 2;
    }
    size_t len = 0;
    std::string cl = req.header("content-length");
    if (!cl.empty()) len = static_cast<size_t>(std::strtoull(cl.c_str(), nullptr, 10));
    if (len > kMaxBody) return 413;
    std::string body = buf.substr(headerEnd + 4);
    if (len > body.size() && req.header("expect").find("100") != std::string::npos)
        sendAll(fd, "HTTP/1.1 100 Continue\r\n\r\n");
    while (body.size() < len) {
        ssize_t n = ::recv(fd, tmp, sizeof tmp, 0);
        if (n <= 0) return -1;
        body.append(tmp, static_cast<size_t>(n));
    }
    body.resize(len);
    req.body = std::move(body);
    return 0;
}

inline void writeResponse(int fd, const Response& r) {
    std::string out = "HTTP/1.1 " + std::to_string(r.status) + " " + reason(r.status) + "\r\n";
    out += "Content-Type: " + r.contentType + "\r\n";
    out += "Content-Length: " + std::to_string(r.body.size()) + "\r\n";
    out += "Connection: close\r\nX-Content-Type-Options: nosniff\r\n";
    for (const auto& h : r.headers) out += h.first + ": " + h.second + "\r\n";
    out += "\r\n";
    sendAll(fd, out);
    sendAll(fd, r.body);
}

inline void handleConnection(int fd, const std::function<Response(const Request&)>& handler) {
    timeval tv{15, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    Request req;
    int st = readRequest(fd, req);
    if (st > 0) {
        Response r;
        r.status = st;
        r.body = std::string(reason(st)) + "\n";
        writeResponse(fd, r);
    } else if (st == 0) {
        Response r;
        try { r = handler(req); }
        catch (const std::exception& e) { r.status = 500; r.body = std::string("Internal error: ") + e.what(); }
        writeResponse(fd, r);
    }
    ::close(fd);
}

// Blocks forever. Returns false if the socket could not be opened.
inline bool serve(int port, const std::function<Response(const Request&)>& handler) {
    std::signal(SIGPIPE, SIG_IGN);
    int srv = ::socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return false; }
    int yes = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (::bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0) {
        std::cerr << "Cannot bind to 127.0.0.1:" << port << " (" << std::strerror(errno) << "). Try another port.\n";
        return false;
    }
    if (::listen(srv, 64) < 0) { perror("listen"); return false; }
    for (;;) {
        int fd = ::accept(srv, nullptr, nullptr);
        if (fd < 0) continue;
        std::thread(handleConnection, fd, std::cref(handler)).detach();
    }
}

}  // namespace http
