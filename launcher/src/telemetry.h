#ifndef BTW_LAUNCHER_TELEMETRY_H
#define BTW_LAUNCHER_TELEMETRY_H

#include <map>
#include <string>
#include <vector>

#include "json.h"

// ==================== 遥测客户端 ====================
// 连接游戏监听的 Unix 域套接字 /tmp/btw_telemetry.sock：
//   * Poll() 非阻塞地维护连接并读取 JSON 行（无游戏时自动退避重连）
//   * ApplyConfig() 用独立短连接逐条下发 SETCONFIG 并读取 OK/ERR
class TelemetryClient
{
public:
    static constexpr const char *kSocketPath = "/tmp/btw_telemetry.sock";

    TelemetryClient() = default;
    ~TelemetryClient();
    TelemetryClient(const TelemetryClient &) = delete;
    TelemetryClient &operator=(const TelemetryClient &) = delete;

    void Poll();
    bool connected() const { return connected_; }
    const JsonValue *stats() const { return has_stats_ ? &stats_ : nullptr; }
    const std::string &last_error() const { return last_error_; }
    // 距最近一帧遥测的秒数（无数据时返回 -1）
    double StatsAge() const;

    // 返回失败项描述；空表示全部成功。未连接时返回 "游戏未运行"
    std::vector<std::string> ApplyConfig(const std::map<std::string, std::string> &updates);

private:
    void Close();

    int fd_ = -1;
    std::string buffer_;
    JsonValue stats_;
    bool has_stats_ = false;
    bool connected_ = false;
    std::string last_error_;
    double last_attempt_ = -1.0;
    double last_message_ = -1.0;
};

double NowSeconds();

#endif  // BTW_LAUNCHER_TELEMETRY_H
