#include "game/scene.h"

#include "core/constants.h"
#include "core/math_utils.h"
#include "core/palette.h"
#include "render/canvas.h"
#include "render/font_atlas.h"

// 底部武器栏：5 格（霰弹 / 机枪 / 护盾 / 大球 / 狙击），每格 120px
void DrawBottomBar(std::vector<Uint8> &canvas, int canvas_width, int canvas_height)
{
    constexpr int BAR_Y = WINDOW_HEIGHT - 20;
    constexpr int BAR_W = 600;
    constexpr int BAR_H = 20;
    const SDL_FColor border = SDL_FColor{0.5f, 0.5f, 0.5f, 1.0f};
    const SDL_FColor sep = SDL_FColor{0.1f, 0.1f, 0.1f, 1.0f};
    const SDL_FColor label = SDL_FColor{0.9f, 0.9f, 0.9f, 1.0f};  // 武器名用浅色（同原来）
    FillRect(canvas, canvas_width, canvas_height, 0, BAR_Y, BAR_W, BAR_H, border);

    static const int EDGES[] = {0, 120, 240, 360, 480, 600};
    static const int SEPS[] = {120, 240, 360, 480};
    // 武器名（位图字体中文）：霰弹 / 机枪 / 护盾 / 大球 / 狙击
    static const char *const LABELS[] = {
        "霰弹", "机枪", "护盾", "大球", "狙击",
    };
    constexpr int COUNT = 5;
    for (int i = 0; i < COUNT - 1; ++i) {
        FillRect(canvas, canvas_width, canvas_height, SEPS[i] - 1, BAR_Y, 2, BAR_H, sep);
    }
    for (int i = 0; i < COUNT; ++i) {
        const int zone_w = EDGES[i + 1] - EDGES[i];
        const int text_width = MeasureTextFont(LABELS[i]);
        const int text_x = EDGES[i] + (zone_w - text_width) / 2;
        const int text_y = BAR_Y + (BAR_H - FONT_LINE_HEIGHT) / 2;
        DrawTextFont(canvas, canvas_width, canvas_height, text_x, text_y, LABELS[i], label);
    }
}

// 背景纹理定位：依次尝试 项目根 / build 目录 / 编译期源码目录 / 旧布局
static SDL_Surface *LoadBackgroundSurface()
{
    static const char *const CANDIDATES[] = {
        "assets/black_wool.png",        // 从项目根目录运行
        "../assets/black_wool.png",     // 从 build/ 目录运行
#ifdef BTW_SOURCE_DIR
        BTW_SOURCE_DIR "/assets/black_wool.png",  // 编译期源码目录兜底
#endif
        "black_wool.png",               // 旧布局兜底
        "../black_wool.png",
    };
    for (const char *path : CANDIDATES) {
        if (SDL_Surface *surf = SDL_LoadPNG(path)) {
            return surf;
        }
    }
    SDL_Log("background: black_wool.png 加载失败（%s），左侧保持纯色背景", SDL_GetError());
    return nullptr;
}

// 左侧机械区背景：用 black_wool.png 平铺填充（600×1000）。
// 加载失败时保留纯色背景；带 alpha 的像素与底色混合。
void FillLeftBackground(std::vector<Uint8> &canvas)
{
    SDL_Surface *surf = LoadBackgroundSurface();
    if (!surf) {
        return;
    }
    SDL_Surface *rgba = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surf);
    if (!rgba || !rgba->pixels || rgba->w <= 0 || rgba->h <= 0) {
        SDL_Log("background: 转换 RGBA32 失败（%s）", SDL_GetError());
        if (rgba) {
            SDL_DestroySurface(rgba);
        }
        return;
    }

    constexpr int LEFT_WIDTH = 600;  // 左侧机械区宽度（含边框）
    constexpr int TILE_SCALE = 3;    // 纹理放大倍数（每纹理像素占 3×3 屏幕像素）
    const Uint8 *src = static_cast<const Uint8 *>(rgba->pixels);
    const int pitch = static_cast<int>(rgba->pitch);
    for (int y = 0; y < WINDOW_HEIGHT; ++y) {
        const Uint8 *row =
            src + static_cast<std::size_t>((y / TILE_SCALE) % rgba->h) * pitch;
        for (int x = 0; x < LEFT_WIDTH; ++x) {
            const Uint8 *px =
                row + static_cast<std::size_t>((x / TILE_SCALE) % rgba->w) * 4;
            const float a = px[3] / 255.0f;
            const float inv = 1.0f - a;
            std::size_t index = (static_cast<std::size_t>(y) * WINDOW_WIDTH + x) * 4;
            canvas[index] = static_cast<Uint8>(px[0] * a + canvas[index] * inv);
            canvas[index + 1] = static_cast<Uint8>(px[1] * a + canvas[index + 1] * inv);
            canvas[index + 2] = static_cast<Uint8>(px[2] * a + canvas[index + 2] * inv);
            canvas[index + 3] = 255;
        }
    }
    SDL_Log("background: 左侧背景已用 black_wool.png 平铺（tile %dx%d，放大 %d 倍）",
            rgba->w, rgba->h, TILE_SCALE);
    SDL_DestroySurface(rgba);
}

