#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把系统字体烘焙成位图字形表（src/render/font_atlas.cpp）。

游戏运行时不加载字体文件，也不依赖任何第三方库：这里用 Pillow + macOS 自带中文字体
把需要的字符**在生成期**栅格化成抗锯齿覆盖率位图，写成 C++ 数组。之后
src/render/canvas.cpp 的 DrawTextFont() / DrawTextFontScaled() 按覆盖率（必要时重采样）
混合到画布上，效果与直接调用字体一致。

生成两套表：
  * small（16px）：界面与固定排版——武器名、HUD、护盾/弹药、物理球价值、×8/×4/×2
  * large（32px）：数值专用，供「随大球半径缩放」的球内数字使用（缩小采样比放大更清晰）

用法（需要 Pillow，改动 UI 文案后重跑即可）：

    python3 scripts/gen_font_atlas.py

换字体/换字号：改下面的 FONT_PATH / FONT_INDEX / *_SIZE，行高与基线由脚本自行测量，
无需手工同步头文件。
"""

import os
import sys

from PIL import Image, ImageDraw, ImageFont

# ---- 烘焙参数 ----
FONT_PATH = "/System/Library/Fonts/Hiragino Sans GB.ttc"  # 冬青黑体简体中文
FONT_INDEX = 2        # 3 个字面：0/1 = W3，2/3 = W6（加粗，小字号更清楚）
SMALL_SIZE = 16       # 界面字号
LARGE_SIZE = 32       # 缩放用的数值字号

# ---- 收录的字符 ----
# 界面文案：底部武器格 + 右侧 HUD（队伍名 / 护盾 / 弹药 / 已灭）
CJK = "霰弹机枪护盾大球狙击红绿蓝黄队药已灭"
# 画面里的数字与单位（FormatValue 输出 0-9 / k / M / B，HUD 百分比另有 . 和 %），
# 以及倍率带的 ×8 / ×4 / ×2（× 为 U+00D7，另收一个小写 x 备用）
ASCII = "0123456789.%kMBx×- "
SMALL_CHARS = CJK + ASCII
# 大号表只收大球数值会用到的字符（FormatValue 的结果）
LARGE_CHARS = "0123456789.kMB"

OUT_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "..", "src", "render", "font_atlas.cpp")

PEN_X = 16            # 渲染时笔的 x（留出左侧空间，便于算 offset_x）
BASE_Y = 96           # 渲染时的基线 y（留足上下空间）
CANVAS_W = 160
CANVAS_H = 192


def xrange_ink(image):
    pixels = image.load()
    xs, ys = [], []
    for y in range(CANVAS_H):
        for x in range(CANVAS_W):
            if pixels[x, y] > 0:
                xs.append(x)
                ys.append(y)
    return pixels, xs, ys


def measure_metrics(font, chars):
    """按实际墨迹算出「基线距行顶」与行高（不再手工同步常量）。"""
    above = below = 0
    for ch in chars:
        image = Image.new("L", (CANVAS_W, CANVAS_H), 0)
        ImageDraw.Draw(image).text((PEN_X, BASE_Y), ch, font=font, fill=255, anchor="ls")
        _, _, ys = xrange_ink(image)
        if not ys:
            continue
        above = max(above, BASE_Y - min(ys))
        below = max(below, max(ys) - BASE_Y)
    return above, above + below + 1


def bake(font, ch, baseline):
    """把一个字符渲染成紧致裁剪的覆盖率位图；空白字符返回 None。"""
    image = Image.new("L", (CANVAS_W, CANVAS_H), 0)
    ImageDraw.Draw(image).text((PEN_X, BASE_Y), ch, font=font, fill=255, anchor="ls")
    pixels, xs, ys = xrange_ink(image)
    if not xs:
        return None
    x0, x1 = min(xs), max(xs)
    y0, y1 = min(ys), max(ys)
    alpha = [pixels[x, y] for y in range(y0, y1 + 1) for x in range(x0, x1 + 1)]
    return {
        "w": x1 - x0 + 1,
        "h": y1 - y0 + 1,
        "ox": x0 - PEN_X,
        "oy": y0 - (BASE_Y - baseline),   # 相对行顶
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


def bake_table(font, size, chars, prefix, body):
    """烘焙一套表，把数组写进 body，返回 (表名, 字形数, 基线, 行高)。"""
    baseline, line_height = measure_metrics(font, chars)
    entries = []
    names = {}
    for ch in chars:
        glyph = bake(font, ch, baseline)
        advance = int(round(font.getlength(ch)))
        entries.append((ch, ord(ch), advance, glyph))
        if glyph:
            name = "kAlpha%s_%04X" % (prefix, ord(ch))
            names[ord(ch)] = name
            body.append("// %s" % ch)
            body.append("const uint8_t %s[] = {" % name)
            body.append(cpp_bytes(glyph["alpha"]))
            body.append("};")
            body.append("")
        print("%-6s %s U+%04X 前进 %d 位图 %s" %
              ("[%dpx]" % size, ch, ord(ch), advance,
               ("%dx%d 偏移 (%d,%d)" % (glyph["w"], glyph["h"], glyph["ox"], glyph["oy"]))
               if glyph else "无墨迹"))

    table = "kGlyphs%s" % prefix
    body.append("const FontGlyph %s[] = {" % table)
    for ch, code, advance, glyph in entries:
        if glyph:
            body.append("    {0x%04X, %d, %d, %d, %d, %d, %s},  // %s"
                        % (code, advance, glyph["w"], glyph["h"], glyph["ox"],
                           glyph["oy"], names[code], ch))
        else:
            body.append("    {0x%04X, %d, 0, 0, 0, 0, nullptr},  // %s"
                        % (code, advance, ch))
    body.append("};")
    body.append("")
    return table, len(entries), baseline, line_height


def main():
    try:
        small_font = ImageFont.truetype(FONT_PATH, SMALL_SIZE, index=FONT_INDEX)
        large_font = ImageFont.truetype(FONT_PATH, LARGE_SIZE, index=FONT_INDEX)
    except OSError as exc:
        sys.exit("无法加载字体 %s：%s" % (FONT_PATH, exc))

    glyph_body = []
    small = bake_table(small_font, SMALL_SIZE, SMALL_CHARS, "Small", glyph_body)
    large = bake_table(large_font, LARGE_SIZE, LARGE_CHARS, "Large", glyph_body)

    body = []
    body.append('// 本文件由 scripts/gen_font_atlas.py 生成，请勿手改；')
    body.append('// 改文案或换字体请重跑： python3 scripts/gen_font_atlas.py')
    body.append('//')
    body.append('// 来源字体：%s (index %d)' % (FONT_PATH, FONT_INDEX))
    body.append('//   小号表 %dpx：行高 %d，基线 %d' % (SMALL_SIZE, small[3], small[2]))
    body.append('//   大号表 %dpx：行高 %d，基线 %d（数值随大球半径缩放时使用）'
                % (LARGE_SIZE, large[3], large[2]))
    body.append('')
    body.append('#include "render/font_atlas.h"')
    body.append('')
    body.append('namespace {')
    body.append('')
    body.extend(glyph_body)
    body.append('}  // namespace')
    body.append('')
    body.append('const FontTable &FontSmall()')
    body.append('{')
    body.append('    static const FontTable table = {kGlyphsSmall, %d, %d, %d, %d};'
                % (small[1], small[3], small[2], SMALL_SIZE))
    body.append('    return table;')
    body.append('}')
    body.append('')
    body.append('const FontTable &FontLarge()')
    body.append('{')
    body.append('    static const FontTable table = {kGlyphsLarge, %d, %d, %d, %d};'
                % (large[1], large[3], large[2], LARGE_SIZE))
    body.append('    return table;')
    body.append('}')
    body.append('')
    body.append('const FontGlyph *FindFontGlyph(const FontTable &table, uint32_t codepoint)')
    body.append('{')
    body.append('    for (int i = 0; i < table.count; ++i) {')
    body.append('        if (table.glyphs[i].codepoint == codepoint) {')
    body.append('            return &table.glyphs[i];')
    body.append('        }')
    body.append('    }')
    body.append('    return nullptr;')
    body.append('}')
    body.append('')
    body.append('int MeasureFontText(const FontTable &table, const char *utf8)')
    body.append('{')
    body.append('    int width = 0;')
    body.append('    for (const char *p = utf8; p != nullptr && *p != 0;) {')
    body.append('        const uint32_t cp = DecodeUtf8(&p);')
    body.append('        const FontGlyph *glyph = FindFontGlyph(table, cp);')
    body.append('        width += glyph != nullptr ? glyph->advance : FONT_UNKNOWN_ADVANCE;')
    body.append('    }')
    body.append('    return width;')
    body.append('}')
    body.append('')
    body.append('const char *FontAtlasCharset()')
    body.append('{')
    body.append('    return "%s%s";' % (SMALL_CHARS, LARGE_CHARS))
    body.append('}')

    out_path = os.path.normpath(OUT_PATH)
    with open(out_path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(body) + "\n")
    print("\n已写入 %s（小号 %d 个字形 / 大号 %d 个字形）"
          % (out_path, small[1], large[1]))


if __name__ == "__main__":
    main()
