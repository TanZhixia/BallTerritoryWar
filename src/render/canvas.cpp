#include "render/canvas.h"

#include "core/constants.h"
#include "render/font_atlas.h"

#include <cmath>
#include <cstring>
int CountPixelsToPaint(const std::vector<Uint8> &canvas, int width, int height,
                              float cx, float cy, float radius, const SDL_FColor &color)
{
    const Uint8 r = static_cast<Uint8>(color.r * 255.0f + 0.5f);
    const Uint8 g = static_cast<Uint8>(color.g * 255.0f + 0.5f);
    const Uint8 b = static_cast<Uint8>(color.b * 255.0f + 0.5f);

    const int min_x = std::max(0, static_cast<int>(std::floor(cx - radius)));
    const int max_x = std::min(width - 1, static_cast<int>(std::ceil(cx + radius)));
    const int min_y = std::max(0, static_cast<int>(std::floor(cy - radius)));
    const int max_y = std::min(height - 1, static_cast<int>(std::ceil(cy + radius)));

    int different = 0;
    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const float dx = static_cast<float>(x) - cx;
            const float dy = static_cast<float>(y) - cy;
            if (dx * dx + dy * dy > radius * radius) {
                continue;
            }

            const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4;
            if (canvas[index] != r || canvas[index + 1] != g || canvas[index + 2] != b) {
                ++different;
            }
        }
    }
    return different;
}

void PaintCircle(std::vector<Uint8> &canvas, int width, int height,
                        float cx, float cy, float radius, const SDL_FColor &color)
{
    const Uint8 r = static_cast<Uint8>(color.r * 255.0f + 0.5f);
    const Uint8 g = static_cast<Uint8>(color.g * 255.0f + 0.5f);
    const Uint8 b = static_cast<Uint8>(color.b * 255.0f + 0.5f);

    const int min_x = std::max(0, static_cast<int>(std::floor(cx - radius)));
    const int max_x = std::min(width - 1, static_cast<int>(std::ceil(cx + radius)));
    const int min_y = std::max(0, static_cast<int>(std::floor(cy - radius)));
    const int max_y = std::min(height - 1, static_cast<int>(std::ceil(cy + radius)));

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const float dx = static_cast<float>(x) - cx;
            const float dy = static_cast<float>(y) - cy;
            if (dx * dx + dy * dy > radius * radius) {
                continue;
            }

            std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4;
            canvas[index] = r;
            canvas[index + 1] = g;
            canvas[index + 2] = b;
            canvas[index + 3] = 255;
        }
    }
}

// 带透明度混合的圆（仅显示层：与画布现有像素混合，不写入领土画布）
void PaintCircleAlpha(std::vector<Uint8> &canvas, int canvas_width, int canvas_height,
                      float cx, float cy, float radius, const SDL_FColor &color, float alpha)
{
    const float t = alpha;
    const float inv = 1.0f - t;

    const int min_x = std::max(0, static_cast<int>(std::floor(cx - radius)));
    const int max_x = std::min(canvas_width - 1, static_cast<int>(std::ceil(cx + radius)));
    const int min_y = std::max(0, static_cast<int>(std::floor(cy - radius)));
    const int max_y = std::min(canvas_height - 1, static_cast<int>(std::ceil(cy + radius)));

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const float dx = static_cast<float>(x) - cx;
            const float dy = static_cast<float>(y) - cy;
            if (dx * dx + dy * dy > radius * radius) {
                continue;
            }
            std::size_t index = (static_cast<std::size_t>(y) * canvas_width + x) * 4;
            canvas[index] = static_cast<Uint8>(color.r * 255.0f * t + canvas[index] * inv);
            canvas[index + 1] = static_cast<Uint8>(color.g * 255.0f * t + canvas[index + 1] * inv);
            canvas[index + 2] = static_cast<Uint8>(color.b * 255.0f * t + canvas[index + 2] * inv);
            canvas[index + 3] = 255;
        }
    }
}

