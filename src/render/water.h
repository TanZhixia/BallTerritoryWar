#ifndef BTW_RENDER_WATER_H
#define BTW_RENDER_WATER_H

#include <SDL3/SDL.h>

#include <cstdint>
#include <vector>

// ==================== 机械区中央缺口的「水」（仅显示层） ====================
// 升力缺口里填一层水：多组行波叠加出涟漪，按 Schlick 形状算菲涅耳（波面越斜、
// 越掠射，反射越强），并通过水平位移采样背景制造折射；画在物理球**之后**，
// 于是穿过缺口的球看起来是隔着水看到的。
// 只写显示画布，不参与碰撞、涂画与领土判定。
void DrawLiftWater(std::vector<Uint8> &canvas, int canvas_width, int canvas_height,
                   uint64_t frame_count);

#endif  // BTW_RENDER_WATER_H
