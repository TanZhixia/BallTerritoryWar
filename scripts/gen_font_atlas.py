#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把系统字体烘焙成 16px 位图字形表（src/render/font_atlas.cpp）。

游戏运行时不加载字体文件，也不依赖任何第三方库：这里用 Pillow + macOS 自带中文字体
把界面要用的字符**在生成期**栅格化成抗锯齿覆盖率位图，写成 C++ 数组。之后
src/render/canvas.cpp 的 DrawTextFont() 逐像素混合到画布上，效果与直接调用字体一致。

用法（需要 Pillow，改动 UI 文案后重跑即可）：

    python3 scripts/gen_font_atlas.py

换字体/换字号：改下面的 FONT_PATH / FONT_INDEX / FONT_SIZE，并同步
src/render/font_atlas.h 里的 FONT_LINE_HEIGHT 与 FONT_BASELINE。
"""

import os
import sys

from PIL import Image, ImageDraw, ImageFont

# ---- 烘焙参数 ----
FONT_PATH = "/System/Library/Fonts/Hiragino Sans GB.ttc"  # 冬青黑体简体中文
FONT_INDEX = 2        # 3 个字面：0/1 = W3，2/3 = W6（加粗，小字号更清楚）
FONT_SIZE = 16
LINE_HEIGHT = 18      # src/render/font_atlas.h 的 FONT_LINE_HEIGHT
BASELINE = 15         # src/render/font_atlas.h 的 FONT_BASELINE

# ---- 收录的字符 ----
# 底部武器格 + 右侧 HUD（队伍名 / 护盾 / 弹药 / 复活中 / 已灭）
CJK = "霰弹机枪护盾大球狙击红绿蓝黄队药复活中已灭"
# 画面里的数字与单位（FormatValue 输出 0-9 / k / M / B，HUD 百分比另有 . 和 %），
# 以及倍率带的 ×8 / ×4 / ×2（× 为 U+00D7，另收一个小写 x 备用）
ASCII = "0123456789.%kMBx×- "

OUT_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "..", "src", "render", "font_atlas.cpp")

PEN_X = 16          # 渲染时笔的 x（留出左侧空间，便于算 offset_x）
CANVAS_W = 80
CANVAS_H = 80


def bake(font: ImageFont.FreeTypeFont, ch: str):
    """把一个字符渲染成紧致裁剪的覆盖率位图；空白字符返回 None。"""
    image = Image.new("L", (CANVAS_W, CANVAS_H), 0)
    ImageDraw.Draw(image).text((PEN_X, BASELINE + 20), ch, font=font, fill=255,
                               anchor="ls")
    pixels = image.load()
    xs, ys = [], []
    for y in range(CANVAS_H):
        for x in range(CANVAS_W):
            if pixels[x, y] > 0:
                xs.append(x)
                ys.append(y)
    if not xs:
        return None
    x0, x1 = min(xs), max(xs)
    y0, y1 = min(ys), max(ys)
    alpha = [pixels[x, y] for y in range(y0, y1 + 1) for x in range(x0, x1 + 1)]
    line_top = BASELINE + 20 - BASELINE
    return {
        "w": x1 - x0 + 1,
        "h": y1 - y0 + 1,
        "ox": x0 - PEN_X,
        "oy": y0 - line_top,
        "alpha": alpha,
    }


def art(glyph):
    """控制台回显（人工检查字形用）。"""
    rows = []
    for y in range(glyph["h"]):
        row = "".join("#" if glyph["alpha"][y * glyph["w"] + x] >= 110 else "."
                      for x in range(glyph["w"]))
        rows.append("   " + row)
    return "\n".join(rows)


def cpp_bytes(values, per_line=16):
    lines = []
    for i in range(0, len(values), per_line):
        chunk = ", ".join("0x%02X" % v for v in values[i:i + per_line])
        lines.append("    " + chunk + ",")
    return "\n".join(lines)


def main():
    try:
        font = ImageFont.truetype(FONT_PATH, FONT_SIZE, index=FONT_INDEX)
    except OSError as exc:
        sys.exit("无法加载字体 %s：%s" % (FONT_PATH, exc))

    chars = CJK + ASCII
    glyphs = []
    for ch in chars:
        glyph = bake(font, ch)
        advance = int(round(font.getlength(ch)))
        glyphs.append((ch, ord(ch), advance, glyph))
        if glyph:
            print("%s U+%04X 前进 %d 位图 %dx%d 偏移 (%d,%d)" %
                  (ch, ord(ch), advance, glyph["w"], glyph["h"], glyph["ox"],
                   glyph["oy"]))
            print(art(glyph))
        else:
            print("%s U+%04X 前进 %d（无墨迹）" % (ch, ord(ch), advance))

    body = []
    body.append('// 本文件由 scripts/gen_font_atlas.py 生成，请勿手改；')
    body.append('// 改文案或换字体请重跑： python3 scripts/gen_font_atlas.py')
    body.append('//')
    body.append('// 来源字体：%s (index %d) %dpx' % (FONT_PATH, FONT_INDEX, FONT_SIZE))
    body.append('')
    body.append('#include "render/font_atlas.h"')
    body.append('')
    body.append('namespace {')
    body.append('')

    names = {}
    for ch, code, advance, glyph in glyphs:
        if not glyph:
            continue
        name = "kAlpha_%04X" % code
        names[code] = name
        body.append('// %s' % ch)
        body.append('const uint8_t %s[] = {' % name)
        body.append(cpp_bytes(glyph["alpha"]))
        body.append('};')
        body.append('')

    body.append('}  // namespace')
    body.append('')
    body.append('namespace {')
    body.append('')
    body.append('const FontGlyph kGlyphs[] = {')
    for ch, code, advance, glyph in glyphs:
        if glyph:
            body.append('    {0x%04X, %d, %d, %d, %d, %d, %s},  // %s'
                        % (code, advance, glyph["w"], glyph["h"], glyph["ox"],
                           glyph["oy"], names[code], ch))
        else:
            body.append('    {0x%04X, %d, 0, 0, 0, 0, nullptr},  // %s'
                        % (code, advance, ch))
    body.append('};')
    body.append('')
    body.append('constexpr int kGlyphCount = '
                'static_cast<int>(sizeof(kGlyphs) / sizeof(kGlyphs[0]));')
    body.append('')
    body.append('}  // namespace')
    body.append('')
    body.append('const FontGlyph *FindFontGlyph(uint32_t codepoint)')
    body.append('{')
    body.append('    for (int i = 0; i < kGlyphCount; ++i) {')
    body.append('        if (kGlyphs[i].codepoint == codepoint) {')
    body.append('            return &kGlyphs[i];')
    body.append('        }')
    body.append('    }')
    body.append('    return nullptr;')
    body.append('}')
    body.append('')
    body.append('int MeasureFontText(const char *utf8)')
    body.append('{')
    body.append('    int width = 0;')
    body.append('    for (const char *p = utf8; p != nullptr && *p != 0;) {')
    body.append('        const uint32_t cp = DecodeUtf8(&p);')
    body.append('        const FontGlyph *glyph = FindFontGlyph(cp);')
    body.append('        width += glyph != nullptr ? glyph->advance : FONT_UNKNOWN_ADVANCE;')
    body.append('    }')
    body.append('    return width;')
    body.append('}')
    body.append('')
    body.append('const char *FontAtlasCharset()')
    body.append('{')
    body.append('    return "%s%s";' % (CJK, ASCII))
    body.append('}')
    body.append('')

    out_path = os.path.normpath(OUT_PATH)
    with open(out_path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(body))
    print("\n已写入 %s（%d 个字形）" % (out_path, len(glyphs)))


if __name__ == "__main__":
    main()
