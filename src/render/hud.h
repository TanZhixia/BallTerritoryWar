#ifndef BTW_RENDER_HUD_H
#define BTW_RENDER_HUD_H

#include <vector>

#include <SDL3/SDL.h>

// 右侧悬浮 HUD 面板：四队（色点 + 队名 + 领土占比 + 护盾/弹药），画在显示画布上。
// 文案为位图字体中文（render/font_atlas.h），数字由 FormatValue 生成。
// color_reviving：该队正处于复活流程中（基地已失守：等待期或物理球已飞向炮塔），
// 第一行显示「复活中」，彻底出局显示「已灭」（终端启动器同样显示「复活中」/等待倒计时）
void DrawHUD(std::vector<Uint8> &canvas, int canvas_width, int canvas_height,
             const float territory[4], const float shield_remaining[4],
             const float machine_gun_ammo[4], const bool color_alive[4],
             const bool color_reviving[4]);

#endif  // BTW_RENDER_HUD_H
