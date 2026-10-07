#include "game/physics.h"

#include "core/constants.h"
#include "core/math_utils.h"
#include "core/palette.h"
#include "game/scene.h"
#include "render/canvas.h"
#include "render/font_atlas.h"
#include "render/particles.h"

#include <algorithm>
#include <cmath>
float BigBallRadius(float value)
{
    // log(x) + radiusLogOffset，整体 ×2（大小为原来的两倍），仅保留下限钳制（上限已删除）
    const float radius =
        (std::log(std::max(1.0f, value)) + g_config.bigBall.radiusLogOffset) * 2.0f;
    return std::max(g_config.bigBall.radiusMin, radius);
}

int g_next_big_ball_id = 1;  // 大球唯一编号（BIGBALL 武器格与基地转化共用）
bool g_weapon_lift_enabled = true;  // 武器带/前期限制升力开关（--no-weapon-lift 关闭）

// 水里的竖向阻尼（1/秒）：轻球浮到水面后会稳定下来，下沉的球也有个终端速度
constexpr float kWaterDragPerSecond = 6.0f;

// PURE_COLORS / NEW_COLORS（队伍配色）已移至 core/palette.cpp

static bool CircleRectCollision(const PhysicsBall &ball, const PhysicsRect &rect,
                                float &normal_x, float &normal_y, float &penetration)
{
    const float closest_x = std::max(rect.x, std::min(ball.x, rect.x + rect.w));
    const float closest_y = std::max(rect.y, std::min(ball.y, rect.y + rect.h));
    const float dx = ball.x - closest_x;
    const float dy = ball.y - closest_y;
    const float dist2 = dx * dx + dy * dy;

    if (dist2 > ball.radius * ball.radius) {
        return false;
    }

    if (dist2 > 1e-9f) {
        const float dist = std::sqrt(dist2);
        normal_x = dx / dist;
        normal_y = dy / dist;
        penetration = ball.radius - dist;
    } else {
        // 圆心在矩形内部：沿穿透最小的方向推出
        const float left = ball.x - rect.x;
        const float right = rect.x + rect.w - ball.x;
        const float top = ball.y - rect.y;
        const float bottom = rect.y + rect.h - ball.y;
        const float min_axis = std::min(std::min(left, right), std::min(top, bottom));
        if (min_axis == left) {
            normal_x = -1.0f; normal_y = 0.0f; penetration = left + ball.radius;
        } else if (min_axis == right) {
            normal_x = 1.0f; normal_y = 0.0f; penetration = right + ball.radius;
        } else if (min_axis == top) {
            normal_x = 0.0f; normal_y = -1.0f; penetration = top + ball.radius;
        } else {
            normal_x = 0.0f; normal_y = 1.0f; penetration = bottom + ball.radius;
        }
    }
    return true;
}

// GetColorBlockCenter / GetBlockingCircles（场景布局几何）已移至 game/scene.cpp

// Find*ColorIndex（配色反查）已移至 core/palette.cpp；
// CanvasPixelMatches（画布像素查询）已移至 render/canvas.cpp

