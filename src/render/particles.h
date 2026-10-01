#ifndef BTW_RENDER_PARTICLES_H
#define BTW_RENDER_PARTICLES_H

#include <SDL3/SDL.h>

#include <vector>

#include "core/effects.h"

// ==================== 视觉特效绘制（仅显示层） ====================
// 只写 display_canvas，不影响持久领土画布、碰撞与计费。

// 在指定位置生成狙击飞行火花（含容量保护）
void SpawnSniperParticles(std::vector<Particle> &particles, float x, float y,
                          const SDL_FColor &color, int count);
// 推进所有粒子（位移 + 阻力 + 生命衰减 + 回收）
void UpdateParticles(std::vector<Particle> &particles, float dt);
// 把粒子画到显示画布（按生命淡出到背景色）
void DrawParticles(std::vector<Uint8> &canvas, int canvas_width, int canvas_height,
                   const std::vector<Particle> &particles);

// 光点拖尾（狙击用）
void SpawnBallTrail(std::vector<TrailDot> &dots, float x, float y,
                    const SDL_FColor &color, float life);
void UpdateTrailDots(std::vector<TrailDot> &dots, float dt);
void DrawTrailDots(std::vector<Uint8> &canvas, int canvas_width, int canvas_height,
                   const std::vector<TrailDot> &dots);

// 在落点生成一个扩散冲击波（含容量保护）
void SpawnShockwave(std::vector<Shockwave> &waves, float x, float y,
                    float max_radius, float duration, float thickness,
                    const SDL_FColor &color);
// 推进所有冲击波（半径扩散 + 生命衰减 + 回收）
void UpdateShockwaves(std::vector<Shockwave> &waves, float dt);
// 把冲击波画到显示画布（圆环，按生命淡出到背景色）
void DrawShockwaves(std::vector<Uint8> &canvas, int canvas_width, int canvas_height,
                    const std::vector<Shockwave> &waves);

// 在指定位置生成一个上浮气泡（物理球拖尾用，含容量保护）
void SpawnPhysicsBubble(std::vector<Bubble> &bubbles, float x, float y,
                        const SDL_FColor &color);
// 推进所有气泡（上浮 + 放大 + 生命衰减 + 回收）
void UpdateBubbles(std::vector<Bubble> &bubbles, float dt);
// 把气泡画到显示画布（空心圆，随生命放大并按生命淡出到背景色）
void DrawBubbles(std::vector<Uint8> &canvas, int canvas_width, int canvas_height,
                 const std::vector<Bubble> &bubbles);

// ==================== 领土新增闪光（仅显示层） ====================
// 逐像素闪光：涂画把像素从“非本队色”变为“本队色”的那一刻点亮该像素本身，
// 每帧寿命递减，渲染时按剩余寿命 alpha-lerp 向白色提亮后淡出，回到领土原色。
// 只作用于显示画布，不影响玩法/计费。涂画侧标记见 canvas.h 的
// PaintCircleFlash；渲染循环每帧调用一次下面的渲染+衰减：
void UpdateAndDrawTerritoryFlash(std::vector<Uint8> &display_canvas,
                                 std::vector<Uint8> &territory_flash);

#endif  // BTW_RENDER_PARTICLES_H