// 一次性绘制的静态场景（调用方先分配好 canvas；blocking_circles 作为输出参数被填充）。
void BuildStaticScene(std::vector<Uint8> &canvas, std::vector<StaticCircle> &blocking_circles)
{
    const std::size_t canvas_size =
        static_cast<std::size_t>(WINDOW_WIDTH) * WINDOW_HEIGHT * 4;
    canvas.assign(canvas_size, 0);
    for (std::size_t i = 0; i < canvas.size(); i += 4) {
        canvas[i] = 15;
        canvas[i + 1] = 15;
        canvas[i + 2] = 25;
        canvas[i + 3] = 255;
    }

    // 左侧机械区背景：black_wool.png 平铺（在边框/武器栏/挡板之前画，被它们覆盖）
    FillLeftBackground(canvas);

    // 左边边框：贴着左/上/右边缘，宽度 20 像素，不画底边
    const SDL_FColor left_border_color = SDL_FColor{0.5f, 0.5f, 0.5f, 1.0f};
    constexpr int LEFT_BORDER_WIDTH = 20;
    constexpr int LEFT_AREA_WIDTH = 600;
    FillRect(canvas, WINDOW_WIDTH, WINDOW_HEIGHT,
             0, 0, LEFT_BORDER_WIDTH, WINDOW_HEIGHT, left_border_color);
    FillRect(canvas, WINDOW_WIDTH, WINDOW_HEIGHT,
             LEFT_AREA_WIDTH - LEFT_BORDER_WIDTH, 0,
             LEFT_BORDER_WIDTH, WINDOW_HEIGHT, left_border_color);
    FillRect(canvas, WINDOW_WIDTH, WINDOW_HEIGHT,
             0, 0, LEFT_AREA_WIDTH, LEFT_BORDER_WIDTH, left_border_color);

    // y=400 的横线，宽度 20，中间留缺口
    constexpr int WALL_Y = 400;
    constexpr int WALL_HALF = LEFT_BORDER_WIDTH / 2;
    constexpr int GAP_CENTER_X = 300;
    constexpr int GAP_HALF = 75;
    FillRect(canvas, WINDOW_WIDTH, WINDOW_HEIGHT,
             0, WALL_Y - WALL_HALF, GAP_CENTER_X - GAP_HALF, LEFT_BORDER_WIDTH,
             left_border_color);
    FillRect(canvas, WINDOW_WIDTH, WINDOW_HEIGHT,
             GAP_CENTER_X + GAP_HALF, WALL_Y - WALL_HALF,
             LEFT_AREA_WIDTH - (GAP_CENTER_X + GAP_HALF), LEFT_BORDER_WIDTH,
             left_border_color);

    // 横线上的图案：左右各三段 x8 | x4 | x2，中间保留缺口
    constexpr int SEPARATOR_X[] = {75, 150, 225, 375, 450, 525};
    const SDL_FColor separator_color = SDL_FColor{0.1f, 0.1f, 0.1f, 1.0f};
    const SDL_FColor label_color = SDL_FColor{0.9f, 0.9f, 0.9f, 1.0f};
    constexpr int LABEL_Y = WALL_Y - 7;  // 5×7 字体放大 2 倍，高 14px，垂直居中

    for (int separator_x : SEPARATOR_X) {
        FillRect(canvas, WINDOW_WIDTH, WINDOW_HEIGHT,
                 separator_x - 1, WALL_Y - WALL_HALF, 2, LEFT_BORDER_WIDTH,
                 separator_color);
    }

    DrawText(canvas, WINDOW_WIDTH, WINDOW_HEIGHT, 25, LABEL_Y, "x8", label_color, 2);
    DrawText(canvas, WINDOW_WIDTH, WINDOW_HEIGHT, 100, LABEL_Y, "x4", label_color, 2);
    DrawText(canvas, WINDOW_WIDTH, WINDOW_HEIGHT, 175, LABEL_Y, "x2", label_color, 2);

    DrawText(canvas, WINDOW_WIDTH, WINDOW_HEIGHT, 400, LABEL_Y, "x2", label_color, 2);
    DrawText(canvas, WINDOW_WIDTH, WINDOW_HEIGHT, 475, LABEL_Y, "x4", label_color, 2);
    DrawText(canvas, WINDOW_WIDTH, WINDOW_HEIGHT, 550, LABEL_Y, "x8", label_color, 2);

    // 底部武器栏（5 格：SHOTGUN / MACHINEGUN / SHIELD / BIGBALL / SNIPER）
    DrawBottomBar(canvas, WINDOW_WIDTH, WINDOW_HEIGHT);

    // 横线上方的两层固定灰色阻挡圆球
    GetBlockingCircles(blocking_circles);
    for (const StaticCircle &circle : blocking_circles) {
        PaintCircle(canvas, WINDOW_WIDTH, WINDOW_HEIGHT,
                    circle.x, circle.y, circle.radius, left_border_color);
    }
    // *8|*4 和 *4|*2 边界挡板
    constexpr int BARRIER_X[] = {75, 150, 450, 525};
    for (int barrier_x : BARRIER_X) {
        FillRect(canvas, WINDOW_WIDTH, WINDOW_HEIGHT,
                 barrier_x - 5, 310, 10, 100, left_border_color);
    }
    FillRect(canvas, WINDOW_WIDTH, WINDOW_HEIGHT,
             0, 410, 225, 20, left_border_color);
    FillRect(canvas, WINDOW_WIDTH, WINDOW_HEIGHT,
             375, 410, 225, 20, left_border_color);

    // 四角默认领土：以基地中心为圆心、半径 BASE_TERRITORY_RADIUS 的实心圆
    //（圆心与护盾圈/炮塔/基地占领判定完全一致，只是形状由方块改为圆）
    for (int color = 0; color < 4; ++color) {
        float center_x = 0.0f;
        float center_y = 0.0f;
        GetColorBlockCenter(color, center_x, center_y);
        const SDL_Color corner = color == 0   ? PURE_FRAME_PALETTE.top_left
                                 : color == 1 ? PURE_FRAME_PALETTE.top_right
                                 : color == 2 ? PURE_FRAME_PALETTE.bottom_left
                                              : PURE_FRAME_PALETTE.bottom_right;
        PaintCircle(canvas, WINDOW_WIDTH, WINDOW_HEIGHT, center_x, center_y,
                    BASE_TERRITORY_RADIUS, ToFColor(corner));
    }
}

