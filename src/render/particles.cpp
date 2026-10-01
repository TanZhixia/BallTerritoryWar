// ==================== 视觉粒子系统 ====================
// 只画在显示画布（display_canvas）上，按生命淡出到背景色后回收；
// 不写入持久领土画布，不影响涂画/碰撞/计费。

#include "render/particles.h"

#include "core/constants.h"
#include "core/math_utils.h"
#include "render/canvas.h"

#include <algorithm>
#include <cmath>

void SpawnSniperParticles(std::vector<Particle> &particles, float x, float y,
                          const SDL_FColor &color, int count)
{
    for (int i = 0; i < count; ++i) {
        Particle p;
        const float angle = RandFloat() * 2.0f * static_cast<float>(M_PI);
        const float speed = 20.0f + RandFloat() * 40.0f;
        p.x = x + (RandFloat() * 2.0f - 1.0f) * 3.0f;
        p.y = y + (RandFloat() * 2.0f - 1.0f) * 3.0f;
        p.vx = std::cos(angle) * speed;
        p.vy = std::sin(angle) * speed;
        p.max_life = 0.25f + RandFloat() * 0.25f;
        p.life = p.max_life;
        p.radius = 1.0f + RandFloat() * 1.5f;
        p.color = color;
        particles.push_back(p);
    }
    // 容量保护：粒子过多时丢弃最老的
    constexpr std::size_t MAX_PARTICLES = 2000;
    if (particles.size() > MAX_PARTICLES) {
        particles.erase(particles.begin(),
                        particles.begin() + (particles.size() - MAX_PARTICLES));
    }
}

void UpdateParticles(std::vector<Particle> &particles, float dt)
{
    std::size_t write = 0;
    for (std::size_t i = 0; i < particles.size(); ++i) {
        Particle &p = particles[i];
        p.life -= dt;
        if (p.life <= 0.0f) {
            continue;
        }
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        // 轻微阻力，火花逐渐减速
        p.vx *= (1.0f - 3.0f * dt);
        p.vy *= (1.0f - 3.0f * dt);
        particles[write++] = p;
    }
    particles.resize(write);
}

void DrawParticles(std::vector<Uint8> &canvas, int canvas_width, int canvas_height,
                   const std::vector<Particle> &particles)
{
    // 背景色（main.cpp 初始化画布用的底色），粒子淡出到它
    constexpr float BG_R = 15.0f / 255.0f;
    constexpr float BG_G = 15.0f / 255.0f;
    constexpr float BG_B = 25.0f / 255.0f;

    for (const Particle &p : particles) {
        const float t = p.life / p.max_life;  // 1 → 0
        SDL_FColor c;
        c.r = p.color.r * t + BG_R * (1.0f - t);
        c.g = p.color.g * t + BG_G * (1.0f - t);
        c.b = p.color.b * t + BG_B * (1.0f - t);
        c.a = 1.0f;
        const float r = p.radius * (0.4f + 0.6f * t);  // 越老越小
        PaintCircle(canvas, canvas_width, canvas_height, p.x, p.y, r, c);
    }
}

// ==================== 光点拖尾（狙击用，仅显示层） ====================

void SpawnBallTrail(std::vector<TrailDot> &dots, float x, float y,
                    const SDL_FColor &color, float life)
{
    TrailDot d;
    d.x = x;
    d.y = y;
    d.max_life = life;
    d.life = life;
    d.color = color;
    dots.push_back(d);
    constexpr std::size_t MAX_TRAIL_DOTS = 5000;
    if (dots.size() > MAX_TRAIL_DOTS) {
        dots.erase(dots.begin(), dots.begin() + (dots.size() - MAX_TRAIL_DOTS));
    }
}

void UpdateTrailDots(std::vector<TrailDot> &dots, float dt)
{
    std::size_t write = 0;
    for (std::size_t i = 0; i < dots.size(); ++i) {
        TrailDot &d = dots[i];
        d.life -= dt;
        if (d.life > 0.0f) {
            dots[write++] = d;
        }
    }
    dots.resize(write);
}

