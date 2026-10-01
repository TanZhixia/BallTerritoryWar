#ifndef BTW_CORE_EFFECTS_H
#define BTW_CORE_EFFECTS_H

#include <SDL3/SDL.h>

// ==================== 视觉特效数据（仅显示层，不参与玩法/涂画） ====================
// 这些结构只描述状态，绘制实现见 render/particles.h。

// 狙击飞行火花
struct Particle
{
    float x, y;
    float vx, vy;
    float life;     // 剩余生命（秒）
    float max_life;
    float radius;
    SDL_FColor color;
};

// 光点拖尾（狙击用）：每帧在当前位置留下快速淡出的光点
struct TrailDot
{
    float x, y;
    float life;
    float max_life;
    SDL_FColor color;
};

// 冲击波（物理球触发乘法带/武器格时从落点向外扩散的圆环）
struct Shockwave
{
    float x, y;
    float radius;      // 当前半径
    float max_radius;  // 扩散最大半径
    float life;        // 剩余生命（秒）
    float max_life;
    float thickness;   // 环宽度（随扩散变细）
    SDL_FColor color;
};

// 气泡拖尾（物理球在左侧机械区移动时）
struct Bubble
{
    float x, y;
    float vx, vy;
    float life;      // 剩余生命（秒）
    float max_life;
    float radius;    // 初始半径（随生命增长而放大）
    SDL_FColor color;
};

#endif  // BTW_CORE_EFFECTS_H