// 涂画圆并给"翻色"像素打领土闪光标记：
// 像素之前不是 color、本次被涂成 color（领土新增）→ flash 记满寿命，
// 之后由 UpdateAndDrawTerritoryFlash 逐帧衰减淡出；本来就是该色则不点亮。
void PaintCircleFlash(std::vector<Uint8> &canvas, std::vector<Uint8> &territory_flash,
                      int canvas_width, int canvas_height,
                      float cx, float cy, float radius, const SDL_FColor &color)
{
    const Uint8 r = static_cast<Uint8>(color.r * 255.0f + 0.5f);
    const Uint8 g = static_cast<Uint8>(color.g * 255.0f + 0.5f);
    const Uint8 b = static_cast<Uint8>(color.b * 255.0f + 0.5f);

    const int min_x = std::max(0, static_cast<int>(std::floor(cx - radius)));
    const int max_x = std::min(canvas_width - 1, static_cast<int>(std::ceil(cx + radius)));
    const int min_y = std::max(0, static_cast<int>(std::floor(cy - radius)));
    const int max_y = std::min(canvas_height - 1, static_cast<int>(std::ceil(cy + radius)));

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const float dx = static_cast<float>(x) - cx;
            const float dy = static_cast<float>(y) - cy;
            if (dx * dx + dy * dy > radius * radius) {
                continue;
            }

            std::size_t index = (static_cast<std::size_t>(y) * canvas_width + x) * 4;
            if (canvas[index] != r || canvas[index + 1] != g || canvas[index + 2] != b) {
                const int fx = x - static_cast<int>(FRAME_X);
                if (fx >= 0 && fx < TERRITORY_FLASH_SIZE && y >= 0 &&
                    y < TERRITORY_FLASH_SIZE) {
                    territory_flash[static_cast<std::size_t>(y) * TERRITORY_FLASH_SIZE +
                                    fx] = TERRITORY_FLASH_FRAMES;
                }
            }
            canvas[index] = r;
            canvas[index + 1] = g;
            canvas[index + 2] = b;
            canvas[index + 3] = 255;
        }
    }
}

void FillRect(std::vector<Uint8> &canvas, int canvas_width, int canvas_height,
                     int x, int y, int rect_width, int rect_height, const SDL_FColor &color)
{
    const Uint8 r = static_cast<Uint8>(color.r * 255.0f + 0.5f);
    const Uint8 g = static_cast<Uint8>(color.g * 255.0f + 0.5f);
    const Uint8 b = static_cast<Uint8>(color.b * 255.0f + 0.5f);

    const int min_x = std::max(0, x);
    const int max_x = std::min(canvas_width - 1, x + rect_width - 1);
    const int min_y = std::max(0, y);
    const int max_y = std::min(canvas_height - 1, y + rect_height - 1);

    for (int py = min_y; py <= max_y; ++py) {
        for (int px = min_x; px <= max_x; ++px) {
            std::size_t index = (static_cast<std::size_t>(py) * canvas_width + px) * 4;
            canvas[index] = r;
            canvas[index + 1] = g;
            canvas[index + 2] = b;
            canvas[index + 3] = 255;
        }
    }
}

// ==================== 位图字体（画面里的全部文字） ====================
// 字形来自 render/font_atlas.h 的数据（生成期由系统字体烘焙），逐像素按覆盖率
// 与目标画布混合。中文、数字、单位共用同一套字形，画面里不再有别的字体。

int MeasureTextFont(const char *utf8)
{
    return MeasureFontText(utf8);
}

