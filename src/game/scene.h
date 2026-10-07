#ifndef BTW_GAME_SCENE_H
#define BTW_GAME_SCENE_H

#include <vector>

#include <SDL3/SDL.h>

#include "core/entities.h"

// ==================== 静态场景与布局几何 ====================
//
// 下面这组常量是「机械区布局」的**唯一真相**：绘制（game/scene.cpp）、碰撞
// （game/physics.cpp）、水面（render/water.cpp）都从这里取值。以前同一组数字在
// 场景与物理里各写一份字面量，改挡板/倍率带只改一处就会让画面和碰撞错位。

// ---- 中央水平挡板与其缺口（倍率带就画在这条挡板上）----
constexpr float LIFT_WALL_Y = 400.0f;        // 挡板中心线 y
constexpr float LIFT_WALL_HALF = 10.0f;      // 半高：上下沿 = 390 / 410
constexpr float LIFT_GAP_CENTER_X = 300.0f;  // 缺口中心 x
constexpr float LIFT_GAP_HALF = 75.0f;       // 缺口半宽：x 225~375

// ---- 由挡板派生的几何（不要另写字面量）----
constexpr float BAND_TOP_Y = LIFT_WALL_Y - LIFT_WALL_HALF;       // 倍率带上沿 390
constexpr float BAND_HEIGHT = LIFT_WALL_HALF * 2.0f;             // 倍率带高 20
constexpr float BAND_LEFT_EDGE = LIFT_GAP_CENTER_X - LIFT_GAP_HALF;   // 缺口左 225
constexpr float BAND_RIGHT_EDGE = LIFT_GAP_CENTER_X + LIFT_GAP_HALF;  // 缺口右 375
constexpr float LOWER_BAFFLE_Y = LIFT_WALL_Y + LIFT_WALL_HALF;   // 下方横挡板顶边 410
constexpr float LOWER_BAFFLE_HEIGHT = 20.0f;                     // 下方横挡板高
// 水柱底部：与下方横挡板下沿齐平（水面画到这儿为止）
constexpr float LIFT_WATER_BOTTOM_Y = LOWER_BAFFLE_Y + LOWER_BAFFLE_HEIGHT;  // 430

// ---- 机械区外框 ----
constexpr float MECH_WIDTH = 600.0f;          // 机械区宽度（含边框）
constexpr float MECH_BORDER = 20.0f;          // 四周边框宽度
constexpr float MECH_BOTTOM_BAR_Y = 980.0f;   // 底部条（武器格）y
constexpr float MECH_BOTTOM_BAR_H = 20.0f;    // 底部条高度

// ---- 倍率带分区：左 ×8/×4/×2 + 中间缺口 + 右 ×2/×4/×8 ----
struct BandZone
{
    float x;           // 分区左边界
    float multiplier;  // 该分区的倍率
};
constexpr int BAND_ZONE_COUNT = 6;
constexpr float BAND_ZONE_WIDTH = 75.0f;
constexpr BandZone BAND_ZONES[BAND_ZONE_COUNT] = {
    {0.0f, 8.0f}, {75.0f, 4.0f}, {150.0f, 2.0f},
    {375.0f, 2.0f}, {450.0f, 4.0f}, {525.0f, 8.0f},
};
// 分区之间的分隔线 x（画 2px 深色线；缺口两侧 225 / 375 也在内）
constexpr float BAND_SEPARATOR_X[6] = {75.0f, 150.0f, 225.0f, 375.0f, 450.0f, 525.0f};
// 带上方升力区的上沿（比挡板顶 310 更高一点，是独立的手感参数）
constexpr float BAND_LIFT_TOP_Y = 300.0f;

// ---- 倍率带上方 4 块竖向挡板（10px 宽，底边正好压在带上沿）----
constexpr float BAFFLE_WIDTH = 10.0f;
constexpr float BAFFLE_TOP_Y = 310.0f;
constexpr float BAFFLE_HEIGHT = 100.0f;  // 310 + 100 = 410，与下方挡板连着
constexpr float BAFFLE_X[4] = {70.0f, 145.0f, 445.0f, 520.0f};  // 分区边界 − 半宽

// ---- 底部武器栏：5 格 × 120px，从左到右 ----
constexpr int WEAPON_SLOT_COUNT = 5;
constexpr float WEAPON_SLOT_WIDTH = 120.0f;

// 该点是否在水里（缺口 → 下方挡板之间那一柱）。
// 物理球的浮力只在这里生效；水面在 y=390 附近，与 DrawLiftWater 画的那柱水一致。
inline bool InLiftWater(float x, float y)
{
    return x >= BAND_LEFT_EDGE && x <= BAND_RIGHT_EDGE &&
           y >= BAND_TOP_Y && y <= LIFT_WATER_BOTTOM_Y;
}

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
