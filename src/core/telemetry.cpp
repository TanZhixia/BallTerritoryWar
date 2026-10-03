#include "core/telemetry.h"

#include "core/config.h"
#include "core/palette.h"

#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>

#include <cerrno>
#include <chrono>
#include <thread>
// ==================== 遥测 IPC（Unix 域套接字，JSON 行协议） ====================
//
// 游戏在 /tmp/btw_telemetry.sock 上监听；启动器连上后，游戏以 20Hz 推送一行 JSON：
// {"territory":[..4], "bigBalls":[..4], "totalBalls":N, "physicsValues":[..4],
//  "physicsCount":N, "shields":[..4], "ammo":[..4], "alive":[..4], "reviving":[..4],
//  "elapsed":秒, "fps":N}
// 没有客户端连接时，游戏不做任何统计（零开销）。


struct TelemetryClient
{
    int fd = -1;
    std::string buffer;  // 未处理完的接收数据
};

// 处理一行客户端命令。当前支持：
//   SETCONFIG <点分键> <值...>   —— 实时应用单个配置项（见 config.cpp）
static void HandleClientCommand(TelemetryClient &client)
{
    const std::string &line = client.buffer;
    if (line.rfind("SETCONFIG ", 0) != 0) {
        return;  // 未知命令忽略
    }
    const std::string rest = line.substr(10);  // strlen("SETCONFIG ") == 10
    const std::size_t sp = rest.find(' ');
    const std::string key = (sp == std::string::npos) ? rest : rest.substr(0, sp);
    std::vector<std::string> values;
    if (sp != std::string::npos) {
        std::string v;
        for (char ch : rest.substr(sp + 1)) {
            if (ch == ' ') {
                if (!v.empty()) {
                    values.push_back(v);
                    v.clear();
                }
            } else {
                v += ch;
            }
        }
        if (!v.empty()) {
            values.push_back(v);
        }
    }

    std::string error;
    std::string reply;
    if (key.empty() || !ApplyConfigKey(key, values, error)) {
        reply = "ERR " + error + "\n";
    } else {
        reply = "OK\n";
    }
    ::send(client.fd, reply.data(), reply.size(), MSG_DONTWAIT);
}

void TelemetryThreadMain(TelemetryState &state)
{
    const char *socket_path = "/tmp/btw_telemetry.sock";
    ::unlink(socket_path);  // 清理上次残留

    const int listen_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        return;
    }
    sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
    if (::bind(listen_fd, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) < 0 ||
        ::listen(listen_fd, 4) < 0) {
        ::close(listen_fd);
        return;
    }

    std::vector<TelemetryClient> clients;
    while (state.running.load()) {
        // 100ms 超时的 select：监听新连接 + 读取客户端命令
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(listen_fd, &readfds);
        int max_fd = listen_fd;
        for (const TelemetryClient &c : clients) {
            FD_SET(c.fd, &readfds);
            max_fd = std::max(max_fd, c.fd);
        }
        timeval tv = {};
        tv.tv_usec = 100000;
        if (::select(max_fd + 1, &readfds, nullptr, nullptr, &tv) > 0) {
            if (FD_ISSET(listen_fd, &readfds)) {
                const int client = ::accept(listen_fd, nullptr, nullptr);
                if (client >= 0) {
                    int yes = 1;
                    ::setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
                    clients.push_back(TelemetryClient{client, {}});
                    state.has_client.store(true);
                }
            }
            for (auto it = clients.begin(); it != clients.end();) {
                if (!FD_ISSET(it->fd, &readfds)) {
                    ++it;
                    continue;
                }
                char buf[4096];
                const ssize_t n = ::recv(it->fd, buf, sizeof(buf), MSG_DONTWAIT);
                if (n <= 0) {
                    // 连接关闭或出错：移除该客户端
                    ::close(it->fd);
                    it = clients.erase(it);
                    continue;
                }
                it->buffer.append(buf, static_cast<std::size_t>(n));
                // 按行处理命令
                std::size_t pos;
                while ((pos = it->buffer.find('\n')) != std::string::npos) {
                    const std::string line = it->buffer.substr(0, pos);
                    it->buffer.erase(0, pos + 1);
                    if (!line.empty()) {
                        TelemetryClient cmd_client{it->fd, line};
                        HandleClientCommand(cmd_client);
                    }
                }
                ++it;
            }
        }

        std::string json;
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            json = state.json;
        }
        if (!json.empty()) {
            const std::string line = json + "\n";
            for (auto it = clients.begin(); it != clients.end();) {
                const ssize_t n = ::send(it->fd, line.data(), line.size(), MSG_DONTWAIT);
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    ++it;  // 客户端太慢：跳过本帧，不阻塞
                } else if (n < 0) {
                    ::close(it->fd);
                    it = clients.erase(it);
                } else {
                    ++it;
                }
            }
        }
        state.has_client.store(!clients.empty());
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    for (const TelemetryClient &c : clients) {
        ::close(c.fd);
    }
    ::close(listen_fd);
    ::unlink(socket_path);
}

