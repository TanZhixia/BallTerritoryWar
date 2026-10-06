#include "render/render.h"

#include "core/config.h"
#include "core/constants.h"
#include "core/math_utils.h"
#include "core/palette.h"
#include "game/physics.h"
#include "game/scene.h"
#include "game/simulation.h"
#include "render/canvas.h"
#include "render/hud.h"
#include "render/particles.h"

#include <cmath>
#include <cstring>
#include <string>

// 显示层渲染：把持久画布拷贝到 display_canvas，再叠加所有特效/UI。
// 只写 display_canvas，不影响 canvas（玩法画布）。
void RenderGame(GameState &state, const std::vector<Uint8> &canvas,
                std::vector<Uint8> &display_canvas)
{
    const std::size_t canvas_size = display_canvas.size();
    SDL_memcpy(display_canvas.data(), canvas.data(), canvas_size);
    for (const PendingPaint &paint : state.new_paints) {
        PaintCircle(display_canvas, WINDOW_WIDTH, WINDOW_HEIGHT,
                    paint.x, paint.y, paint.radius, paint.color);
    }

    // 小球拖尾（仅显示层）：沿速度反方向画渐隐圆点，越远越小越淡。
    // 大球自带尾流小球、狙击有独立光效，均跳过。
    for (const BallObject &ball : state.balls) {
        if (ball.is_big || ball.is_sniper || ball.dying) {
            continue;
        }
        const float speed = std::sqrt(ball.vx * ball.vx + ball.vy * ball.vy);
        if (speed < 1.0f) {
            continue;
        }
        const float ux = ball.vx / speed;
        const float uy = ball.vy / speed;
        constexpr int TRAIL_DOTS = 4;
        constexpr float TRAIL_SPACING = 2.5f;  // 相邻拖尾点间距（像素）
        for (int k = 1; k <= TRAIL_DOTS; ++k) {
            const float fade = 1.0f - static_cast<float>(k) / (TRAIL_DOTS + 1);
            PaintCircleAlpha(display_canvas, WINDOW_WIDTH, WINDOW_HEIGHT,
                             ball.x - ux * TRAIL_SPACING * k,
                             ball.y - uy * TRAIL_SPACING * k,
                             std::max(1.0f, ball.radius * (0.5f + 0.5f * fade)),
                             ball.new_color, 0.45f * fade);
        }
    }

    // 底部栏每帧重画
    DrawBottomBar(display_canvas, WINDOW_WIDTH, WINDOW_HEIGHT);

    // 狙击特效：光点拖尾 + 飞行抖动（仅显示层）+ 火花粒子尾迹
    {
        if (g_config.sniper.dotTrailLife > 0.0f) {
            for (const BallObject &ball : state.balls) {
                if (!ball.is_sniper || ball.dying) {
                    continue;
                }
                SpawnBallTrail(state.trail_dots, ball.x, ball.y, ball.new_color,
                               g_config.sniper.dotTrailLife);
            }
            UpdateTrailDots(state.trail_dots, 1.0f / 60.0f);
            DrawTrailDots(display_canvas, WINDOW_WIDTH, WINDOW_HEIGHT, state.trail_dots);
        }

        const float jitter = g_config.sniper.jitter;
        for (const BallObject &ball : state.balls) {
            if (!ball.is_sniper || ball.dying) {
                continue;
            }
            const float jx = ball.x + (RandFloat() * 2.0f - 1.0f) * jitter;
            const float jy = ball.y + (RandFloat() * 2.0f - 1.0f) * jitter;
            // 暗色光晕 + 本体（抖动位置）
            SDL_FColor glow = ball.new_color;
            glow.r *= 0.55f;
            glow.g *= 0.55f;
            glow.b *= 0.55f;
            PaintCircle(display_canvas, WINDOW_WIDTH, WINDOW_HEIGHT,
                        jx, jy, ball.radius + 3.0f, glow);
            PaintCircle(display_canvas, WINDOW_WIDTH, WINDOW_HEIGHT,
                        jx, jy, ball.radius, ball.new_color);
            // 火花粒子
            SpawnSniperParticles(state.particles, ball.x, ball.y, ball.new_color,
                                 g_config.sniper.particlesPerFrame);
        }
        UpdateParticles(state.particles, 1.0f / 60.0f);
        DrawParticles(display_canvas, WINDOW_WIDTH, WINDOW_HEIGHT, state.particles);
    }

    // 冲击波（乘法带/武器格触发，左机械区）：从落点扩散的圆环，仅显示层
    UpdateShockwaves(state.shockwaves, 1.0f / 60.0f);
    DrawShockwaves(display_canvas, WINDOW_WIDTH, WINDOW_HEIGHT, state.shockwaves);

    // 领土新增闪光：翻色像素被点亮，逐帧衰减淡出（仅显示层）
    UpdateAndDrawTerritoryFlash(display_canvas, state.territory_flash);

    // 现代化 HUD：顶部半透明面板（领土进度条 + 护盾/弹药 + 复活中提示）。
    // 复活提示覆盖整个复活流程（等待期 + 飞行期），与遥测/启动器同一个判定口径，
    // 否则基地刚失守的 60 秒等待期内 HUD 会误显示成“已灭”
    bool hud_reviving[4] = {false, false, false, false};
    for (int color = 0; color < 4; ++color) {
        hud_reviving[color] = IsRevivalPending(state, color);
    }
    DrawHUD(display_canvas, WINDOW_WIDTH, WINDOW_HEIGHT, state.hud_territory,
            state.shield_remaining, state.machine_gun_ammo, state.color_alive,
            hud_reviving);

    // 大球显示 value（黑字）
    const SDL_FColor big_text_color = SDL_FColor{0.0f, 0.0f, 0.0f, 1.0f};
    for (const BallObject &ball : state.balls) {
        if (!ball.is_big) {
            continue;
        }
        if (ball.dying) {
            PaintCircle(display_canvas, WINDOW_WIDTH, WINDOW_HEIGHT,
                        ball.x, ball.y, ball.radius, ball.new_color);
        }
        const std::string big_value = FormatValue(ball.value);
        const int big_text_scale = ball.radius >= 60.0f ? 3 : 2;
        const int big_text_width =
            static_cast<int>(big_value.size()) * 6 * big_text_scale;
        DrawText(display_canvas, WINDOW_WIDTH, WINDOW_HEIGHT,
                 static_cast<int>(ball.x) - big_text_width / 2,
                 static_cast<int>(ball.y) - (7 * big_text_scale) / 2,
                 big_value.c_str(), big_text_color, big_text_scale);
    }
    // 气泡拖尾画在物理球主体之下，被球体盖住一部分更自然
    UpdateBubbles(state.bubbles, 1.0f / 60.0f);
    DrawBubbles(display_canvas, WINDOW_WIDTH, WINDOW_HEIGHT, state.bubbles);
    DrawPhysicsBalls(display_canvas, WINDOW_WIDTH, WINDOW_HEIGHT, state.physics_balls);

    // 护盾：空心圆，中心是发射点，半径由 shield.radius 配置（默认 80px），使用新颜色
    const SDL_FColor ammo_text_color = SDL_FColor{1.0f, 1.0f, 1.0f, 1.0f};
    for (int color = 0; color < 4; ++color) {
        if (state.shield_remaining[color] <= 0.0f) {
            continue;
        }
        float center_x = 0.0f, center_y = 0.0f;
        GetColorBlockCenter(color, center_x, center_y);
        DrawHollowCircle(display_canvas, WINDOW_WIDTH, WINDOW_HEIGHT,
                         center_x, center_y, g_config.shield.radius,
                         NEW_COLORS[color], 4.0f);

        const std::string shield_text = FormatValue(state.shield_remaining[color]);
        const int shield_text_width = static_cast<int>(shield_text.size()) * 12;
        const int shield_text_y =
            (color == 0 || color == 1)
                ? static_cast<int>(center_y) + static_cast<int>(g_config.shield.radius) + 10
                : static_cast<int>(center_y) - static_cast<int>(g_config.shield.radius) - 18;
        DrawText(display_canvas, WINDOW_WIDTH, WINDOW_HEIGHT,
                 static_cast<int>(center_x) - shield_text_width / 2,
                 shield_text_y,
                 shield_text.c_str(), ammo_text_color, 2);
    }

    // 炮塔：基地中心一个圆 + 随发射方向旋转的长方形炮管
    for (int color = 0; color < 4; ++color) {
        if (!state.color_alive[color]) {
            continue;
        }
        float center_x = 0.0f, center_y = 0.0f;
        GetColorBlockCenter(color, center_x, center_y);
        PaintCircle(display_canvas, WINDOW_WIDTH, WINDOW_HEIGHT,
                    center_x, center_y, 14.0f, NEW_COLORS[color]);
        const float barrel_center_x =
            center_x + std::cos(state.machine_gun_angle[color]) * 19.0f;
        const float barrel_center_y =
            center_y + std::sin(state.machine_gun_angle[color]) * 19.0f;
        DrawRotatedRect(display_canvas, WINDOW_WIDTH, WINDOW_HEIGHT,
                        barrel_center_x, barrel_center_y, 38.0f, 8.0f,
                        state.machine_gun_angle[color], NEW_COLORS[color]);
    }

    // 在每个颜色发射位置显示机枪剩余量（完整数字，不用 k），置于炮塔上层
    for (int color = 0; color < 4; ++color) {
        float center_x = 0.0f, center_y = 0.0f;
        GetColorBlockCenter(color, center_x, center_y);
        char ammo_text[32];
        std::snprintf(ammo_text, sizeof(ammo_text), "%lld",
                      static_cast<long long>(state.machine_gun_ammo[color]));
        const int text_width = static_cast<int>(std::strlen(ammo_text)) * 12;
        DrawText(display_canvas, WINDOW_WIDTH, WINDOW_HEIGHT,
                 static_cast<int>(center_x) - text_width / 2,
                 static_cast<int>(center_y) - 8,
                 ammo_text, ammo_text_color, 2);
    }

    // 复活飞行中的物理球画在所有 UI 之上：它要横穿战场飞到炮塔，动画必须醒目
    DrawRevivingPhysicsBalls(display_canvas, WINDOW_WIDTH, WINDOW_HEIGHT,
                             state.physics_balls);
}