// ==================== 布局几何 ====================

// 四角基地中心：距两条边各 100px（= 开领土圆的半径，圆心即护盾/炮塔/占领判定中心）
void GetColorBlockCenter(int color_index, float &x, float &y)
{
    const float block_half = BASE_TERRITORY_RADIUS;  // 距边 100px
    x = FRAME_X + block_half;
    y = FRAME_Y + block_half;
    if (color_index == 1 || color_index == 3) {
        x = FRAME_X + FRAME_SIZE - block_half;
    }
    if (color_index == 2 || color_index == 3) {
        y = FRAME_Y + FRAME_SIZE - block_half;
    }
}

// 机械区的固定阻挡圆阵（全部为小圆点，半径 = 物理球半径的一半）：
//   * 乘法带以上（顶部边框 y=20 ~ 乘法带上沿 y=390）三排，居中平摊
//   * 乘法带以下（下沿 y=430 起 200px 之后）四排，排间距 65px
// 两处的排间距都是 65px，逐排横向错开 40px（交替排列），列间距 80px。
void GetBlockingCircles(std::vector<StaticCircle> &circles)
{
    circles.clear();

    const float dot_radius = std::max(1.0f, g_config.physics.radius * 0.5f);
    constexpr float DOT_ROW_SPACING = 65.0f;  // 上下两处点阵共用的排间距
    constexpr float COL_SPACING = 80.0f;      // 列间距

    // ---------- 乘法带以上：3 排，在腔体内居中平摊 ----------
    constexpr float UPPER_TOP = 20.0f;      // 顶部边框下沿
    constexpr float UPPER_BOTTOM = 390.0f;  // 乘法带上沿
    constexpr int UPPER_ROWS = 3;
    const float upper_span = DOT_ROW_SPACING * static_cast<float>(UPPER_ROWS - 1);
    const float upper_first =
        UPPER_TOP + (UPPER_BOTTOM - UPPER_TOP - upper_span) * 0.5f;
    for (int row = 0; row < UPPER_ROWS; ++row) {
        const float row_y = upper_first + static_cast<float>(row) * DOT_ROW_SPACING;
        const float start_x = (row % 2 == 0) ? 60.0f : 20.0f;  // 逐排交错 40px
        for (float x = start_x; x <= 580.0f; x += COL_SPACING) {
            circles.push_back(StaticCircle{x, row_y, dot_radius});
        }
    }

    // ---------- 乘法带以下：4 排，从带下 200px 起，排间距与上方一致 ----------
    constexpr float BAND_BOTTOM = 430.0f;     // 乘法带下沿
    constexpr float GAP_BELOW_BAND = 200.0f;  // 第一排与乘法带的距离
    constexpr float FIELD_TOP = BAND_BOTTOM + GAP_BELOW_BAND;
    constexpr int LOWER_ROWS = 4;  // 原本 6 排，去掉最下面两排
    for (int row = 0; row < LOWER_ROWS; ++row) {
        const float row_y = FIELD_TOP + static_cast<float>(row) * DOT_ROW_SPACING;
        const float start_x = (row % 2 == 0) ? 60.0f : 20.0f;  // 逐排交错 40px
        for (float x = start_x; x <= 580.0f; x += COL_SPACING) {
            circles.push_back(StaticCircle{x, row_y, dot_radius});
        }
    }
}
