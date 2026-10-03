#ifndef BTW_GAME_SIMULATION_H
#define BTW_GAME_SIMULATION_H

#include <SDL3/SDL.h>

#include <vector>

#include "core/state.h"
#include "core/telemetry.h"

// 初始化实体与队伍状态（球/物理球/弹药/护盾/开局霰弹）
void InitializeGame(GameState &state);
// 推进一帧模拟；返回 false 表示本局结束
bool StepGame(GameState &state, std::vector<Uint8> &canvas,
              std::vector<Uint8> &canvas_snapshot, TelemetryState &telemetry);
// 该颜色是否还有物理球正在复活飞行中（胜负判定用：复活中的颜色不算被消灭）
bool IsRevivalPending(const GameState &state, int color);

#endif  // BTW_GAME_SIMULATION_H