void UpdatePhysicsBalls(std::vector<PhysicsBall> &balls, float dt,
                               std::vector<BallObject> &paint_balls,
                               const SDL_FColor *pure_colors,
                               const SDL_FColor *new_colors,
                               float machine_gun_ammo[4],
                               float machine_gun_angle[4],
                               const std::vector<StaticCircle> &static_circles,
                               float &lift_threshold,
                               const float weapon_lift_thresholds[3],
                               float shield_remaining[4],
                               bool high_value_lift_enabled,
                               std::vector<Shockwave> &waves,
                               const float revival_wait[4])
{
    const PhysicsRect bounce_rects[] = {
        {0.0f, 0.0f, 20.0f, 1000.0f, 0.0f},     // 左边框
        {580.0f, 0.0f, 20.0f, 1000.0f, 0.0f},   // 右边框
        {0.0f, 0.0f, 600.0f, 20.0f, 0.0f},      // 上边框
        {0.0f, 980.0f, 600.0f, 20.0f, 0.0f},    // 底部条（兜底反弹）
        {0.0f, 390.0f, 225.0f, 20.0f, 0.0f},    // 左武器区（兜底反弹）
        {375.0f, 390.0f, 225.0f, 20.0f, 0.0f},  // 右武器区（兜底反弹）
        {70.0f, 310.0f, 10.0f, 100.0f, 0.0f},   // *8|*4 挡板（左，底边贴 *x）
        {145.0f, 310.0f, 10.0f, 100.0f, 0.0f},  // *4|*2 挡板（左，底边贴 *x）
        {445.0f, 310.0f, 10.0f, 100.0f, 0.0f},  // *2|*4 挡板（右，底边贴 *x）
        {520.0f, 310.0f, 10.0f, 100.0f, 0.0f},  // *4|*8 挡板（右，底边贴 *x）
        {0.0f, 410.0f, 225.0f, 20.0f, 0.0f},    // *x 下方横挡板（左，紧贴）
        {375.0f, 410.0f, 225.0f, 20.0f, 0.0f},  // *x 下方横挡板（右，紧贴）
    };
    // 乘法带（*8/*4/*2）的上沿：升力区不得越过它，否则会把带下方的球顶回带上
    constexpr float BAND_TOP_Y = 390.0f;

    const PhysicsRect special_rects[] = {
        {0.0f, 390.0f, 75.0f, 20.0f, 8.0f},     // *8
        {75.0f, 390.0f, 75.0f, 20.0f, 4.0f},    // *4
        {150.0f, 390.0f, 75.0f, 20.0f, 2.0f},   // *2
        {375.0f, 390.0f, 75.0f, 20.0f, 2.0f},   // *2
        {450.0f, 390.0f, 75.0f, 20.0f, 4.0f},   // *4
        {525.0f, 390.0f, 75.0f, 20.0f, 8.0f},   // *8
    };

    for (PhysicsBall &ball : balls) {
        // 复活飞行中的球：不参与重力/升力/挡板/武器格/越界兜底，由
        // UpdateRevivingBall 单独推进（它会飞出自己的机械区，去战场炮塔）
        if (ball.reviving) {
            continue;
        }
        ball.vy += g_config.physics.gravity * dt;
        // 水中的浮力（缺口 → 下方挡板那一柱）：物理球的"质量"与 value 成正比，
        // 而浮力对所有球都相同，取「浮力加速度 = g × 阈值 / value」；阈值就是
        // lift_threshold，按 lift.growthPerSecond 每秒 +1% 复利增长 → 浮力随时间变大。
        // 注意重力上面已经加过，这里只补浮力这一项，净加速度才是
        //     a = g × (1 − 阈值 / value)
        //   value < 阈值 → 上浮（越轻越快，浮力上限沿用 lift.acceleration）
        //   value = 阈值 → 悬浮；value > 阈值 → 下沉（浮力一直托着，所以比空气中慢）
        // 再叠一层竖向流体阻尼：轻球会稳稳浮在水面附近，而不是无限来回弹。
        if (InLiftWater(ball.x, ball.y)) {
            const float ratio = lift_threshold / std::max(1.0f, ball.value);
            const float buoyancy = std::min(g_config.physics.gravity * ratio,
                                            g_config.lift.acceleration);
            ball.vy -= buoyancy * dt;
            ball.vy *= std::max(0.0f, 1.0f - kWaterDragPerSecond * dt);
        }
        // *8/*4/*2 上方升力：高于对应阈值的球生效（启动器可关闭）。
        // 只作用于乘法带以上（y ≤ 带顶 390）：已经落到带以下的球不再被顶回带上，
        // 否则会被抬回去重复吃倍率、来回上下弹。
        if (g_weapon_lift_enabled && ball.y >= 300.0f && ball.y <= BAND_TOP_Y) {
            const bool in_8x =
                (ball.x >= 0.0f && ball.x < 75.0f) ||
                (ball.x >= 525.0f && ball.x < 600.0f);
            const bool in_4x =
                (ball.x >= 75.0f && ball.x < 150.0f) ||
                (ball.x >= 450.0f && ball.x < 525.0f);
            const bool in_2x =
                (ball.x >= 150.0f && ball.x < 225.0f) ||
                (ball.x >= 375.0f && ball.x < 450.0f);
            if (in_8x && ball.value >= weapon_lift_thresholds[0]) {
                ball.vy -= g_config.lift.acceleration * dt;
            } else if (in_4x && ball.value >= weapon_lift_thresholds[1]) {
                ball.vy -= g_config.lift.acceleration * dt;
            } else if (in_2x && ball.value >= weapon_lift_thresholds[2]) {
                ball.vy -= g_config.lift.acceleration * dt;
            }
        }
        // 前期限制：10 分钟内 value > 2M 的球在霰弹/狙击列会被升力顶回去
        // （启动器可关闭：勾选后霰弹/狙击列的升力消失，中间升力不变）
        if (g_weapon_lift_enabled && high_value_lift_enabled &&
            ball.value > g_config.lift.highValueLimit &&
            ball.y >= 700.0f && ball.y <= 980.0f) {
            const bool in_shotgun = ball.x >= 0.0f && ball.x < 120.0f;
            const bool in_sniper = ball.x >= 480.0f && ball.x < 600.0f;
            if (in_shotgun || in_sniper) {
                ball.vy -= g_config.lift.acceleration * dt;
            }
        }
        ball.x += ball.vx * dt;
        ball.y += ball.vy * dt;

        

        bool reset = false;
        for (const PhysicsRect &rect : special_rects) {
            float nx = 0.0f, ny = 0.0f, penetration = 0.0f;
            if (CircleRectCollision(ball, rect, nx, ny, penetration)) {
                ball.value *= rect.multiplier;  // *2 *4 *8（底部武器栏不在 special_rects 里）
                // 冲击波：从落点（球当前所在）向外扩散的白色小波
                SpawnShockwave(waves, ball.x, ball.y, 20.0f, 0.45f, 2.5f,
                               SDL_FColor{1.0f, 1.0f, 1.0f, 1.0f});
                ball.x = 300.0f;
                ball.y = 100.0f;
                const float angle = RandFloat() * 2.0f * static_cast<float>(M_PI);
                ball.vx = std::cos(angle) * g_config.physics.launchSpeed;
                ball.vy = std::sin(angle) * g_config.physics.launchSpeed;
                reset = true;
                break;
            }
        }
        if (reset) {
            continue;
        }

        // 复活等待期（基地已失守、等着复活）：该颜色的物理球落入任意武器格都不发射武器，
        // 而是把价值转化为该队护盾（为复活后的护盾圈攒量），球本身重置回顶部继续跑
        if (revival_wait != nullptr &&
            revival_wait[FindPhysicsColorIndex(ball, pure_colors)] > 0.0f) {
            bool in_any_slot = false;
            for (int slot = 0; slot < 5 && !in_any_slot; ++slot) {
                const PhysicsRect slot_rect = {
                    static_cast<float>(slot) * 120.0f, 980.0f, 120.0f, 20.0f, 0.0f};
                float slot_nx = 0.0f, slot_ny = 0.0f, slot_pen = 0.0f;
                in_any_slot = CircleRectCollision(ball, slot_rect, slot_nx, slot_ny, slot_pen);
            }
            if (in_any_slot) {
                const int color_index = FindPhysicsColorIndex(ball, pure_colors);
                SpawnShockwave(waves, ball.x, ball.y, 24.0f, 0.5f, 2.5f,
                               SDL_FColor{1.0f, 1.0f, 1.0f, 1.0f});
                shield_remaining[color_index] +=
                    ball.value * g_config.revive.shieldValueScale;
                ball.value = 1.0f;  // 转化为护盾后恢复 1
                ball.x = 300.0f;
                ball.y = 100.0f;
                const float angle = RandFloat() * 2.0f * static_cast<float>(M_PI);
                ball.vx = std::cos(angle) * g_config.physics.launchSpeed;
                ball.vy = std::sin(angle) * g_config.physics.launchSpeed;
                continue;
            }
        }

        // 底部武器格（与画布视觉一致，共 600px，5 格各 120px）：
        //   0-120 霰弹 / 120-240 机枪 / 240-360 护盾 / 360-480 大球 / 480-600 狙击
        // 霰弹：底部第一格，把 value 分成最多 1000 份发射（每份 = value/份数）
        const PhysicsRect shotgun_rect = {0.0f, 980.0f, 120.0f, 20.0f, 0.0f};
        float shotgun_nx = 0.0f, shotgun_ny = 0.0f, shotgun_pen = 0.0f;
        if (CircleRectCollision(ball, shotgun_rect, shotgun_nx, shotgun_ny, shotgun_pen)) {
            const int color_index = FindPhysicsColorIndex(ball, pure_colors);
            SpawnShockwave(waves, ball.x, ball.y, 24.0f, 0.5f, 2.5f,
                           SDL_FColor{1.0f, 1.0f, 1.0f, 1.0f});
            float center_x = 0.0f, center_y = 0.0f;
            GetColorBlockCenter(color_index, center_x, center_y);

            const float base_angle = std::atan2(ball.vy, ball.vx);
            const int shot_count = std::max(
                1, std::min(static_cast<int>(ball.value / g_config.shotgun.fragmentValue),
                            g_config.shotgun.maxFragments));
            const float ball_value = ball.value / static_cast<float>(shot_count);
            for (int i = 0; i < shot_count; ++i) {
                const float random_offset_deg =
                    RandFloat() * g_config.shotgun.spreadDeg -
                    g_config.shotgun.spreadDeg * 0.5f;
                const float angle =
                    base_angle + random_offset_deg * static_cast<float>(M_PI) / 180.0f;
                paint_balls.emplace_back(
                    center_x,
                    center_y,
                    g_config.shotgun.speed,
                    angle,
                    g_config.paintBalls.radius,
                    pure_colors[color_index],
                    new_colors[color_index],
                    ball_value);
            }

            ball.value = 1.0f;  // 霰弹发射后恢复 1
            ball.x = 300.0f;
            ball.y = 100.0f;
            const float angle = RandFloat() * 2.0f * static_cast<float>(M_PI);
            ball.vx = std::cos(angle) * g_config.physics.launchSpeed;
            ball.vy = std::sin(angle) * g_config.physics.launchSpeed;
            continue;
        }

        // 机枪：底部第二格（120-240），把 value 加入对应颜色的变量
        const PhysicsRect machine_gun_rect = {120.0f, 980.0f, 120.0f, 20.0f, 0.0f};
        float mg_nx = 0.0f, mg_ny = 0.0f, mg_pen = 0.0f;
        if (CircleRectCollision(ball, machine_gun_rect, mg_nx, mg_ny, mg_pen)) {
            const int color_index = FindPhysicsColorIndex(ball, pure_colors);
            SpawnShockwave(waves, ball.x, ball.y, 24.0f, 0.5f, 2.5f,
                           SDL_FColor{1.0f, 1.0f, 1.0f, 1.0f});
            machine_gun_ammo[color_index] += ball.value;
            ball.value = 1.0f;  // 机枪装填后恢复 1
            ball.x = 300.0f;
            ball.y = 100.0f;
            const float angle = RandFloat() * 2.0f * static_cast<float>(M_PI);
            ball.vx = std::cos(angle) * g_config.physics.launchSpeed;
            ball.vy = std::sin(angle) * g_config.physics.launchSpeed;
            continue;
        }

        // 护盾：底部第三格，把 value 加入对应颜色的护盾
        const PhysicsRect shield_rect = {240.0f, 980.0f, 120.0f, 20.0f, 0.0f};
        float shield_nx = 0.0f, shield_ny = 0.0f, shield_pen = 0.0f;
        if (CircleRectCollision(ball, shield_rect, shield_nx, shield_ny, shield_pen)) {
            const int color_index = FindPhysicsColorIndex(ball, pure_colors);
            SpawnShockwave(waves, ball.x, ball.y, 24.0f, 0.5f, 2.5f,
                           SDL_FColor{1.0f, 1.0f, 1.0f, 1.0f});
            shield_remaining[color_index] += ball.value;
            ball.value = 1.0f;  // 护盾充能后恢复 1
            ball.x = 300.0f;
            ball.y = 100.0f;
            const float angle = RandFloat() * 2.0f * static_cast<float>(M_PI);
            ball.vx = std::cos(angle) * g_config.physics.launchSpeed;
            ball.vy = std::sin(angle) * g_config.physics.launchSpeed;
            continue;
        }

        // 大球：底部第四格，生成一个大球
        const PhysicsRect big_ball_rect = {360.0f, 980.0f, 120.0f, 20.0f, 0.0f};
        float big_nx = 0.0f, big_ny = 0.0f, big_pen = 0.0f;
        if (CircleRectCollision(ball, big_ball_rect, big_nx, big_ny, big_pen)) {
            const int color_index = FindPhysicsColorIndex(ball, pure_colors);
            SpawnShockwave(waves, ball.x, ball.y, 24.0f, 0.5f, 2.5f,
                           SDL_FColor{1.0f, 1.0f, 1.0f, 1.0f});
            float center_x = 0.0f, center_y = 0.0f;
            GetColorBlockCenter(color_index, center_x, center_y);
            const float angle = RandFloat() * 2.0f * static_cast<float>(M_PI);
            paint_balls.emplace_back(
                center_x,
                center_y,
                g_config.paintBalls.speed * g_config.bigBall.speedFactor,
                angle,
                BigBallRadius(ball.value),
                pure_colors[color_index],
                new_colors[color_index],
                ball.value,
                true,          // 大球
                false,         // 不是狙击
                g_next_big_ball_id++);
            ball.value = 1.0f;  // 大球生成后恢复 1
            ball.x = 300.0f;
            ball.y = 100.0f;
            const float reset_angle = RandFloat() * 2.0f * static_cast<float>(M_PI);
            ball.vx = std::cos(reset_angle) * g_config.physics.launchSpeed;
            ball.vy = std::sin(reset_angle) * g_config.physics.launchSpeed;
            continue;
        }

        // 狙击：底部第五格（x 480~600，120px），发射一个继承全部 value 的粒子
        const PhysicsRect sniper_rect = {480.0f, 980.0f, 120.0f, 20.0f, 0.0f};
        float sniper_nx = 0.0f, sniper_ny = 0.0f, sniper_pen = 0.0f;
        if (CircleRectCollision(ball, sniper_rect, sniper_nx, sniper_ny, sniper_pen)) {
            const int color_index = FindPhysicsColorIndex(ball, pure_colors);
            SpawnShockwave(waves, ball.x, ball.y, 24.0f, 0.5f, 2.5f,
                           SDL_FColor{1.0f, 1.0f, 1.0f, 1.0f});
            float center_x = 0.0f, center_y = 0.0f;
            GetColorBlockCenter(color_index, center_x, center_y);
            const float angle = RandFloat() * 2.0f * static_cast<float>(M_PI);
            paint_balls.emplace_back(
                center_x,
                center_y,
                g_config.sniper.speed,
                angle,
                g_config.paintBalls.radius,
                pure_colors[color_index],
                new_colors[color_index],
                ball.value,
                false,
                true);  // 狙击粒子
            ball.value = 1.0f;  // 狙击发射后恢复 1
            ball.x = 300.0f;
            ball.y = 100.0f;
            const float reset_angle = RandFloat() * 2.0f * static_cast<float>(M_PI);
            ball.vx = std::cos(reset_angle) * g_config.physics.launchSpeed;
            ball.vy = std::sin(reset_angle) * g_config.physics.launchSpeed;
            continue;
        }

        // 越界兜底：防止任何情况下球消失
        if (ball.x < 0.0f || ball.x > 600.0f ||
            ball.y < 0.0f || ball.y > 1000.0f) {
            ball.x = 300.0f;
            ball.y = 100.0f;
            const float angle = RandFloat() * 2.0f * static_cast<float>(M_PI);
            ball.vx = std::cos(angle) * g_config.physics.launchSpeed;
            ball.vy = std::sin(angle) * g_config.physics.launchSpeed;
            ball.value = 1.0f;
            continue;
        }

        for (const PhysicsRect &rect : bounce_rects) {
            float nx = 0.0f, ny = 0.0f, penetration = 0.0f;
            if (!CircleRectCollision(ball, rect, nx, ny, penetration)) {
                continue;
            }
            ball.x += nx * penetration;
            ball.y += ny * penetration;
            const float vn = ball.vx * nx + ball.vy * ny;
            if (vn < 0.0f) {
                ball.vx -= (1.0f + g_config.physics.restitution) * vn * nx;
                ball.vy -= (1.0f + g_config.physics.restitution) * vn * ny;
            }
        }

        for (const StaticCircle &circle : static_circles) {
            const float dx = ball.x - circle.x;
            const float dy = ball.y - circle.y;
            const float min_dist = ball.radius + circle.radius;
            const float dist2 = dx * dx + dy * dy;
            if (dist2 >= min_dist * min_dist) {
                continue;
            }

            float nx = 0.0f, ny = 0.0f, penetration = 0.0f;
            if (dist2 > 1e-9f) {
                const float dist = std::sqrt(dist2);
                nx = dx / dist;
                ny = dy / dist;
                penetration = min_dist - dist;
            } else {
                nx = 0.0f;
                ny = -1.0f;
                penetration = min_dist;
            }

            ball.x += nx * penetration;
            ball.y += ny * penetration;
            const float vn = ball.vx * nx + ball.vy * ny;
            if (vn < 0.0f) {
                ball.vx -= (1.0f + g_config.physics.restitution) * vn * nx;
                ball.vy -= (1.0f + g_config.physics.restitution) * vn * ny;
            }
        }
    }

    for (std::size_t i = 0; i < balls.size(); ++i) {
        for (std::size_t j = i + 1; j < balls.size(); ++j) {
            PhysicsBall &a = balls[i];
            PhysicsBall &b = balls[j];
            const float dx = b.x - a.x;
            const float dy = b.y - a.y;
            const float min_dist = a.radius + b.radius;
            const float dist2 = dx * dx + dy * dy;
            if (dist2 >= min_dist * min_dist) {
                continue;
            }

            if (dist2 > 1e-9f) {
                const float dist = std::sqrt(dist2);
                const float nx = dx / dist;
                const float ny = dy / dist;
                const float overlap = (min_dist - dist) * 0.5f;
                a.x -= nx * overlap;
                a.y -= ny * overlap;
                b.x += nx * overlap;
                b.y += ny * overlap;

                const float vn = (a.vx - b.vx) * nx + (a.vy - b.vy) * ny;
                if (vn > 0.0f) {
                    const float impulse =
                        -(1.0f + g_config.physics.restitution) * vn * 0.5f;
                    a.vx += impulse * nx;
                    a.vy += impulse * ny;
                    b.vx -= impulse * nx;
                    b.vy -= impulse * ny;
                }
            } else {
                a.x -= 0.5f;
            }
        }
    }

    // 机枪：每帧对每个有弹药的 颜色发射至多 10 个球（frame_cost/ball_value，弹药按 1/12000/帧 消耗）
    for (int color_index = 0; color_index < 4; ++color_index) {
        if (machine_gun_ammo[color_index] <= 0.0f) {
            continue;
        }

        const float ammo = machine_gun_ammo[color_index];
        const float frame_cost = ammo / g_config.machineGun.drainDivisor;
        const float base_ball_value = std::max(
            frame_cost / g_config.machineGun.maxBallsPerFrame,
            g_config.machineGun.minBallValue);
        int shot_count = std::max(1, static_cast<int>(frame_cost / base_ball_value));
        float ball_value = base_ball_value;
        float center_x = 0.0f, center_y = 0.0f;
        GetColorBlockCenter(color_index, center_x, center_y);

        // 锁定瞄准：敌方大球离基地 <= lockDistance 时，计算精准命中——
        // 按子弹飞行时间（距离/弹速）预测大球到达位置再瞄准
        bool locked = false;
        float locked_big_value = 0.0f;
        float locked_big_speed = 0.0f;
        float locked_big_x = 0.0f, locked_big_y = 0.0f, locked_big_radius = 0.0f;
        const float lock_dist = g_config.machineGun.lockDistance;
        const float mg_speed = g_config.machineGun.speed;
        float best_dist2 = lock_dist * lock_dist;
        for (const BallObject &big : paint_balls) {
            if (!big.is_big || big.dying) {
                continue;
            }
            const int big_color = FindPaintColorIndex(big, pure_colors);
            if (big_color == color_index) {
                continue;  // 友方大球不锁
            }
            const float dx = big.x - center_x;
            const float dy = big.y - center_y;
            const float dist2 = dx * dx + dy * dy;
            if (dist2 > best_dist2) {
                continue;
            }
            // 精准命中：逐步模拟大球未来路径（含 frame 四壁反弹），
            // 求"子弹到达时间 == 大球到达时间"的拦截点；无精确拦截时取最接近点
            float sim_x = big.x;
            float sim_y = big.y;
            float sim_vx = big.vx;
            float sim_vy = big.vy;
            // 模拟窗口 = 锁定距离/弹速 × 1.5：拦截点可能略超锁定距离（子弹仍可追击）
            const float max_time = 1.5f * lock_dist / mg_speed;
            constexpr float SIM_DT = 1.0f / 60.0f;
            float aim_x = dx;
            float aim_y = dy;
            float best_err = 1e9f;
            for (float t = SIM_DT; t <= max_time + 1e-4f; t += SIM_DT) {
                sim_x += sim_vx * SIM_DT;
                sim_y += sim_vy * SIM_DT;
                // 模拟 frame 墙壁反弹（与大球实际碰撞一致）
                if (sim_x - big.radius < FRAME_X) {
                    sim_x = FRAME_X + big.radius;
                    sim_vx = std::fabs(sim_vx);
                } else if (sim_x + big.radius > FRAME_X + FRAME_SIZE) {
                    sim_x = FRAME_X + FRAME_SIZE - big.radius;
                    sim_vx = -std::fabs(sim_vx);
                }
                if (sim_y - big.radius < FRAME_Y) {
                    sim_y = FRAME_Y + big.radius;
                    sim_vy = std::fabs(sim_vy);
                } else if (sim_y + big.radius > FRAME_Y + FRAME_SIZE) {
                    sim_y = FRAME_Y + FRAME_SIZE - big.radius;
                    sim_vy = -std::fabs(sim_vy);
                }
                const float sx = sim_x - center_x;
                const float sy = sim_y - center_y;
                const float sd = std::sqrt(sx * sx + sy * sy);
                const float bullet_t = sd / mg_speed;
                const float err = std::fabs(bullet_t - t);
                if (err < best_err) {
                    best_err = err;
                    aim_x = sx;
                    aim_y = sy;
                }
                if (err < SIM_DT * 0.5f) {
                    break;  // 拦截成功：子弹与大球同时到达该点
                }
            }
            best_dist2 = dist2;
            locked = true;
            locked_big_value = big.value;
            locked_big_x = big.x;
            locked_big_y = big.y;
            locked_big_radius = big.radius;
            locked_big_speed = std::sqrt(big.vx * big.vx + big.vy * big.vy);
            machine_gun_angle[color_index] = std::atan2(aim_y, aim_x);
        }

        // 威胁评估（会击中 = 大球边缘与护盾圆相触/相交）：
        //   不会击中 → 1x 装样子
        //   会击中但撞击后球立即死亡（护盾能吞：shield >= big×absorbRatio）→ 1x
        //   会击中且护盾立即破碎（big > shield 且小方被 damage 击穿，shield→0）→ 认真打：发射等值总和
        //   其余 → 10x
        int threat = 0;
        float threat_total_per_frame = 0.0f;
        if (locked) {
            float shield_x = 0.0f, shield_y = 0.0f;
            GetColorBlockCenter(color_index, shield_x, shield_y);
            const float dxs = locked_big_x - shield_x;
            const float dys = locked_big_y - shield_y;
            const float touch_r = g_config.shield.radius + locked_big_radius;
            const float my_shield = shield_remaining[color_index];
            const float big_val = locked_big_value;
            if (dxs * dxs + dys * dys <= touch_r * touch_r) {
                if (my_shield >= big_val * g_config.combat.absorbRatio) {
                    threat = 0;  // 护盾一口吞掉，球撞击即死
                } else {
                    const float larger = std::max(my_shield, big_val);
                    const float smaller = std::min(my_shield, big_val);
                    const float damage = larger * g_config.combat.damageRatio;
                    const bool shield_broken =
                        (smaller - damage <= 0.0f) && (big_val > my_shield);
                    if (shield_broken) {
                        threat = 2;  // 撞击后护盾立刻破碎
                        const float dist = std::sqrt(best_dist2);
                        const float frames = std::max(
                            1.0f, dist / std::max(1.0f, locked_big_speed) * 60.0f);
                        threat_total_per_frame = big_val / frames;
                    } else {
                        threat = 1;  // 10x
                    }
                }
            }
        }

        // 按威胁决定单发价值（球数不变，只调 value）
        if (locked) {
            if (threat == 2) {
                ball_value = std::max(
                    threat_total_per_frame / static_cast<float>(shot_count),
                    base_ball_value);
            } else if (threat == 1) {
                ball_value *= 10.0f;  // 中威胁：10 倍
            }
            // 低威胁：保持 1x 做做样子
        }

        // 梭哈：仅高威胁且机枪总弹药 < 大球价值时，全部弹药一次性打出（霰弹模式）
        bool shotgun_dump = false;
        if (locked && threat == 2 && machine_gun_ammo[color_index] < locked_big_value) {
            shotgun_dump = true;
            const int dump_count = std::max(
                1, std::min(static_cast<int>(machine_gun_ammo[color_index] / ball_value),
                            g_config.shotgun.maxFragments));
            shot_count = dump_count;
            ball_value = machine_gun_ammo[color_index] / static_cast<float>(dump_count);
        }

        for (int i = 0; i < shot_count; ++i) {
            const float random_offset_deg =
                RandFloat() * g_config.machineGun.spreadDeg -
                g_config.machineGun.spreadDeg * 0.5f;
            const float angle =
                machine_gun_angle[color_index] +
                random_offset_deg * static_cast<float>(M_PI) / 180.0f;
            paint_balls.emplace_back(
                center_x,
                center_y,
                mg_speed,
                angle,
                g_config.paintBalls.radius,
                pure_colors[color_index],
                new_colors[color_index],
                ball_value);
        }

        if (shotgun_dump) {
            machine_gun_ammo[color_index] = 0.0f;  // 霰弹模式：弹药清空
        } else {
            float drain = frame_cost;
            if (locked && threat == 2) {
                drain = std::max(frame_cost, threat_total_per_frame);  // 高威胁：按发射总量消耗
            }
            machine_gun_ammo[color_index] -= std::min(drain, machine_gun_ammo[color_index]);
            if (machine_gun_ammo[color_index] < 0.0f) {
                machine_gun_ammo[color_index] = 0.0f;
            }
        }
        if (!locked) {
            machine_gun_angle[color_index] +=
                g_config.machineGun.rotateDegPerFrame * static_cast<float>(M_PI) / 180.0f;
            machine_gun_angle[color_index] =
                std::fmod(machine_gun_angle[color_index],
                          2.0f * static_cast<float>(M_PI));  // 0~360° 循环
        }
    }

}

