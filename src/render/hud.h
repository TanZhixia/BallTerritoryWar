#ifndef BTW_RENDER_HUD_H
#define BTW_RENDER_HUD_H

#include <vector>

#include <SDL3/SDL.h>

// 右侧悬浮 HUD 面板：四队（色点 + 队名 + 领土占比 + 护盾/弹药），画在显示画布上。
// 文案为位图字体中文（render/font_atlas.h），数字由 FormatValue 生成。
// 存活显示队名与领土占比，基地被占（color_alive=false）后显示「已灭」
void DrawHUD(std::vector<Uint8> &canvas, int canvas_width, int canvas_height,
             const float territory[4], const float shield_remaining[4],
             const float machine_gun_ammo[4], const bool color_alive[4]);

#endif  // BTW_RENDER_HUD_H
