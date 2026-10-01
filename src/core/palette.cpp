#include "core/palette.h"

#include "core/entities.h"
#include "core/math_utils.h"

#include <cmath>

// 新配色：纯色的柔和版本（新红/新绿/新蓝/新黄，各通道单独调过）
const FramePalette NEW_FRAME_PALETTE = {
    {255, 150, 150, 255},  // 新红
    {100, 255, 100, 255},  // 新绿
    {180, 210, 255, 255},  // 新蓝
    {255, 240, 140, 255},  // 新黄
};

// 纯色版：对应新颜色的“旧颜色/轨迹颜色”
const FramePalette PURE_FRAME_PALETTE = {
    {255, 0, 0, 255},      // 纯红
    {40, 200, 40, 255},    // 纯绿
    {30, 130, 255, 255},   // 纯蓝
    {255, 200, 0, 255},    // 纯黄（旧色）
};

// 4 队纯色（领土判定）与显示层新色，供模拟/渲染共享
const SDL_FColor PURE_COLORS[4] = {
    ToFColor(PURE_FRAME_PALETTE.top_left),
    ToFColor(PURE_FRAME_PALETTE.top_right),
    ToFColor(PURE_FRAME_PALETTE.bottom_left),
    ToFColor(PURE_FRAME_PALETTE.bottom_right),
};
const SDL_FColor NEW_COLORS[4] = {
    ToFColor(NEW_FRAME_PALETTE.top_left),
    ToFColor(NEW_FRAME_PALETTE.top_right),
    ToFColor(NEW_FRAME_PALETTE.bottom_left),
    ToFColor(NEW_FRAME_PALETTE.bottom_right),
};

int FindPhysicsColorIndex(const PhysicsBall &ball, const SDL_FColor *colors)
{
    for (int i = 0; i < 4; ++i) {
        if (std::fabs(ball.color.r - colors[i].r) < 0.01f &&
            std::fabs(ball.color.g - colors[i].g) < 0.01f &&
            std::fabs(ball.color.b - colors[i].b) < 0.01f) {
            return i;
        }
    }
    return 0;
}

int FindPaintColorIndex(const BallObject &ball, const SDL_FColor *colors)
{
    for (int i = 0; i < 4; ++i) {
        if (std::fabs(ball.old_color.r - colors[i].r) < 0.01f &&
            std::fabs(ball.old_color.g - colors[i].g) < 0.01f &&
            std::fabs(ball.old_color.b - colors[i].b) < 0.01f) {
            return i;
        }
    }
    return 0;
}
