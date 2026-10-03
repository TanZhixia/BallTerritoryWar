#ifndef BTW_CORE_TELEMETRY_H
#define BTW_CORE_TELEMETRY_H

#include <SDL3/SDL.h>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "core/entities.h"

// ==================== 遥测 IPC（Unix 域套接字，JSON 行协议） ====================
// 游戏在 /tmp/btw_telemetry.sock 上监听；启动器连上后推送 JSON 行。
// 没有客户端连接时，游戏不做任何统计（零开销）。

struct TelemetryState
{
    std::atomic<bool> running{true};
    std::atomic<bool> has_client{false};
    std::mutex mutex;
    std::string json;
};

void TelemetryThreadMain(TelemetryState &state);

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
                      TelemetryState &telemetry);

#endif  // BTW_CORE_TELEMETRY_H
