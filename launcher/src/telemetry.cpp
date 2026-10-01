#include "telemetry.h"

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>

double NowSeconds()
{
    using clock = std::chrono::steady_clock;
    static const clock::time_point start = clock::now();
    return std::chrono::duration<double>(clock::now() - start).count();
}

TelemetryClient::~TelemetryClient()
{
    Close();
}

void TelemetryClient::Close()
{
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    connected_ = false;
    buffer_.clear();
}

void TelemetryClient::Poll()
{
    const double now = NowSeconds();

    if (fd_ < 0) {
        // 每 0.8s 尝试重连一次，避免空转
        if (last_attempt_ >= 0.0 && now - last_attempt_ < 0.8) {
            return;
        }
        last_attempt_ = now;

        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            last_error_ = std::string("socket 创建失败：") + std::strerror(errno);
            return;
        }
        sockaddr_un addr = {};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, kSocketPath, sizeof(addr.sun_path) - 1);
        if (::connect(fd, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) != 0) {
            ::close(fd);
            last_error_ = "游戏未运行（未连接到遥测 socket）";
            return;
        }
        int yes = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
        fd_ = fd;
        connected_ = true;
        last_error_.clear();
        buffer_.clear();
    }

    char chunk[4096];
    while (true) {
        const ssize_t n = ::recv(fd_, chunk, sizeof(chunk), MSG_DONTWAIT);
        if (n > 0) {
            buffer_.append(chunk, static_cast<std::size_t>(n));
            continue;
        }
        if (n == 0) {
            Close();  // 对端关闭
            last_error_ = "游戏已退出";
            return;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        Close();
        last_error_ = std::string("遥测读取失败：") + std::strerror(errno);
        return;
    }

    std::size_t pos = 0;
    while ((pos = buffer_.find('\n')) != std::string::npos) {
        const std::string line = buffer_.substr(0, pos);
        buffer_.erase(0, pos + 1);
        if (line.empty()) {
            continue;
        }
        JsonValue parsed;
        std::string error;
        if (!JsonParse(line, parsed, error)) {
            last_error_ = "遥测 JSON 解析失败：" + error;
            continue;
        }
        if (parsed.Find("territory") == nullptr) {
            continue;  // 只接受战况帧
        }
        stats_ = std::move(parsed);
        has_stats_ = true;
        last_message_ = NowSeconds();
    }
}

double TelemetryClient::StatsAge() const
{
    if (!has_stats_ || last_message_ < 0.0) {
        return -1.0;
    }
    return NowSeconds() - last_message_;
}

std::vector<std::string> TelemetryClient::ApplyConfig(
    const std::map<std::string, std::string> &updates)
{
    std::vector<std::string> failures;
    if (updates.empty()) {
        return failures;
    }

    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        failures.push_back(std::string("socket 创建失败：") + std::strerror(errno));
        return failures;
    }
    int yes = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
    timeval send_timeout = {};
    send_timeout.tv_usec = 500000;
    timeval recv_timeout = {};
    recv_timeout.tv_usec = 300000;
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout));
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout));

    sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, kSocketPath, sizeof(addr.sun_path) - 1);
    if (::connect(fd, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        failures.push_back("游戏未运行（无法连接遥测 socket）");
        return failures;
    }

    std::string pending;
    for (const auto &kv : updates) {
        pending = "SETCONFIG " + kv.first + " " + kv.second + "\n";
        if (::send(fd, pending.data(), pending.size(), 0) < 0) {
            failures.push_back(kv.first + "：发送失败");
            break;
        }
        std::string reply;
        char c = 0;
        while (true) {
            const ssize_t n = ::recv(fd, &c, 1, 0);
            if (n <= 0) {
                break;
            }
            if (c == '\n') {
                break;
            }
            reply += c;
        }
        if (reply.rfind("OK", 0) != 0) {
            failures.push_back(kv.first + "：" + (reply.empty() ? "无响应" : reply));
        }
    }
    ::close(fd);
    return failures;
}
