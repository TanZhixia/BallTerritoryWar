#ifndef BTW_CORE_PALETTE_H
#define BTW_CORE_PALETTE_H

#include <SDL3/SDL.h>

class BallObject;
struct PhysicsBall;

// 四角配色：每个角一种队伍颜色
struct FramePalette
{
    SDL_Color top_left;
    SDL_Color top_right;
    SDL_Color bottom_left;
    SDL_Color bottom_right;
};

// 新配色：纯色的柔和版本（新红/新绿/新蓝/新黄，各通道单独调过）——用于显示层
extern const FramePalette NEW_FRAME_PALETTE;
// 纯色版：对应新颜色的“旧颜色/轨迹颜色”——用于领土判定
extern const FramePalette PURE_FRAME_PALETTE;

// PURE_COLORS：领土判定用的纯色（旧色、轨迹色）；NEW_COLORS：显示层柔和新色。
extern const SDL_FColor PURE_COLORS[4];
extern const SDL_FColor NEW_COLORS[4];

// 按颜色反查队伍下标（颜色存的是浮点，用容差比较）
int FindPhysicsColorIndex(const PhysicsBall &ball, const SDL_FColor *colors);
int FindPaintColorIndex(const BallObject &ball, const SDL_FColor *colors);

#endif  // BTW_CORE_PALETTE_H