// 每隔 30 帧（0.5s）在主循环内采集一次统计并生成 JSON。
// 领土比例由调用方用 SampleTerritory 采好传入（HUD 与遥测共用一次采样）。
void CollectTelemetry(std::vector<BallObject> &balls,
                             const std::vector<PhysicsBall> &physics_balls,
                             const float territory[4],
                             const SDL_FColor *pure_colors,
                             const float shield_remaining[4],
                             const float machine_gun_ammo[4],
                             const bool color_alive[4],
                             const bool color_reviving[4],
                             float elapsed_minutes,
                             TelemetryState &telemetry)
{
    if (!telemetry.has_client.load()) {
        return;  // 无客户端时不采样
    }

    static Uint64 last_ticks = SDL_GetTicks();
    const Uint64 now = SDL_GetTicks();
    const float fps = 30.0f * 1000.0f /
        static_cast<float>(std::max<Uint64>(1, now - last_ticks));
    last_ticks = now;

    int big_balls[4] = {};
    for (const BallObject &ball : balls) {
        if (!ball.is_big || ball.dying) {
            continue;
        }
        ++big_balls[FindPaintColorIndex(ball, pure_colors)];
    }

    float physics_values[4] = {};
    int physics_count = 0;
    for (const PhysicsBall &ball : physics_balls) {
        physics_values[FindPhysicsColorIndex(ball, pure_colors)] += ball.value;
        ++physics_count;
    }

    char buf[1400];
    std::snprintf(
        buf, sizeof(buf),
        "{\"territory\":[%.4f,%.4f,%.4f,%.4f],\"bigBalls\":[%d,%d,%d,%d],"
        "\"totalBalls\":%zu,\"physicsValues\":[%.0f,%.0f,%.0f,%.0f],"
        "\"physicsCount\":%d,\"shields\":[%.0f,%.0f,%.0f,%.0f],"
        "\"ammo\":[%.0f,%.0f,%.0f,%.0f],\"alive\":[%s,%s,%s,%s],"
        "\"reviving\":[%s,%s,%s,%s],"
        "\"elapsed\":%.1f,\"fps\":%.1f}",
        territory[0], territory[1], territory[2], territory[3],
        big_balls[0], big_balls[1], big_balls[2], big_balls[3],
        balls.size(),
        physics_values[0], physics_values[1], physics_values[2], physics_values[3],
        physics_count,
        shield_remaining[0], shield_remaining[1], shield_remaining[2], shield_remaining[3],
        machine_gun_ammo[0], machine_gun_ammo[1], machine_gun_ammo[2], machine_gun_ammo[3],
        color_alive[0] ? "true" : "false", color_alive[1] ? "true" : "false",
        color_alive[2] ? "true" : "false", color_alive[3] ? "true" : "false",
        color_reviving[0] ? "true" : "false", color_reviving[1] ? "true" : "false",
        color_reviving[2] ? "true" : "false", color_reviving[3] ? "true" : "false",
        elapsed_minutes * 60.0f, fps);
    std::lock_guard<std::mutex> lock(telemetry.mutex);
    telemetry.json = buf;
}
