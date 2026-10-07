#ifndef BTW_RENDER_CANVAS_H
#define BTW_RENDER_CANVAS_H

#include <SDL3/SDL.h>

#include <string>
#include <vector>

// ==================== 像素画布绘制原语 ====================
// 整张画布是 RGBA8 的 std::vector<Uint8>（1600×1000×4），所有图形都逐像素光栅化。
// 不带 Alpha 的函数用于持久领土画布（写死 255），带 Alpha 的函数只用于显示层混合。

// 统计圆内“需要改色”的像素数（涂画计费）
int CountPixelsToPaint(const std::vector<Uint8> &canvas, int width, int height,
                       float cx, float cy, float radius, const SDL_FColor &color);
void PaintCircle(std::vector<Uint8> &canvas, int width, int height,
                 float cx, float cy, float radius, const SDL_FColor &color);
// 带透明度混合的圆（仅显示层：与画布现有像素混合，不参与领土判定）
void PaintCircleAlpha(std::vector<Uint8> &canvas, int canvas_width, int canvas_height,
                      float cx, float cy, float radius, const SDL_FColor &color, float alpha);
// 涂画圆并给“翻色”像素打领土闪光标记（像素之前不是该色、本次被涂成该色）
void PaintCircleFlash(std::vector<Uint8> &canvas, std::vector<Uint8> &territory_flash,
                      int canvas_width, int canvas_height,
                      float cx, float cy, float radius, const SDL_FColor &color);

void FillRect(std::vector<Uint8> &canvas, int canvas_width, int canvas_height,
              int x, int y, int rect_width, int rect_height, const SDL_FColor &color);
// 位图字体（UTF-8，行高 FONT_LINE_HEIGHT，抗锯齿混合）：画面里**所有**文字都走这里
//（中文与数字同一套字形），字形数据由 scripts/gen_font_atlas.py 从系统字体烘焙，
// 见 render/font_atlas.h。y 是行顶；返回绘制结束后的 x 坐标。
int DrawTextFont(std::vector<Uint8> &canvas, int canvas_width, int canvas_height,
                 int x, int y, const char *text, const SDL_FColor &color);
// 位图字体的文本像素宽度（UTF-8），用于居中排版
int MeasureTextFont(const char *utf8);
void DrawHollowCircle(std::vector<Uint8> &canvas, int canvas_width, int canvas_height,
                      float cx, float cy, float radius,
                      const SDL_FColor &color, float thickness);
void DrawRotatedRect(std::vector<Uint8> &canvas, int canvas_width, int canvas_height,
                     float cx, float cy, float length, float thickness,
                     float angle, const SDL_FColor &color);

// 数值格式化：1000 以上带 k / M / B 单位
std::string FormatValue(float value);
// 画布该像素是否等于指定颜色（纯色精确匹配，领土判定用）
bool CanvasPixelMatches(const std::vector<Uint8> &canvas, int canvas_width,
                        int x, int y, const SDL_FColor &color);
// 采样战场区域领土比例（隔 4 像素，1/16 样本），纯色精确匹配
void SampleTerritory(const std::vector<Uint8> &canvas, const SDL_FColor *pure_colors,
                     float out[4]);

// 半透明矩形 / 圆角矩形（画在显示画布）
void FillRectAlpha(std::vector<Uint8> &canvas, int canvas_width, int canvas_height,
                   int x, int y, int rect_width, int rect_height,
                   const SDL_FColor &color, float alpha);
void FillRoundRectAlpha(std::vector<Uint8> &canvas, int canvas_width, int canvas_height,
                        int x, int y, int rect_width, int rect_height, float radius,
                        const SDL_FColor &color, float alpha);

#endif  // BTW_RENDER_CANVAS_H