// 复活飞行推进：先原地停顿 stopSeconds（速度清零），再以固定速度飞向炮塔。
// 返回 true 表示本帧已到达目标（调用方执行复活流程，并把该球移除）。
bool UpdateRevivingBall(PhysicsBall &ball, float dt)
{
    if (!ball.reviving) {
        return false;
    }
    if (ball.revive_stop > 0.0f) {
        ball.revive_stop -= dt;
        ball.vx = 0.0f;  // 动画第一阶段：原地停住不动
        ball.vy = 0.0f;
        if (ball.revive_stop > 0.0f) {
            return false;
        }
    }
    const float dx = ball.target_x - ball.x;
    const float dy = ball.target_y - ball.y;
    const float distance = std::sqrt(dx * dx + dy * dy);
    const float speed = g_config.revive.flightSpeed;
    const float step = speed * dt;
    if (distance <= step || distance < 0.001f) {
        ball.x = ball.target_x;
        ball.y = ball.target_y;
        ball.vx = 0.0f;
        ball.vy = 0.0f;
        return true;  // 到达炮塔
    }
    ball.vx = dx / distance * speed;
    ball.vy = dy / distance * speed;
    ball.x += ball.vx * dt;
    ball.y += ball.vy * dt;
    return false;
}

namespace {
void DrawOnePhysicsBall(std::vector<Uint8> &canvas, int canvas_width, int canvas_height,
                        const PhysicsBall &ball, const SDL_FColor &text_color)
{
    PaintCircle(canvas, canvas_width, canvas_height, ball.x, ball.y, ball.radius, ball.color);
    const std::string value_text = FormatValue(ball.value);
    const int text_width = MeasureTextFont(value_text.c_str());
    DrawTextFont(canvas, canvas_width, canvas_height,
                 static_cast<int>(ball.x) - text_width / 2,
                 static_cast<int>(ball.y) - FontLineHeight() / 2,
                 value_text.c_str(), text_color);
}
}  // namespace

