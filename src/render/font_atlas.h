#ifndef BTW_RENDER_FONT_ATLAS_H
#define BTW_RENDER_FONT_ATLAS_H

#include <cstdint>

// ==================== 位图字形表（界面中文 / 状态文案） ====================
// 字形来自系统字体（Hiragino Sans GB W6），由 scripts/gen_font_atlas.py 在**生成期**
// 栅格化成 16px 抗锯齿覆盖率位图，结果放在 render/font_atlas.cpp。
// 游戏运行时不加载字体文件、不依赖第三方库，只读这里的位图并逐像素混合，
// 因此窗口截图/录像与直接调用字体渲染的效果一致。
//
// 换字体、改字号或增删文案：改脚本参数后重跑
//     python3 scripts/gen_font_atlas.py
// 并同步下面的行高/基线常量。

constexpr int FONT_LINE_HEIGHT = 18;  // 一行占的像素高度（含下伸部分）
constexpr int FONT_BASELINE = 15;     // 行顶到基线的距离
constexpr int FONT_UNKNOWN_ADVANCE = 8;  // 未收录字符按此宽度跳过

struct FontGlyph
{
    uint32_t codepoint;    // Unicode 码点
    uint8_t advance;       // 笔前进（像素）
    uint8_t width;         // 位图宽（紧致裁剪；0 = 无墨迹，如空格）
    uint8_t height;        // 位图高
    int8_t offset_x;       // 位图左上角相对笔位置的水平偏移
    int8_t offset_y;       // 位图左上角相对行顶的垂直偏移
    const uint8_t *alpha;  // width×height 个覆盖率（0-255，行优先）
};

// 查字形；未收录返回 nullptr
const FontGlyph *FindFontGlyph(uint32_t codepoint);
// 文本像素宽度（UTF-8）；未收录字符按 FONT_UNKNOWN_ADVANCE 计
int MeasureFontText(const char *utf8);
// 生成这份字表时收录的字符（调试与文档用）
const char *FontAtlasCharset();

// 解码一个 UTF-8 码点并把 p 前移到下一个字符；非法字节返回 U+FFFD 并前进 1 字节。
// 生成的字表与画布绘制共用同一套解码，避免两处实现不一致。
inline uint32_t DecodeUtf8(const char **p)
{
    const unsigned char c = static_cast<unsigned char>(**p);
    if (c < 0x80) {
        ++(*p);
        return c;
    }
    int extra = 0;
    uint32_t cp = 0;
    if ((c & 0xE0) == 0xC0) {
        extra = 1;
        cp = c & 0x1Fu;
    } else if ((c & 0xF0) == 0xE0) {
        extra = 2;
        cp = c & 0x0Fu;
    } else if ((c & 0xF8) == 0xF0) {
        extra = 3;
        cp = c & 0x07u;
    } else {
        ++(*p);
        return 0xFFFD;
    }
    const char *q = *p + 1;
    for (int i = 0; i < extra; ++i) {
        const unsigned char cc = static_cast<unsigned char>(q[i]);
        if ((cc & 0xC0) != 0x80) {
            ++(*p);
            return 0xFFFD;
        }
        cp = (cp << 6) | (cc & 0x3Fu);
    }
    *p = q + extra;
    return cp;
}

#endif  // BTW_RENDER_FONT_ATLAS_H