void DrawTrailDots(std::vector<Uint8> &canvas, int canvas_width, int canvas_height,
                   const std::vector<TrailDot> &dots)
{
    constexpr float BG_R = 15.0f / 255.0f;
    constexpr float BG_G = 15.0f / 255.0f;
    constexpr float BG_B = 25.0f / 255.0f;
    for (const TrailDot &d : dots) {
        const float t = d.life / d.max_life;  // 1 → 0
        const float alpha = 0.45f * t;
        const float inv = 1.0f - alpha;
        SDL_FColor c;
        c.r = d.color.r * alpha + BG_R * inv;
        c.g = d.color.g * alpha + BG_G * inv;
        c.b = d.color.b * alpha + BG_B * inv;
        c.a = 1.0f;
        const float radius = 1.5f * (0.3f + 0.7f * t);
        PaintCircle(canvas, canvas_width, canvas_height, d.x, d.y, radius, c);
    }
}

// ==================== 冲击波（物理球触发乘法带/武器格时从落点扩散） ====================
// 只画在显示画布上，不写入持久领土画布，不影响玩法/计费。

void SpawnShockwave(std::vector<Shockwave> &waves, float x, float y,
                    float max_radius, float duration, float thickness,
                    const SDL_FColor &color)
{
    Shockwave w;
    w.x = x;
    w.y = y;
    w.radius = 2.0f;          // 初始小半径，随后扩散
    w.max_radius = max_radius;
    w.life = duration;
    w.max_life = duration;
    w.thickness = thickness;
    w.color = color;
    waves.push_back(w);
    // 容量保护：冲击波过多时丢弃最老的
    constexpr std::size_t MAX_WAVES = 64;
    if (waves.size() > MAX_WAVES) {
        waves.erase(waves.begin(), waves.begin() + (waves.size() - MAX_WAVES));
    }
}

void UpdateShockwaves(std::vector<Shockwave> &waves, float dt)
{
    std::size_t write = 0;
    for (std::size_t i = 0; i < waves.size(); ++i) {
        Shockwave &w = waves[i];
        w.life -= dt;
        if (w.life <= 0.0f) {
            continue;
        }
        // 半径从初始线性扩散到 max_radius
        const float t = 1.0f - w.life / w.max_life;  // 0 → 1
        w.radius = w.max_radius * (0.12f + 0.88f * t);
        // 环随扩散变细
        w.thickness = std::max(0.5f, w.thickness * (1.0f - 0.55f * t));
        waves[write++] = w;
    }
    waves.resize(write);
}

void DrawShockwaves(std::vector<Uint8> &canvas, int canvas_width, int canvas_height,
                    const std::vector<Shockwave> &waves)
{
    constexpr float BG_R = 15.0f / 255.0f;
    constexpr float BG_G = 15.0f / 255.0f;
    constexpr float BG_B = 25.0f / 255.0f;

    for (const Shockwave &w : waves) {
        const float t = w.life / w.max_life;  // 1 → 0 淡出
        const float alpha = 0.55f * t;
        const float inv = 1.0f - alpha;
        const Uint8 r = static_cast<Uint8>((w.color.r * alpha + BG_R * inv) * 255.0f + 0.5f);
        const Uint8 g = static_cast<Uint8>((w.color.g * alpha + BG_G * inv) * 255.0f + 0.5f);
        const Uint8 b = static_cast<Uint8>((w.color.b * alpha + BG_B * inv) * 255.0f + 0.5f);

        const float half_thick = w.thickness * 0.5f;
        const int min_x = std::max(0, static_cast<int>(std::floor(w.x - w.radius - half_thick)));
        const int max_x = std::min(canvas_width - 1,
                                   static_cast<int>(std::ceil(w.x + w.radius + half_thick)));
        const int min_y = std::max(0, static_cast<int>(std::floor(w.y - w.radius - half_thick)));
        const int max_y = std::min(canvas_height - 1,
                                   static_cast<int>(std::ceil(w.y + w.radius + half_thick)));

        for (int py = min_y; py <= max_y; ++py) {
            for (int px = min_x; px <= max_x; ++px) {
                const float dx = static_cast<float>(px) + 0.5f - w.x;
                const float dy = static_cast<float>(py) + 0.5f - w.y;
                const float dist = std::sqrt(dx * dx + dy * dy);
                if (std::fabs(dist - w.radius) > half_thick) {
                    continue;
                }
                const std::size_t index =
                    (static_cast<std::size_t>(py) * canvas_width + px) * 4;
                canvas[index] = r;
                canvas[index + 1] = g;
                canvas[index + 2] = b;
                canvas[index + 3] = 255;
            }
        }
    }
}