int DrawTextFont(std::vector<Uint8> &canvas, int canvas_width, int canvas_height,
                 int x, int y, const char *text, const SDL_FColor &color)
{
    const int cr = static_cast<int>(color.r * 255.0f + 0.5f);
    const int cg = static_cast<int>(color.g * 255.0f + 0.5f);
    const int cb = static_cast<int>(color.b * 255.0f + 0.5f);

    int pen_x = x;
    for (const char *p = text; p != nullptr && *p != 0;) {
        const uint32_t codepoint = DecodeUtf8(&p);
        const FontGlyph *glyph = FindFontGlyph(codepoint);
        if (glyph == nullptr) {
            pen_x += FONT_UNKNOWN_ADVANCE;  // 字表没收录：跳过，不留空白方块
            continue;
        }
        if (glyph->alpha != nullptr && glyph->width > 0 && glyph->height > 0) {
            for (int gy = 0; gy < glyph->height; ++gy) {
                const int py = y + glyph->offset_y + gy;
                if (py < 0 || py >= canvas_height) {
                    continue;
                }
                for (int gx = 0; gx < glyph->width; ++gx) {
                    const int coverage = glyph->alpha[gy * glyph->width + gx];
                    if (coverage == 0) {
                        continue;
                    }
                    const int px = pen_x + glyph->offset_x + gx;
                    if (px < 0 || px >= canvas_width) {
                        continue;
                    }
                    const std::size_t index =
                        (static_cast<std::size_t>(py) * canvas_width + px) * 4;
                    const int inv = 255 - coverage;
                    canvas[index] = static_cast<Uint8>(
                        (cr * coverage + canvas[index] * inv + 127) / 255);
                    canvas[index + 1] = static_cast<Uint8>(
                        (cg * coverage + canvas[index + 1] * inv + 127) / 255);
                    canvas[index + 2] = static_cast<Uint8>(
                        (cb * coverage + canvas[index + 2] * inv + 127) / 255);
                    canvas[index + 3] = 255;
                }
            }
        }
        pen_x += glyph->advance;
    }
    return pen_x;
}

void DrawHollowCircle(std::vector<Uint8> &canvas, int canvas_width, int canvas_height,
                             float cx, float cy, float radius,
                             const SDL_FColor &color, float thickness)
{
    const Uint8 r = static_cast<Uint8>(color.r * 255.0f + 0.5f);
    const Uint8 g = static_cast<Uint8>(color.g * 255.0f + 0.5f);
    const Uint8 b = static_cast<Uint8>(color.b * 255.0f + 0.5f);

    const int min_x = std::max(0, static_cast<int>(std::floor(cx - radius - thickness)));
    const int max_x = std::min(canvas_width - 1, static_cast<int>(std::ceil(cx + radius + thickness)));
    const int min_y = std::max(0, static_cast<int>(std::floor(cy - radius - thickness)));
    const int max_y = std::min(canvas_height - 1, static_cast<int>(std::ceil(cy + radius + thickness)));

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const float dx = static_cast<float>(x) - cx;
            const float dy = static_cast<float>(y) - cy;
            const float dist = std::sqrt(dx * dx + dy * dy);
            if (std::fabs(dist - radius) > thickness * 0.5f) {
                continue;
            }
            std::size_t index = (static_cast<std::size_t>(y) * canvas_width + x) * 4;
            canvas[index] = r;
            canvas[index + 1] = g;
            canvas[index + 2] = b;
            canvas[index + 3] = 255;
        }
    }
}

