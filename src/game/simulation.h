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
#endif  // BTW_GAME_SIMULATION_H
