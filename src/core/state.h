#ifndef BTW_CORE_STATE_H
#define BTW_CORE_STATE_H

#include <SDL3/SDL.h>

#include <vector>

#include "core/effects.h"
#include "core/entities.h"

// ==================== 运行时游戏状态 ====================
// 把原先散落在 main() 里的实体与队伍状态集中管理，供模拟/渲染模块共享。
struct GameState
{
    // 实体与特效
    std::vector<BallObject> balls;
    std::vector<PhysicsBall> physics_balls;
    std::vector<PendingPaint> old_paints;
    std::vector<PendingPaint> new_paints;
    std::vector<Particle> particles;
    std::vector<TrailDot> trail_dots;
    std::vector<Shockwave> shockwaves;
    std::vector<Bubble> bubbles;
    std::vector<Uint8> territory_flash;
    std::vector<StaticCircle> blocking_circles;

    // 队伍状态
    float machine_gun_ammo[4] = {};
    float machine_gun_angle[4] = {};
    float shield_remaining[4] = {};
    float lift_threshold = 0.0f;
    float weapon_lift_thresholds[3] = {};
    bool color_alive[4] = {true, true, true, true};

    // 胜负与采样
    bool game_over_started = false;
    Uint64 game_over_start_frame = 0;
    float hud_territory[4] = {};

    // 计时与退出条件
    Uint64 frame_count = 0;
    Uint64 game_start_time = 0;
    int max_frames = 0;
    float run_duration_seconds = 0.0f;
};

#endif  // BTW_CORE_STATE_H