void DrawPhysicsBalls(std::vector<Uint8> &canvas, int canvas_width, int canvas_height,
                             const std::vector<PhysicsBall> &balls)
{
    const SDL_FColor text_color = SDL_FColor{1.0f, 1.0f, 1.0f, 1.0f};
    for (const PhysicsBall &ball : balls) {
        if (ball.reviving) {
            continue;  // 复活飞行的球画在最上层（见 DrawRevivingPhysicsBalls）
        }
        DrawOnePhysicsBall(canvas, canvas_width, canvas_height, ball, text_color);
    }
}

void DrawRevivingPhysicsBalls(std::vector<Uint8> &canvas, int canvas_width, int canvas_height,
                              const std::vector<PhysicsBall> &balls)
{
    const SDL_FColor text_color = SDL_FColor{1.0f, 1.0f, 1.0f, 1.0f};
    for (const PhysicsBall &ball : balls) {
        if (!ball.reviving) {
            continue;
        }
        DrawOnePhysicsBall(canvas, canvas_width, canvas_height, ball, text_color);
    }
}

void ApplySniperGravity(std::vector<BallObject> &balls, float dt)
{
    const float radius = g_config.sniper.gravityRadius;
    const float G = g_config.sniper.gravityStrength;
    const float max_speed = g_config.sniper.gravityMaxSpeed;
    const float value_gain = g_config.sniper.gravityValueGain;  // 每秒钟 +value
    if (radius <= 0.0f) {
        return;  // 半径 0 = 没有引力场
    }
    // 引力与“场内增值”是两个独立效果：任意一个开启都需要继续遍历
    const bool gravity_enabled = (G > 0.0f && max_speed > 0.0f);
    if (!gravity_enabled && value_gain <= 0.0f) {
        return;
    }
    const float radius2 = radius * radius;

    // 先收集狙击（引力源；狙击自身不被吸引，位置保持其正常飞行路径）
    std::vector<const BallObject *> snipers;
    for (const BallObject &b : balls) {
        if (b.is_sniper && !b.dying) {
            snipers.push_back(&b);
        }
    }
    if (snipers.empty()) {
        return;
    }



    for (BallObject &ball : balls) {
        if (ball.is_sniper || ball.dying) {
            continue;  // 狙击是引力源自身不被吸引
        }
        float ax = 0.0f;
        float ay = 0.0f;
        bool in_field = false;
        for (const BallObject *s : snipers) {
            const float dx = s->x - ball.x;
            const float dy = s->y - ball.y;
            const float dist2 = dx * dx + dy * dy;
            if (dist2 > radius2 || dist2 < 1e-6f) {
                continue;
            }
            in_field = true;
            if (!gravity_enabled) {
                continue;
            }
            // 牛顿引力：a = G * M / r^2，指向狙击（质量 = 狙击浓缩的价值）
            const float a = G * s->value / dist2;
            const float inv_dist = 1.0f / std::sqrt(dist2);
            ax += dx * inv_dist * a;
            ay += dy * inv_dist * a;
        }
        // 特性：进入狙击引力场的“小球”每秒 +value_gain（大球与狙击不计）
        if (in_field && !ball.is_big && value_gain > 0.0f) {
            ball.value += value_gain * dt;
        }
        if (!gravity_enabled) {
            continue;
        }
        if (ax == 0.0f && ay == 0.0f) {
            continue;
        }
        ball.vx += ax * dt;
        ball.vy += ay * dt;
        // 速度上限：防止高价值狙击把球甩成数值失控
        const float speed = std::sqrt(ball.vx * ball.vx + ball.vy * ball.vy);
        if (speed > max_speed) {
            ball.vx = ball.vx / speed * max_speed;
            ball.vy = ball.vy / speed * max_speed;
        }
    }
}

