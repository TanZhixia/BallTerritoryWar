#ifndef BTW_GAME_SCENE_H
#define BTW_GAME_SCENE_H

#include <vector>

#include <SDL3/SDL.h>

#include "core/entities.h"

// ==================== 静态场景与布局几何 ====================

// 机械区中央水平挡板与其缺口（显示层的水面等需要与 BuildStaticScene 共用同一组几何）
constexpr float LIFT_WALL_Y = 400.0f;       // 挡板中心线 y
constexpr float LIFT_WALL_HALF = 10.0f;     // 挡板半高（上下各 10px，即 y=390~410）
constexpr float LIFT_GAP_CENTER_X = 300.0f; // 缺口中心 x
constexpr float LIFT_GAP_HALF = 75.0f;      // 缺口半宽（x=225~375）

// 四角基地中心（color_index: 0 左上红 / 1 右上绿 / 2 左下蓝 / 3 右下黄）
void GetColorBlockCenter(int color_index, float &x, float &y);
// 机械区的固定灰色阻挡圆点：乘法带以上 3 排 + 乘法带以下 4 排，均为交错排列，
// 半径 = 物理球半径的一半
void GetBlockingCircles(std::vector<StaticCircle> &circles);

// 一次性绘制静态场景（左侧背景/边框/乘法带/武器栏/挡板/四角基地）
void BuildStaticScene(std::vector<Uint8> &canvas, std::vector<StaticCircle> &blocking_circles);
// 底部 5 格武器栏（每帧重画在显示画布上）
void DrawBottomBar(std::vector<Uint8> &canvas, int canvas_width, int canvas_height);
// 左侧机械区背景：black_wool.png 平铺
void FillLeftBackground(std::vector<Uint8> &canvas);

#endif  // BTW_GAME_SCENE_H
