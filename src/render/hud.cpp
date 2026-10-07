// ==================== HUD 面板（右侧悬浮状态板） ====================
// 战场右上角的半透明圆角面板：四队状态 —— 色点 + 队名 + 领土占比 + 护盾/弹药，
// 基地被占领（color_alive=false）后第一行显示「已灭」。
// 只画在显示画布上，不参与领土判定；面板避开四角基地（右边缘 1400 < 右上基地 1500-100）。
// 文案走位图字体（render/font_atlas.h，16px 中文），数字由 FormatValue 生成。

#include "render/hud.h"

#include "core/math_utils.h"
#include "core/palette.h"
#include "render/canvas.h"
#include "render/font_atlas.h"

#include <cstdio>

namespace {

// 面板几何：4 队 × 125px 栏宽，3 行 × 18px 行高
constexpr int PANEL_X = 860;
constexpr int PANEL_Y = 10;
constexpr int PANEL_W = 540;
constexpr int PANEL_H = 62;
constexpr float PANEL_RADIUS = 10.0f;
constexpr int GAP_X = 10;
constexpr int BLOCK_W = (PANEL_W - 2 * GAP_X - 3 * 6) / 4;  // 4 队
constexpr int ROW_NAME_Y = PANEL_Y + 3;                      // 队名 + 领土占比
const int ROW_SHIELD_Y = ROW_NAME_Y + FontLineHeight();  // 护盾
const int ROW_AMMO_Y = ROW_SHIELD_Y + FontLineHeight();  // 弹药

const char *const kTeamNames[4] = {"红队", "绿队", "蓝队", "黄队"};

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

        // 色点：存活用队伍色，已出局用灰色
        PaintCircle(canvas, canvas_width, canvas_height, x + 4, ROW_NAME_Y + 8, 3.5f,
                    alive ? team : gray);

        // 第一行：队名 + 领土占比 / 已灭
        char line[64];
        if (alive) {
            std::snprintf(line, sizeof(line), "%s %.1f%%", kTeamNames[color],
                          territory[color] * 100.0f);
        } else {
            std::snprintf(line, sizeof(line), "%s 已灭", kTeamNames[color]);
        }
        DrawTextFont(canvas, canvas_width, canvas_height, x + 12, ROW_NAME_Y, line,
                     alive ? white : dim);

        // 第二行：护盾
        std::snprintf(line, sizeof(line), "护盾 %s",
                      alive ? FormatValue(shield_remaining[color]).c_str() : "--");
        DrawTextFont(canvas, canvas_width, canvas_height, x + 12, ROW_SHIELD_Y, line, dim);

        // 第三行：弹药
        std::snprintf(line, sizeof(line), "弹药 %s",
                      alive ? FormatValue(machine_gun_ammo[color]).c_str() : "--");
        DrawTextFont(canvas, canvas_width, canvas_height, x + 12, ROW_AMMO_Y, line, dim);
    }
}