// 大球引力场（质量一律按 value 线性，a = G × 大球value / r²）：
//   * 敌对大球之间互相吸引（同色不吸）
//   * 狙击爆炸产生的碎片（is_fragment）被大球吸引；己方（同色）大球不吸引它
// 速度上限 gravityMaxSpeed 兜底，避免高价值大球互吸时数值失控。
void ApplyBigBallGravity(std::vector<BallObject> &balls, const SDL_FColor *pure_colors, float dt)
{
    const float radius = g_config.bigBall.gravityRadius;
    const float G = g_config.bigBall.gravityStrength;
    const float fragment_G = g_config.bigBall.fragmentGravityStrength;
    const float max_speed = g_config.bigBall.gravityMaxSpeed;
    if (radius <= 0.0f || max_speed <= 0.0f) {
        return;  // 半径 0 或没有速度上限时不启用引力场
    }
    if (G <= 0.0f && fragment_G <= 0.0f) {
        return;  // 两个效果都关闭
    }
    const float radius2 = radius * radius;

    // 收集大球引力源
    std::vector<const BallObject *> sources;
    for (const BallObject &b : balls) {
        if (b.is_big && !b.dying) {
            sources.push_back(&b);
        }
    }
    if (sources.empty()) {
        return;
    }

    for (BallObject &ball : balls) {
        if (ball.dying) {
            continue;
        }
        const bool is_big_ball = ball.is_big;
        const bool is_fragment = ball.is_fragment;  // 狙击爆炸碎片
        if (!is_big_ball && !is_fragment) {
            continue;  // 其他小球不受大球引力影响
        }
        // 大球之间与狙击碎片都按 value 线性（“按 value 引力”）
        const float gravity_g = is_big_ball ? G : fragment_G;
        if (gravity_g <= 0.0f) {
            continue;
        }
        float ax = 0.0f;
        float ay = 0.0f;
        for (const BallObject *s : sources) {
            if (&ball == s) {
                continue;  // 不吸引自身
            }
            const int src_color = FindPaintColorIndex(*s, pure_colors);
            if (FindPaintColorIndex(ball, pure_colors) == src_color) {
                continue;  // 不吸引同色（同队/自己尾流）
            }
            const float dx = s->x - ball.x;
            const float dy = s->y - ball.y;
            const float r2 = dx * dx + dy * dy;
            if (r2 >= radius2 || r2 < 1e-6f) {
                continue;
            }
            const float inv_r = 1.0f / std::sqrt(r2);
            const float a = gravity_g * s->value / r2;  // 质量 = 大球 value
            ax += dx * inv_r * a;
            ay += dy * inv_r * a;
        }
        if (ax == 0.0f && ay == 0.0f) {
            continue;
        }
        ball.vx += ax * dt;
        ball.vy += ay * dt;
        const float speed = std::sqrt(ball.vx * ball.vx + ball.vy * ball.vy);
        if (speed > max_speed) {
            ball.vx *= max_speed / speed;
            ball.vy *= max_speed / speed;
        }
    }
}

