#ifndef BTW_RENDER_RENDER_H
#define BTW_RENDER_RENDER_H

#include <vector>

#include <SDL3/SDL.h>

#include "core/state.h"

// 把持久画布拷贝到 display_canvas 并叠加所有显示层特效/UI
void RenderGame(GameState &state, const std::vector<Uint8> &canvas,
                std::vector<Uint8> &display_canvas);

#endif  // BTW_RENDER_RENDER_H
