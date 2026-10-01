#ifndef BTW_CORE_MATH_UTILS_H
#define BTW_CORE_MATH_UTILS_H

#include <SDL3/SDL.h>

#include <cstdlib>

// [0, 1) 均匀随机数（使用全局 std::rand，种子在开局时设置）
inline float RandFloat()
{
    return static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
}

// SDL_Color（0~255）→ SDL_FColor（0~1）
inline SDL_FColor ToFColor(const SDL_Color &color)
{
    return SDL_FColor{
        color.r / 255.0f,
        color.g / 255.0f,
        color.b / 255.0f,
        color.a / 255.0f,
    };
}

#endif  // BTW_CORE_MATH_UTILS_H