void SpawnSniperExplosion(std::vector<BallObject> &spawns,
                                const BallObject &sniper)
{
    const int count = std::max(
        1, std::min(static_cast<int>(sniper.value / g_config.sniper.explosionValue),
                    g_config.sniper.maxFragments));
    const float ball_value = sniper.value / static_cast<float>(count);
    for (int i = 0; i < count; ++i) {
        const float angle = RandFloat() * 2.0f * static_cast<float>(M_PI);  // 360°
        spawns.emplace_back(
            sniper.x,
            sniper.y,
            g_config.sniper.speed,
            angle,
            g_config.paintBalls.radius,
            sniper.old_color,
            sniper.new_color,
            ball_value,
            false,  // 不是大球
            false,  // 不是狙击
            -1,     // 无大球 id
            false,  // 未死亡
            true);  // 狙击爆炸碎片：会被大球按 value 引力吸引
    }
}

void ResolvePaintBallCollisions(std::vector<BallObject> &balls,
                                       float shield_remaining[4],
                                       const SDL_FColor *pure_colors)
{
    std::vector<char> dead(balls.size(), 0);
    std::vector<BallObject> explosion_spawns;

    auto bounce_big_balls = [](BallObject &a, BallObject &b) {
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float min_dist = a.radius + b.radius;
        const float dist2 = dx * dx + dy * dy;
        if (dist2 > 1e-9f) {
            const float dist = std::sqrt(dist2);
            const float nx = dx / dist;
            const float ny = dy / dist;
            const float overlap = (min_dist - dist) * 0.5f;
            const float mass_a = std::max(a.value, 0.001f);
            const float mass_b = std::max(b.value, 0.001f);
            const float inv_a = 1.0f / mass_a;
            const float inv_b = 1.0f / mass_b;
            const float inv_total = inv_a + inv_b;
            a.x -= nx * overlap * (inv_a / inv_total);
            a.y -= ny * overlap * (inv_a / inv_total);
            b.x += nx * overlap * (inv_b / inv_total);
            b.y += ny * overlap * (inv_b / inv_total);
        } else {
            a.x -= 0.5f;
        }
    };

    auto apply_momentum = [](BallObject &a, BallObject &b, float nx, float ny) {
        const float mass_a = std::max(a.value, 0.001f);
        const float mass_b = std::max(b.value, 0.001f);
        const float vn = (a.vx - b.vx) * nx + (a.vy - b.vy) * ny;
        if (vn > 0.0f) {
            const float impulse = -2.0f * vn / (1.0f / mass_a + 1.0f / mass_b);
            a.vx += impulse / mass_a * nx;
            a.vy += impulse / mass_a * ny;
            b.vx -= impulse / mass_b * nx;
            b.vy -= impulse / mass_b * ny;
        }
    };

    // 护盾吸收
    const float shield_radius = g_config.shield.radius;
    for (std::size_t i = 0; i < balls.size(); ++i) {
        if (dead[i] || balls[i].dying) {
            continue;
        }
        for (int color = 0; color < 4; ++color) {
            if (shield_remaining[color] <= 0.0f) {
                continue;
            }
            float center_x = 0.0f, center_y = 0.0f;
            GetColorBlockCenter(color, center_x, center_y);
            const float dx = balls[i].x - center_x;
            const float dy = balls[i].y - center_y;
            const float check_radius =
                balls[i].is_big ? shield_radius + balls[i].radius : shield_radius;
            if (dx * dx + dy * dy > check_radius * check_radius) {
                continue;
            }

            const int ball_color = FindPaintColorIndex(balls[i], pure_colors);
            if (ball_color == color) {
                continue;
            }

            if (balls[i].is_sniper) {
                SpawnSniperExplosion(explosion_spawns, balls[i]);
                dead[i] = 1;
                break;
            }

            if (balls[i].is_big) {
                if (shield_remaining[color] >= balls[i].value * g_config.combat.absorbRatio) {
                    shield_remaining[color] -= balls[i].value;
                    dead[i] = 1;
                } else {
                    const float larger = std::max(shield_remaining[color], balls[i].value);
                    const float smaller = std::min(shield_remaining[color], balls[i].value);
                    const float damage =
                        larger * g_config.combat.damageRatio;
                    if (smaller - damage <= 0.0f) {
                        if (balls[i].value <= shield_remaining[color]) {
                            shield_remaining[color] -= balls[i].value;
                            dead[i] = 1;
                        } else {
                            balls[i].value -= shield_remaining[color];
                            shield_remaining[color] = 0.0f;
                        }
                    } else {
                        shield_remaining[color] -= damage;
                        balls[i].value -= damage;
                    }
                    if (!dead[i]) {
                        const float dist = std::sqrt(dx * dx + dy * dy);
                        float nx = 0.0f, ny = -1.0f;
                        float penetration = shield_radius + balls[i].radius;
                        if (dist > 1e-9f) {
                            nx = dx / dist;
                            ny = dy / dist;
                            penetration = shield_radius + balls[i].radius - dist;
                        }
                        balls[i].x += nx * penetration;
                        balls[i].y += ny * penetration;
                        const float vn = balls[i].vx * nx + balls[i].vy * ny;
                        if (vn < 0.0f) {
                            balls[i].vx -= 2.0f * vn * nx;
                            balls[i].vy -= 2.0f * vn * ny;
                        }
                    }
                }
                break;
            }

            if (balls[i].value > shield_remaining[color]) {
                balls[i].value -= shield_remaining[color];
                shield_remaining[color] = 0.0f;
            } else {
                shield_remaining[color] -= balls[i].value;
                dead[i] = 1;
            }
            break;
        }
    }

    // 大球交互（大球数量少，直接检测）
    for (std::size_t i = 0; i < balls.size(); ++i) {
        if (dead[i] || !balls[i].is_big || balls[i].dying) {
            continue;
        }
        for (std::size_t j = 0; j < balls.size(); ++j) {
            if (i == j || dead[j] || balls[j].dying) {
                continue;
            }
            if (balls[j].is_big && j <= i) {
                continue;  // 避免大球之间重复处理
            }

            BallObject &a = balls[i];
            BallObject &b = balls[j];
            const float dx = b.x - a.x;
            const float dy = b.y - a.y;
            const float min_dist = a.radius + b.radius;
            const float dist2 = dx * dx + dy * dy;
            if (dist2 >= min_dist * min_dist) {
                continue;
            }

            const bool same_color =
                std::fabs(a.old_color.r - b.old_color.r) < 0.01f &&
                std::fabs(a.old_color.g - b.old_color.g) < 0.01f &&
                std::fabs(a.old_color.b - b.old_color.b) < 0.01f;

            if (b.is_sniper) {
                if (same_color) {
                    // 本方大球撞击本方狙击：直接吞噬，转换为自身 value
                    b.value += a.value;
                    dead[i] = 1;  // 大球进入死亡缩小动画
                } else {
                    // 异色狙击：爆炸
                    SpawnSniperExplosion(explosion_spawns, b);
                    dead[j] = 1;
                }
                continue;
            }

            // 大球 vs 大球/敌方小球：按质量交换动量并分离位置（真实物理）
            if ((b.is_big || !same_color) && dist2 > 1e-9f) {
                const float dist = std::sqrt(dist2);
                const float nx = dx / dist;
                const float ny = dy / dist;
                apply_momentum(a, b, nx, ny);
                bounce_big_balls(a, b);
            }

            if (b.is_big) {
                if (same_color) {
                    // 动量已交换，位置已分离
                } else if (a.value >= b.value * g_config.combat.absorbRatio) {
                    a.value -= b.value;
                    dead[j] = 1;
                } else if (b.value >= a.value * g_config.combat.absorbRatio) {
                    b.value -= a.value;
                    dead[i] = 1;
                    break;
                } else {
                    const float larger = std::max(a.value, b.value);
                    const float smaller = std::min(a.value, b.value);
                    const float damage = larger * g_config.combat.damageRatio;
                    if (smaller - damage <= 0.0f) {
                        if (a.value <= b.value) {
                            b.value -= a.value;
                            dead[i] = 1;
                            break;
                        } else {
                            a.value -= b.value;
                            dead[j] = 1;
                        }
                    } else {
                        a.value -= damage;
                        b.value -= damage;
                    }
                }
                continue;
            }

            // 大球 vs 小球
            if (same_color) {
                if (b.big_ball_id == a.big_ball_id) {
                    continue;  // 自己发射的拖尾不吸收
                }
                a.value += b.value;
                dead[j] = 1;
            } else {
                if (a.value < b.value) {
                    b.value -= a.value;
                    dead[i] = 1;
                    break;
                } else if (b.value < a.value) {
                    a.value -= b.value;
                    dead[j] = 1;
                } else {
                    dead[i] = 1;
                    dead[j] = 1;
                    break;
                }
            }
        }
    }

    // 小-小碰撞：空间网格
    constexpr float CELL_SIZE = 16.0f;
    const int grid_width = std::max(1, static_cast<int>(std::ceil(FRAME_SIZE / CELL_SIZE)));
    const int grid_height = grid_width;
    std::vector<int> cell_start(static_cast<std::size_t>(grid_width) * grid_height, -1);
    std::vector<int> next(balls.size(), -1);

    for (std::size_t i = 0; i < balls.size(); ++i) {
        if (dead[i] || balls[i].is_big || balls[i].dying) {
            continue;
        }
        const int cx = std::max(0, std::min(grid_width - 1,
            static_cast<int>((balls[i].x - FRAME_X) / CELL_SIZE)));
        const int cy = std::max(0, std::min(grid_height - 1,
            static_cast<int>((balls[i].y - FRAME_Y) / CELL_SIZE)));
        const std::size_t cell = static_cast<std::size_t>(cy) * grid_width + cx;
        next[i] = cell_start[cell];
        cell_start[cell] = static_cast<int>(i);
    }

    for (std::size_t i = 0; i < balls.size(); ++i) {
        if (dead[i] || balls[i].is_big || balls[i].dying) {
            continue;
        }
        const int cx = std::max(0, std::min(grid_width - 1,
            static_cast<int>((balls[i].x - FRAME_X) / CELL_SIZE)));
        const int cy = std::max(0, std::min(grid_height - 1,
            static_cast<int>((balls[i].y - FRAME_Y) / CELL_SIZE)));

        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int nx = cx + dx;
                const int ny = cy + dy;
                if (nx < 0 || nx >= grid_width || ny < 0 || ny >= grid_height) {
                    continue;
                }
                const std::size_t cell = static_cast<std::size_t>(ny) * grid_width + nx;
                for (int j = cell_start[cell]; j != -1; j = next[static_cast<std::size_t>(j)]) {
                    if (j <= static_cast<int>(i) || dead[static_cast<std::size_t>(j)] ||
                        balls[static_cast<std::size_t>(j)].is_big ||
                        balls[static_cast<std::size_t>(j)].dying) {
                        continue;
                    }

                    BallObject &a = balls[i];
                    BallObject &b = balls[static_cast<std::size_t>(j)];
                    const float dxx = b.x - a.x;
                    const float dyy = b.y - a.y;
                    const float min_dist = a.radius + b.radius;
                    const float dist2 = dxx * dxx + dyy * dyy;
                    if (dist2 >= min_dist * min_dist) {
                        continue;
                    }

                    const bool same_color =
                        std::fabs(a.old_color.r - b.old_color.r) < 0.01f &&
                        std::fabs(a.old_color.g - b.old_color.g) < 0.01f &&
                        std::fabs(a.old_color.b - b.old_color.b) < 0.01f;
                    if (same_color) {
                        continue;
                    }

                    // 小球也参与 value 物理
                    if (dist2 > 1e-9f) {
                        const float dist = std::sqrt(dist2);
                        const float nx = dxx / dist;
                        const float ny = dyy / dist;
                        apply_momentum(a, b, nx, ny);
                        bounce_big_balls(a, b);
                    } else {
                        a.x -= 0.5f;
                    }

                    if (a.value >= b.value * g_config.combat.absorbRatio) {
                        a.value -= b.value;
                        dead[j] = 1;
                    } else if (b.value >= a.value * g_config.combat.absorbRatio) {
                        b.value -= a.value;
                        dead[i] = 1;
                        break;
                    } else {
                        const float larger = std::max(a.value, b.value);
                        const float smaller = std::min(a.value, b.value);
                        const float damage = larger * g_config.combat.damageRatio;
                        if (smaller - damage <= 0.0f) {
                            if (a.value <= b.value) {
                                b.value -= a.value;
                                dead[i] = 1;
                                break;
                            } else {
                                a.value -= b.value;
                                dead[j] = 1;
                            }
                        } else {
                            a.value -= damage;
                            b.value -= damage;
                        }
                    }
                }
                if (dead[i]) {
                    break;
                }
            }
            if (dead[i]) {
                break;
            }
        }
    }

    for (std::size_t i = 0; i < balls.size(); ++i) {
        if (dead[i] && balls[i].is_big) {
            balls[i].dying = true;  // 大球死亡进入缩小动画
        }
    }

    std::size_t write = 0;
    for (std::size_t i = 0; i < balls.size(); ++i) {
        if (!dead[i] || balls[i].dying) {
            balls[write++] = balls[i];
        }
    }
    while (balls.size() > write) {
        balls.pop_back();
    }
    for (BallObject &spawn : explosion_spawns) {
        balls.push_back(spawn);
    }

}
