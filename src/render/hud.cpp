// ==================== HUD 面板（像素字体风格） ====================
// 顶部半透明圆角面板：四队状态（色点 + 领土百分比 + 护盾/弹药）。
// 只画在显示画布上，不参与领土判定。面板置于战场区顶部居中，避开四角基地。
// 注意：5×7 像素字体仅含 ASCII，故全部用字母/数字标识。

#include "render/hud.h"

#include "core/math_utils.h"
#include "core/palette.h"
#include "render/canvas.h"

#include <cstdio>

namespace {

// 面板几何
constexpr int PANEL_X = 860;
constexpr int PANEL_Y = 10;
constexpr int PANEL_W = 480;
constexpr int PANEL_H = 58;
constexpr float PANEL_RADIUS = 10.0f;
constexpr int GAP_X = 10;
constexpr int BLOCK_W = (PANEL_W - 2 * GAP_X - 3 * 6) / 4;  // 4 队

}  // namespace

void DrawHUD(std::vector<Uint8> &canvas, int canvas_width, int canvas_height,
             const float territory[4], const float shield_remaining[4],
             const float machine_gun_ammo[4], const bool color_alive[4])
{
    // 面板底 + 细边框
    const SDL_FColor panel_bg = {0.04f, 0.055f, 0.11f, 1.0f};
    const SDL_FColor border = {0.45f, 0.55f, 0.9f, 1.0f};
    FillRoundRectAlpha(canvas, canvas_width, canvas_height,
                       PANEL_X, PANEL_Y, PANEL_W, PANEL_H, PANEL_RADIUS, panel_bg, 0.72f);
    const int bx = PANEL_X, by = PANEL_Y, bw = PANEL_W, bh = PANEL_H;
    FillRectAlpha(canvas, canvas_width, canvas_height, bx, by, bw, 1, border, 0.18f);
    FillRectAlpha(canvas, canvas_width, canvas_height, bx, by + bh - 1, bw, 1, border, 0.18f);
    FillRectAlpha(canvas, canvas_width, canvas_height, bx, by, 1, bh, border, 0.18f);
    FillRectAlpha(canvas, canvas_width, canvas_height, bx + bw - 1, by, 1, bh, border, 0.18f);

    const SDL_FColor white = {0.92f, 0.95f, 1.0f, 1.0f};
    const SDL_FColor dim = {0.6f, 0.65f, 0.8f, 1.0f};
    const SDL_FColor gray = {0.35f, 0.38f, 0.48f, 1.0f};

    for (int color = 0; color < 4; ++color) {
        const int x = PANEL_X + GAP_X + color * (BLOCK_W + 6);
        const SDL_FColor team = ToFColor(
            color == 0 ? NEW_FRAME_PALETTE.top_left :
            color == 1 ? NEW_FRAME_PALETTE.top_right :
            color == 2 ? NEW_FRAME_PALETTE.bottom_left :
                         NEW_FRAME_PALETTE.bottom_right);
        const bool alive = color_alive[color];

        // 色点
        PaintCircle(canvas, canvas_width, canvas_height, x + 4, PANEL_Y + 8, 3.5f,
                    alive ? team : gray);

        // 第一行（2 倍字）：领土百分比
        char line1[16];
        if (alive) {
            std::snprintf(line1, sizeof(line1), "%.1f", territory[color] * 100.0f);
        } else {
            std::snprintf(line1, sizeof(line1), "--");
        }
        DrawText(canvas, canvas_width, canvas_height,
                 x + 12, PANEL_Y + 2, line1, alive ? white : dim, 2);

        // 第二行（2 倍字）：护盾
        char line2[24];
        std::snprintf(line2, sizeof(line2), "SH %s",
                      alive ? FormatValue(shield_remaining[color]).c_str() : "--");
        DrawText(canvas, canvas_width, canvas_height, x + 12, PANEL_Y + 20,
                 line2, dim, 2);

        // 第三行（2 倍字）：弹药
        std::snprintf(line2, sizeof(line2), "AM %s",
                      alive ? FormatValue(machine_gun_ammo[color]).c_str() : "--");
        DrawText(canvas, canvas_width, canvas_height, x + 12, PANEL_Y + 38,
                 line2, dim, 2);
    }
}
