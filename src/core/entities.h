#ifndef BTW_CORE_ENTITIES_H
#define BTW_CORE_ENTITIES_H

#include <SDL3/SDL.h>

#include <cmath>

#include "core/config.h"

// ==================== 玩法实体 ====================
// 说明：BallObject 是“画笔球”（含大球/狙击/星星），PhysicsBall 是左侧机械区的物理球。

class BallObject
{
public:
    float x, y;
    float vx, vy;
    float radius;
    float value;
    SDL_FColor old_color;
    SDL_FColor new_color;
    bool is_big;
    bool is_sniper;
    bool is_fragment;  // 狙击爆炸产生的小球（会被大球按 value 引力吸引）
    bool dying;
    int big_ball_id;
    float age = 0.0f;  // 存活秒数（大球用于每 splitIntervalSeconds 秒分裂一次）

    BallObject(float start_x, float start_y, float speed, float angle,
               float ball_radius, SDL_FColor old_ball_color, SDL_FColor new_ball_color,
               float ball_value = g_config.paintBalls.initialValue, bool big = false,
               bool sniper = false, int ball_big_id = -1, bool ball_dying = false,
               bool fragment = false)
        : x(start_x), y(start_y), radius(ball_radius),
          value(ball_value), old_color(old_ball_color), new_color(new_ball_color),
          is_big(big), is_sniper(sniper), is_fragment(fragment), dying(ball_dying),
          big_ball_id(ball_big_id)
    {
        vx = std::cos(angle) * speed;
        vy = std::sin(angle) * speed;
    }

    // 位移 + 战场四壁反弹
    void Update(float dt, float min_x, float min_y, float max_x, float max_y)
    {
        x += vx * dt;
        y += vy * dt;

        if (x - radius < min_x) {
            x = min_x + radius;
            vx = std::fabs(vx);
        } else if (x + radius > max_x) {
            x = max_x - radius;
            vx = -std::fabs(vx);
        }

        if (y - radius < min_y) {
            y = min_y + radius;
            vy = std::fabs(vy);
        } else if (y + radius > max_y) {
            y = max_y - radius;
            vy = -std::fabs(vy);
        }
    }
};

// 本帧待涂画的一次落笔（先按旧色涂、移动后再按新色涂）
struct PendingPaint
{
    float x, y;
    float radius;
    SDL_FColor color;
};

// 左侧机械区的物理球
struct PhysicsBall
{
    float x, y;
    float vx, vy;
    float radius;
    float value;
    SDL_FColor color;
};

// 机械区碰撞矩形（倍率带 / 武器格 / 挡板）
struct PhysicsRect
{
    float x, y, w, h;
    float multiplier;  // 0 = 普通反弹；>0 = 武器区倍数
};

// 静态阻挡圆（机械区的灰色圆点阵：乘法带以上 3 排 + 以下 6 排，交错排列）
struct StaticCircle
{
    float x, y;
    float radius;
};

#endif  // BTW_CORE_ENTITIES_H
