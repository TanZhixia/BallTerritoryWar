#ifndef BTW_RENDER_HUD_H
#define BTW_RENDER_HUD_H

#include <vector>

#include <SDL3/SDL.h>

// 顶部半透明圆角 HUD 面板：四队领土进度条 + 护盾/弹药（画在显示画布）
// color_reviving：该队正处于复活流程中（基地已失守：等待期或物理球已飞向炮塔），
// 第一行显示 REVIVING（游戏内像素字体只有 ASCII；终端启动器显示“复活中”/等待倒计时）
void DrawHUD(std::vector<Uint8> &canvas, int canvas_width, int canvas_height,
             const float territory[4], const float shield_remaining[4],
             const float machine_gun_ammo[4], const bool color_alive[4],
             const bool color_reviving[4]);

#endif  // BTW_RENDER_HUD_H
