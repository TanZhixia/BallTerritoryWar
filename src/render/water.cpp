#include "render/water.h"

#include "game/scene.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// ==================== 缺口里的水：涟漪 + 菲涅耳 + 折射 ====================
// 思路（2D 侧视的简化模型）：
//   1) 用三组不同波数/相速的正弦行波叠加出水面高度 h(x,t) 与斜率 dh/dx；
//   2) 斜率的绝对值当作掠射程度：越斜 → 菲涅耳反射越强（Schlick 的 2D 近似），
//      同时给一根尖细的镜面高光；
//   3) 折射：按斜率与深度把背景像素水平位移后采样（越深位移越大，模拟折射角）；
//   4) 水的吸收/散射：越深水色越浓，最后叠反射天色与表面泡沫亮线。

namespace {

// ---- 水面观感参数（想调效果改这里）----
struct Wave
{
    float k;      // 波数（弧度/像素）
    float speed;  // 相速度（弧度/秒）
    float amp;    // 振幅（像素）
    float phase;  // 初相
};

const Wave kWAVES[] = {
    {0.045f, 1.5f, 1.60f, 0.0f},   // 长波：整体起伏
    {0.110f, -2.2f, 0.80f, 1.7f},  // 中波：反向行波，避免看起来在平移
    {0.260f, 3.0f, 0.35f, 3.9f},   // 细纹
};

constexpr float SURFACE_SCALE = 1.1f;  // 波高 → 水面上下起伏的像素
constexpr float SURFACE_DROP = 3.5f;   // 水面基准线相对缺口上沿下移几像素
constexpr float REFRACT_BASE = 10.0f;  // 表层水平折射位移（像素）
constexpr float REFRACT_DEPTH = 16.0f; // 每加深一层增加的位移
constexpr float FRESNEL_GAIN = 2.6f;   // 斜率 → 掠射程度
constexpr float REFLECT_SHIFT = 30.0f; // 反射采样随斜率的水平偏移（越大镜像越"碎"）
constexpr float ATTEN_DEPTH = 0.12f;   // 光穿过水体后的中性衰减（只有明暗，不带色）
constexpr int SNAPSHOT_MARGIN = 32;    // 折射采样向左右多取的范围
constexpr int SNAPSHOT_TOP = 40;       // 向上多取的范围：水面镜像要采样水面上方

// 只有表面高光带一点白；水本身**没有颜色**，颜色全部来自背景（黑色羊毛）的折射与镜像反射
constexpr float FOAM_TINT[3] = {0.88f, 0.96f, 1.00f};

inline float Clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

// 从快照里双线性取一个通道（0-1）；折射位移是小数，双线性才不会有台阶感
inline float SampleSnapshot(const std::vector<Uint8> &buffer, int width, int height,
                            float x, float y, int channel)
{
    x = std::clamp(x, 0.0f, static_cast<float>(width - 1));
    y = std::clamp(y, 0.0f, static_cast<float>(height - 1));
    const int x0 = static_cast<int>(x);
    const int y0 = static_cast<int>(y);
    const int x1 = std::min(x0 + 1, width - 1);
    const int y1 = std::min(y0 + 1, height - 1);
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);
    const auto at = [&](int px, int py) {
        return static_cast<float>(
            buffer[(static_cast<std::size_t>(py) * width + px) * 4 + channel]);
    };
    const float top = at(x0, y0) * (1.0f - fx) + at(x1, y0) * fx;
    const float bottom = at(x0, y1) * (1.0f - fx) + at(x1, y1) * fx;
    return (top * (1.0f - fy) + bottom * fy) / 255.0f;
}

}  // namespace