void DrawRotatedRect(std::vector<Uint8> &canvas, int canvas_width, int canvas_height,
                            float cx, float cy, float length, float thickness,
                            float angle, const SDL_FColor &color)
{
    const Uint8 r = static_cast<Uint8>(color.r * 255.0f + 0.5f);
    const Uint8 g = static_cast<Uint8>(color.g * 255.0f + 0.5f);
    const Uint8 b = static_cast<Uint8>(color.b * 255.0f + 0.5f);

    const float cos_a = std::cos(angle);
    const float sin_a = std::sin(angle);
    const float half_len = length * 0.5f;
    const float half_thick = thickness * 0.5f;

    const int min_x = std::max(0, static_cast<int>(std::floor(cx - length)));
    const int max_x = std::min(canvas_width - 1, static_cast<int>(std::ceil(cx + length)));
    const int min_y = std::max(0, static_cast<int>(std::floor(cy - length)));
    const int max_y = std::min(canvas_height - 1, static_cast<int>(std::ceil(cy + length)));

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const float dx = static_cast<float>(x) - cx;
            const float dy = static_cast<float>(y) - cy;
            const float local_x = dx * cos_a + dy * sin_a;
            const float local_y = -dx * sin_a + dy * cos_a;
            if (std::fabs(local_x) <= half_len &&
                std::fabs(local_y) <= half_thick) {
                const std::size_t index =
                    (static_cast<std::size_t>(y) * canvas_width + x) * 4;
                canvas[index] = r;
                canvas[index + 1] = g;
                canvas[index + 2] = b;
                canvas[index + 3] = 255;
            }
        }
    }
}

std::string FormatValue(float value)
{
    char buffer[32];
    if (value >= 1000000000.0f) {
        std::snprintf(buffer, sizeof(buffer), "%.0fB", value / 1000000000.0f);
    } else if (value >= 1000000.0f) {
        std::snprintf(buffer, sizeof(buffer), "%.0fM", value / 1000000.0f);
    } else if (value >= 1000.0f) {
        std::snprintf(buffer, sizeof(buffer), "%.0fk", value / 1000.0f);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(value));
    }
    return std::string(buffer);
}

// ==================== 半透明绘制（显示层专用，不参与领土判定） ====================

void FillRectAlpha(std::vector<Uint8> &canvas, int canvas_width, int canvas_height,
                   int x, int y, int rect_width, int rect_height,
                   const SDL_FColor &color, float alpha)
{
    const Uint8 r = static_cast<Uint8>(color.r * 255.0f + 0.5f);
    const Uint8 g = static_cast<Uint8>(color.g * 255.0f + 0.5f);
    const Uint8 b = static_cast<Uint8>(color.b * 255.0f + 0.5f);
    const float t = alpha;
    const float inv = 1.0f - t;

    const int min_x = std::max(0, x);
    const int max_x = std::min(canvas_width - 1, x + rect_width - 1);
    const int min_y = std::max(0, y);
    const int max_y = std::min(canvas_height - 1, y + rect_height - 1);
    for (int py = min_y; py <= max_y; ++py) {
        for (int px = min_x; px <= max_x; ++px) {
            std::size_t index = (static_cast<std::size_t>(py) * canvas_width + px) * 4;
            canvas[index] = static_cast<Uint8>(r * t + canvas[index] * inv);
            canvas[index + 1] = static_cast<Uint8>(g * t + canvas[index + 1] * inv);
            canvas[index + 2] = static_cast<Uint8>(b * t + canvas[index + 2] * inv);
        }
    }
}

