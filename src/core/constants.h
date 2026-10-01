#ifndef BTW_CORE_CONSTANTS_H
#define BTW_CORE_CONSTANTS_H

#include <SDL3/SDL.h>

// ==================== 全局几何与尺寸常量 ====================
// 画布 1600×1000：右半 1000×1000 为领土战场（FRAME），左半 600×1000 为物理机械区。

constexpr int WINDOW_WIDTH = 1600;
constexpr int WINDOW_HEIGHT = 1000;
constexpr Uint64 FRAME_MS = 1000 / 60;

constexpr float FRAME_X = 600.0f;
constexpr float FRAME_Y = 0.0f;
constexpr float FRAME_SIZE = 1000.0f;

// 领土新增闪光缓冲：只覆盖战场区域（FRAME_SIZE × FRAME_SIZE），
// 每像素 1 字节 = 剩余寿命帧数，0 = 无闪光
constexpr int TERRITORY_FLASH_SIZE = 1000;
constexpr int TERRITORY_FLASH_FRAMES = 18;  // 闪光寿命（帧，18 帧 ≈ 0.3s @60fps）

// 开局四角默认领土：以基地中心为圆心的实心圆半径（即原来的护盾圈大小，与可调的
// shield.radius 无关，保证开局领土固定）
constexpr float BASE_TERRITORY_RADIUS = 100.0f;

constexpr int BALL_COUNT = 2000;  // 画笔球预留容量

#endif  // BTW_CORE_CONSTANTS_H