void DrawLiftWater(std::vector<Uint8> &canvas, int canvas_width, int canvas_height,
                   uint64_t frame_count)
{
    const int left = static_cast<int>(LIFT_GAP_CENTER_X - LIFT_GAP_HALF);
    const int right = static_cast<int>(LIFT_GAP_CENTER_X + LIFT_GAP_HALF);
    const int top = static_cast<int>(LIFT_WALL_Y - LIFT_WALL_HALF);
    const int bottom = std::min(static_cast<int>(LIFT_WALL_Y + LIFT_WALL_HALF), canvas_height);
    if (right <= left || bottom <= top) {
        return;
    }

    // 先快照缺口附近的一条带（向上多取一段，供水面的镜像反射采样）：
    // 折射与反射都要读"别处"的像素，边画边读会拖影
    const int snap_left = std::max(0, left - SNAPSHOT_MARGIN);
    const int snap_right = std::min(canvas_width, right + SNAPSHOT_MARGIN);
    const int snap_top = std::max(0, top - SNAPSHOT_TOP);
    const int snap_width = snap_right - snap_left;
    const int snap_height = bottom - snap_top;
    std::vector<Uint8> snapshot(static_cast<std::size_t>(snap_width) * snap_height * 4);
    for (int row = 0; row < snap_height; ++row) {
        const std::size_t src =
            (static_cast<std::size_t>(snap_top + row) * canvas_width + snap_left) * 4;
        std::memcpy(&snapshot[static_cast<std::size_t>(row) * snap_width * 4],
                    &canvas[src], static_cast<std::size_t>(snap_width) * 4);
    }

    const float time = static_cast<float>(frame_count) * (1.0f / 60.0f);
    const float span = static_cast<float>(bottom - top);

    for (int x = left; x < right; ++x) {
        // ---- 1) 波面高度与斜率 ----
        const float fx = static_cast<float>(x - left);
        float height = 0.0f;
        float slope = 0.0f;
        for (const Wave &wave : kWAVES) {
            const float phase = fx * wave.k + time * wave.speed + wave.phase;
            height += wave.amp * std::sin(phase);
            slope += wave.amp * wave.k * std::cos(phase);
        }
        const float surface = static_cast<float>(top) + SURFACE_DROP + height * SURFACE_SCALE;

        // ---- 2) 菲涅耳：斜率的绝对值 = 掠射程度（Schlick 形状）----
        const float grazing = std::min(1.0f, std::fabs(slope) * FRESNEL_GAIN);
        const float fresnel = 0.05f + 0.95f * grazing * grazing;
        const float specular = grazing * grazing * grazing * grazing * grazing;

        for (int y = top; y < bottom; ++y) {
            if (static_cast<float>(y) < surface - 0.5f) {
                continue;  // 水面之上仍是空气：保持背景不变
            }
            const float depth = Clamp01((static_cast<float>(y) - surface) /
                                        std::max(1.0f, span - SURFACE_DROP));
            const float shift = slope * (REFRACT_BASE + REFRACT_DEPTH * depth);
            const float sink = height * 0.8f * depth;  // 顺带一点纵向位移，更像透过水体看
            const float sample_x = static_cast<float>(x) + shift - static_cast<float>(snap_left);
            const float sample_y = static_cast<float>(y - snap_top) + sink;

            // ---- 3) 折射：取位移后的背景色（双线性，避免整像素台阶）----
            float r = SampleSnapshot(snapshot, snap_width, snap_height, sample_x, sample_y, 0);
            float g = SampleSnapshot(snapshot, snap_width, snap_height, sample_x, sample_y, 1);
            float b = SampleSnapshot(snapshot, snap_width, snap_height, sample_x, sample_y, 2);

            // ---- 4) 反射：关于水面镜像采样水面上方（离水面越深，镜像取到越上方）。
            //      水本身没有颜色，所以反射到的就是背景那块黑色羊毛的色，而不是"天光"。
            const float mirror_x = static_cast<float>(x) + slope * REFLECT_SHIFT -
                                   static_cast<float>(snap_left);
            const float mirror_y = surface - (static_cast<float>(y) - surface) -
                                   static_cast<float>(snap_top);
            const float reflect_amount = fresnel * 0.85f;
            r += (SampleSnapshot(snapshot, snap_width, snap_height, mirror_x, mirror_y, 0) - r) *
                 reflect_amount;
            g += (SampleSnapshot(snapshot, snap_width, snap_height, mirror_x, mirror_y, 1) - g) *
                 reflect_amount;
            b += (SampleSnapshot(snapshot, snap_width, snap_height, mirror_x, mirror_y, 2) - b) *
                 reflect_amount;

            // ---- 5) 中性衰减：只把透过的光稍微压暗，不带任何色偏 ----
            const float atten = 1.0f - ATTEN_DEPTH * depth;
            r *= atten;
            g *= atten;
            b *= atten;

            // ---- 6) 表面高光：水面唯一"自带"的白色，靠菲涅耳掠射驱动 ----
            const float near_surface =
                std::max(0.0f, 1.0f - std::fabs(static_cast<float>(y) - surface) / 2.0f);
            const float foam = Clamp01(near_surface * 0.45f + specular * 0.85f);
            r += (FOAM_TINT[0] - r) * foam;
            g += (FOAM_TINT[1] - g) * foam;
            b += (FOAM_TINT[2] - b) * foam;

            const std::size_t index =
                (static_cast<std::size_t>(y) * canvas_width + x) * 4;
            canvas[index] = static_cast<Uint8>(Clamp01(r) * 255.0f + 0.5f);
            canvas[index + 1] = static_cast<Uint8>(Clamp01(g) * 255.0f + 0.5f);
            canvas[index + 2] = static_cast<Uint8>(Clamp01(b) * 255.0f + 0.5f);
            canvas[index + 3] = 255;
        }
    }
}