// 圆角矩形（逐像素判定：点到内部最近点的距离 <= 半径即在内），半透明混合
void FillRoundRectAlpha(std::vector<Uint8> &canvas, int canvas_width, int canvas_height,
                        int x, int y, int rect_width, int rect_height, float radius,
                        const SDL_FColor &color, float alpha)
{
    const Uint8 r = static_cast<Uint8>(color.r * 255.0f + 0.5f);
    const Uint8 g = static_cast<Uint8>(color.g * 255.0f + 0.5f);
    const Uint8 b = static_cast<Uint8>(color.b * 255.0f + 0.5f);
    const float t = alpha;
    const float inv = 1.0f - t;
    const float rad = radius;

    const int min_x = std::max(0, x);
    const int max_x = std::min(canvas_width - 1, x + rect_width - 1);
    const int min_y = std::max(0, y);
    const int max_y = std::min(canvas_height - 1, y + rect_height - 1);
    for (int py = min_y; py <= max_y; ++py) {
        for (int px = min_x; px <= max_x; ++px) {
            const float px_c = static_cast<float>(px) + 0.5f;
            const float py_c = static_cast<float>(py) + 0.5f;
            // 找矩形内最近点（圆角处被裁圆）
            float nx = px_c;
            float ny = py_c;
            if (nx < x + rad) nx = x + rad;
            if (nx > x + rect_width - rad) nx = x + rect_width - rad;
            if (ny < y + rad) ny = y + rad;
            if (ny > y + rect_height - rad) ny = y + rect_height - rad;
            const float dx = px_c - nx;
            const float dy = py_c - ny;
            if (dx * dx + dy * dy > rad * rad) {
                continue;
            }
            std::size_t index = (static_cast<std::size_t>(py) * canvas_width + px) * 4;
            canvas[index] = static_cast<Uint8>(r * t + canvas[index] * inv);
            canvas[index + 1] = static_cast<Uint8>(g * t + canvas[index + 1] * inv);
            canvas[index + 2] = static_cast<Uint8>(b * t + canvas[index + 2] * inv);
        }
    }
}

// ==================== 领土采样（HUD 与遥测共用） ====================

void SampleTerritory(const std::vector<Uint8> &canvas, const SDL_FColor *pure_colors,
                     float out[4])
{
    Uint8 pure_r[4] = {}, pure_g[4] = {}, pure_b[4] = {};
    for (int c = 0; c < 4; ++c) {
        pure_r[c] = static_cast<Uint8>(pure_colors[c].r * 255.0f + 0.5f);
        pure_g[c] = static_cast<Uint8>(pure_colors[c].g * 255.0f + 0.5f);
        pure_b[c] = static_cast<Uint8>(pure_colors[c].b * 255.0f + 0.5f);
    }
    // 打包成 32 位一次性比较（RGBA 小端：R | G<<8 | B<<16 | A<<24）
    Uint32 packed[4] = {};
    for (int c = 0; c < 4; ++c) {
        packed[c] = static_cast<Uint32>(pure_r[c]) |
                     (static_cast<Uint32>(pure_g[c]) << 8) |
                     (static_cast<Uint32>(pure_b[c]) << 16) | 0xFF000000u;
    }

    // 战场区域隔 4 像素采样（1/16 像素，~62.5k 样本，百分比误差 <0.2%）
    float territory[4] = {};
    const int x0 = static_cast<int>(FRAME_X);
    const int x1 = x0 + static_cast<int>(FRAME_SIZE);
    int samples = 0;
    for (int y = 0; y < WINDOW_HEIGHT; y += 4) {
        for (int x = x0; x < x1; x += 4) {
            Uint32 pixel = 0;
            std::memcpy(&pixel,
                        &canvas[(static_cast<std::size_t>(y) * WINDOW_WIDTH + x) * 4],
                        sizeof(pixel));
            for (int c = 0; c < 4; ++c) {
                if (pixel == packed[c]) {
                    territory[c] += 1.0f;
                    break;
                }
            }
            ++samples;
        }
    }
    for (int c = 0; c < 4; ++c) {
        out[c] = territory[c] / static_cast<float>(std::max(1, samples));
    }
}

// ==================== 画布像素查询 ====================

// 该像素是否等于指定颜色（纯色精确匹配，基地占领判定用）
bool CanvasPixelMatches(const std::vector<Uint8> &canvas, int canvas_width,
                        int x, int y, const SDL_FColor &color)
{
    const Uint8 r = static_cast<Uint8>(color.r * 255.0f + 0.5f);
    const Uint8 g = static_cast<Uint8>(color.g * 255.0f + 0.5f);
    const Uint8 b = static_cast<Uint8>(color.b * 255.0f + 0.5f);
    const std::size_t index = (static_cast<std::size_t>(y) * canvas_width + x) * 4;
    return canvas[index] == r && canvas[index + 1] == g && canvas[index + 2] == b;
}
