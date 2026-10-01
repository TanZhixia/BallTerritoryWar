#ifndef BTW_RENDER_HUD_H
#define BTW_RENDER_HUD_H

#include <vector>

#include <SDL3/SDL.h>

// 顶部半透明圆角 HUD 面板：四队领土进度条 + 护盾/弹药（画在显示画布）
void DrawHUD(std::vector<Uint8> &canvas, int canvas_width, int canvas_height,
             const float territory[4], const float shield_remaining[4],
             const float machine_gun_ammo[4], const bool color_alive[4]);

#endif  // BTW_RENDER_HUD_H