// ==================== 气泡拖尾（物理球，仅显示层） ====================
// 物理球（左侧机械区每队 4 个大球）移动时从球尾冒出小气泡：空心圆，上浮并逐渐放大，
// 按生命淡出到背景色；不写入持久领土画布，不影响玩法/计费。
void SpawnPhysicsBubble(std::vector<Bubble> &bubbles, float x, float y,
                        const SDL_FColor &color)
{
    Bubble b;
    b.x = x + (RandFloat() * 2.0f - 1.0f) * 2.0f;
    b.y = y + (RandFloat() * 2.0f - 1.0f) * 2.0f;
    b.vx = (RandFloat() * 2.0f - 1.0f) * 15.0f;      // 轻微横向漂移
    b.vy = -(20.0f + RandFloat() * 25.0f);           // 上浮
    b.max_life = 0.5f + RandFloat() * 0.4f;          // 0.5~0.9s
    b.life = b.max_life;
    b.radius = 1.2f + RandFloat() * 1.0f;
    b.color = color;
    bubbles.push_back(b);
    // 容量保护：气泡过多时丢弃最老的
    constexpr std::size_t MAX_BUBBLES = 1500;
    if (bubbles.size() > MAX_BUBBLES) {
        bubbles.erase(bubbles.begin(), bubbles.begin() + (bubbles.size() - MAX_BUBBLES));
    }
}

void UpdateBubbles(std::vector<Bubble> &bubbles, float dt)
{
    std::size_t write = 0;
    for (std::size_t i = 0; i < bubbles.size(); ++i) {
        Bubble &b = bubbles[i];
        b.life -= dt;
        if (b.life <= 0.0f) {
            continue;
        }
        b.x += b.vx * dt;
        b.y += b.vy * dt;
        bubbles[write++] = b;
    }
    bubbles.resize(write);
}

void DrawBubbles(std::vector<Uint8> &canvas, int canvas_width, int canvas_height,
                 const std::vector<Bubble> &bubbles)
{
    // 背景色（main.cpp 初始化画布用的底色），气泡淡出到它
    constexpr float BG_R = 15.0f / 255.0f;
    constexpr float BG_G = 15.0f / 255.0f;
    constexpr float BG_B = 25.0f / 255.0f;

    for (const Bubble &b : bubbles) {
        const float t = b.life / b.max_life;  // 1 → 0
        const float alpha = 0.5f * t;
        const float inv = 1.0f - alpha;
        SDL_FColor c;
        c.r = b.color.r * alpha + BG_R * inv;
        c.g = b.color.g * alpha + BG_G * inv;
        c.b = b.color.b * alpha + BG_B * inv;
        c.a = 1.0f;
        // 气泡上升时逐渐放大（越老越大），再随生命淡出
        const float radius = b.radius * (1.0f + (1.0f - t) * 1.8f);
        DrawHollowCircle(canvas, canvas_width, canvas_height, b.x, b.y, radius, c, 1.0f);
    }
}

// ==================== 领土新增闪光（仅显示层） ====================
// 渲染逐像素领土闪光并递减寿命（每帧调用一次，单次遍历战场缓冲）：
// 闪光像素按剩余寿命 alpha-lerp 向白色提亮，寿命归零后恢复领土原色。
// 点亮时机在涂画侧（canvas.cpp 的 PaintCircleFlash）：
// 像素从"非本队色"翻成"本队色"的那一刻记满寿命。

void UpdateAndDrawTerritoryFlash(std::vector<Uint8> &display_canvas,
                                 std::vector<Uint8> &territory_flash)
{
    constexpr int ORIGIN_X = static_cast<int>(FRAME_X);
    for (int y = 0; y < TERRITORY_FLASH_SIZE; ++y) {
        std::size_t fidx = static_cast<std::size_t>(y) * TERRITORY_FLASH_SIZE;
        std::size_t didx =
            (static_cast<std::size_t>(y) * WINDOW_WIDTH + ORIGIN_X) * 4;
        for (int x = 0; x < TERRITORY_FLASH_SIZE; ++x, ++fidx, didx += 4) {
            const int f = territory_flash[fidx];
            if (f <= 0) {
                continue;
            }
            const float alpha = 0.5f * static_cast<float>(f) /
                                static_cast<float>(TERRITORY_FLASH_FRAMES);
            const float inv = 1.0f - alpha;
            display_canvas[didx] =
                static_cast<Uint8>(255.0f * alpha + display_canvas[didx] * inv);
            display_canvas[didx + 1] =
                static_cast<Uint8>(255.0f * alpha + display_canvas[didx + 1] * inv);
            display_canvas[didx + 2] =
                static_cast<Uint8>(255.0f * alpha + display_canvas[didx + 2] * inv);
            territory_flash[fidx] = static_cast<Uint8>(f - 1);
        }
    }
}
