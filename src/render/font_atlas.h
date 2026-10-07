#ifndef BTW_RENDER_FONT_ATLAS_H
#define BTW_RENDER_FONT_ATLAS_H

#include <cstdint>

// ==================== 位图字形表（画面里的全部文字） ====================
// 字形来自系统字体（Hiragino Sans GB W6），由 scripts/gen_font_atlas.py 在**生成期**
// 栅格化成抗锯齿覆盖率位图，结果放在 render/font_atlas.cpp。
// 游戏运行时不加载字体文件、不依赖第三方库，只读这里的位图并逐像素混合，
// 因此窗口截图/录像与直接调用字体渲染的效果一致。
//
// 两套表：
//   * FontSmall()（16px）：界面与固定排版——武器名、HUD、护盾/弹药、物理球价值、×8/×4/×2
//   * FontLarge()（32px）：数值专用，供随大球半径缩放的球内数字缩采样使用
//     （从大字表缩小比把小字表放大清晰得多）
// 行高与基线由脚本按实际墨迹测量后写进生成文件，这里不再硬编码。
//
// 换字体、改字号或增删文案：改脚本参数后重跑
//     python3 scripts/gen_font_atlas.py

constexpr int FONT_UNKNOWN_ADVANCE = 8;  // 未收录字符按此宽度跳过

struct FontGlyph
{
    uint32_t codepoint;    // Unicode 码点
    uint8_t advance;       // 笔前进（像素，按烘焙字号）
    uint8_t width;         // 位图宽（紧致裁剪；0 = 无墨迹，如空格）
    uint8_t height;        // 位图高
    int8_t offset_x;       // 位图左上角相对笔位置的水平偏移
    int8_t offset_y;       // 位图左上角相对行顶的垂直偏移
    const uint8_t *alpha;  // width×height 个覆盖率（0-255，行优先）
};

struct FontTable
{
    const FontGlyph *glyphs;
    int count;
    int line_height;   // 该字号的行高（像素）
    int baseline;      // 行顶到基线
    int nominal_size;  // 烘焙字号（用于两套表之间的缩放换算）
};

const FontTable &FontSmall();
const FontTable &FontLarge();

// 主字号行高（= FontSmall().line_height），界面排版常用
inline int FontLineHeight() { return FontSmall().line_height; }

// 在指定表里查字形；未收录返回 nullptr
const FontGlyph *FindFontGlyph(const FontTable &table, uint32_t codepoint);
// 文本像素宽度（UTF-8，按该表的烘焙字号）
int MeasureFontText(const FontTable &table, const char *utf8);
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
