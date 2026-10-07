#include "game/simulation.h"

#include "core/constants.h"
#include "core/math_utils.h"
#include "core/palette.h"
#include "game/physics.h"
#include "game/scene.h"
#include "render/canvas.h"
#include "render/particles.h"

#include <algorithm>
#include <cmath>

// 初始化实体与队伍状态（球/物理球/弹药/护盾/开局霰弹）
void InitializeGame(GameState &state)
{
    state.balls.reserve(BALL_COUNT);

    // 独立物理小球：每种旧颜色 4 个，初始在 (100,300) 附近
    state.physics_balls.reserve(16);
    std::srand(static_cast<unsigned int>(SDL_GetTicks()));
    for (int color = 0; color < 4; ++color) {
        for (int i = 0; i < g_config.physics.countPerColor; ++i) {
            PhysicsBall ball = {};
            ball.x = 300.0f + (i % 4) * 4.0f - 6.0f;
            ball.y = 100.0f + (i / 4.0f) * 4.0f - 6.0f;
            const float angle = RandFloat() * 2.0f * static_cast<float>(M_PI);
            ball.vx = std::cos(angle) * g_config.physics.launchSpeed;
            ball.vy = std::sin(angle) * g_config.physics.launchSpeed;
            ball.radius = PhysicsBallRadius(ball.value, g_config.lift.initialThreshold);
            ball.value = g_config.physics.initialValue;
            ball.color = PURE_COLORS[color];
            state.physics_balls.push_back(ball);
        }
    }

    for (int color = 0; color < 4; ++color) {
        state.machine_gun_ammo[color] = g_config.machineGun.initialAmmo;
        state.shield_remaining[color] = g_config.shield.initialValue;
        state.color_alive[color] = true;
    }
    state.lift_threshold = g_config.lift.initialThreshold;
    for (int i = 0; i < 3; ++i) {
        state.weapon_lift_thresholds[i] = g_config.lift.weaponInitial[i];
    }

    // 开局每队向 frame 中心发射一个 250 万霰弹（分成 1000 发 × 2500）
    {
        constexpr float CENTER_X = 1100.0f;
        constexpr float CENTER_Y = 500.0f;
        const float opening_value = g_config.startup.openingShotgunValue;
        const int count = std::max(
            1, std::min(static_cast<int>(opening_value / g_config.shotgun.fragmentValue),
                        g_config.shotgun.maxFragments));
        const float ball_value = opening_value / static_cast<float>(count);
        for (int color = 0; color < 4; ++color) {
            float block_x = 0.0f, block_y = 0.0f;
            GetColorBlockCenter(color, block_x, block_y);
            const float base_angle = std::atan2(CENTER_Y - block_y, CENTER_X - block_x);
            for (int i = 0; i < count; ++i) {
                const float random_offset_deg =
                    RandFloat() * g_config.shotgun.spreadDeg -
                    g_config.shotgun.spreadDeg * 0.5f;
                const float angle =
                    base_angle + random_offset_deg * static_cast<float>(M_PI) / 180.0f;
                state.balls.emplace_back(
                    block_x,
                    block_y,
                    g_config.shotgun.speed,
                    angle,
                    g_config.paintBalls.radius,
                    PURE_COLORS[color],
                    NEW_COLORS[color],
                    ball_value);
            }
        }
    }

    state.territory_flash.assign(
        static_cast<std::size_t>(TERRITORY_FLASH_SIZE) * TERRITORY_FLASH_SIZE, 0);
    state.old_paints.reserve(BALL_COUNT);
    state.new_paints.reserve(BALL_COUNT);

    state.game_start_time = SDL_GetTicks();
}

// 一帧模拟：涂画/移动、引力、碰撞、基地占领、胜负判定、物理区、遥测。
// 返回 false 表示本局结束（调用方应退出主循环）。
bool StepGame(GameState &state, std::vector<Uint8> &canvas,
              std::vector<Uint8> &canvas_snapshot, TelemetryState &telemetry)
{
    const std::size_t canvas_size = canvas.size();
    ++state.frame_count;

    const float elapsed_minutes =
        static_cast<float>(SDL_GetTicks() - state.game_start_time) / 60000.0f;
    const int pixel_cost = std::min(
        static_cast<int>(g_config.paintBalls.pixelCostMax),
        static_cast<int>(g_config.paintBalls.pixelCostBase) +
            static_cast<int>(elapsed_minutes / g_config.paintBalls.pixelCostPerMinute));


    // 狙击引力场：吸引半径内所有球（含大球）绕狙击旋转，狙击自身不动
    ApplySniperGravity(state.balls, 1.0f / 60.0f);
    // 大球引力场：大球间按 sqrt(value) 质量互相吸引
    ApplyBigBallGravity(state.balls, PURE_COLORS, 1.0f / 60.0f);

    // 大球半径实时刷新：随当前 value 实时变化（发射后持续更新；死亡动画除外）
    for (BallObject &ball : state.balls) {
        if (ball.is_big && !ball.dying) {
            ball.radius = BigBallRadius(ball.value);
        }
    }

    for (int step = 0; step < 2; ++step) {
        SDL_memcpy(canvas_snapshot.data(), canvas.data(), canvas_size);
        state.old_paints.clear();
        state.new_paints.clear();

        for (auto it = state.balls.begin(); it != state.balls.end();) {
            if (it->dying) {
                ++it;  // 死亡动画：原地缩小消失（不移动、不涂画、不计费）
                continue;
            }

            it->value -= static_cast<float>(CountPixelsToPaint(
                canvas_snapshot, WINDOW_WIDTH, WINDOW_HEIGHT, it->x, it->y,
                it->radius, it->old_color)) * pixel_cost;

            if (it->value <= g_config.paintBalls.deleteValue) {
                it = state.balls.erase(it);
                continue;
            }

            state.old_paints.push_back(PendingPaint{it->x, it->y, it->radius, it->old_color});
            it->Update(1.0f / 60.0f, FRAME_X, FRAME_Y,
                       FRAME_X + FRAME_SIZE, FRAME_Y + FRAME_SIZE);
            state.new_paints.push_back(PendingPaint{it->x, it->y, it->radius, it->new_color});
            ++it;
        }

        for (const PendingPaint &paint : state.old_paints) {
            PaintCircleFlash(canvas, state.territory_flash, WINDOW_WIDTH, WINDOW_HEIGHT,
                             paint.x, paint.y, paint.radius, paint.color);
        }
    }

    // 大球拖尾：每帧在运动方向垂直直径上随机 1 个点，向反方向发射 value=20 的小球
    std::vector<BallObject> trail_spawns;
    for (BallObject &big : state.balls) {
        if (!big.is_big || big.dying || big.value <= 0.0f) {
            continue;
        }
        const float speed = std::sqrt(big.vx * big.vx + big.vy * big.vy);
        if (speed < 0.0001f) {
            continue;
        }
        const float dir_x = big.vx / speed;
        const float dir_y = big.vy / speed;
        const float perp_x = -dir_y;
        const float perp_y = dir_x;
        const float t = (RandFloat() * 2.0f - 1.0f) * big.radius;
        trail_spawns.emplace_back(
            big.x + perp_x * t,
            big.y + perp_y * t,
            g_config.paintBalls.speed,
            std::atan2(-dir_y, -dir_x),  // 反方向
            g_config.paintBalls.radius,
            big.old_color,
            big.new_color,
            g_config.bigBall.trailValue,
            false,
            false,
            big.big_ball_id);
        big.value -= g_config.bigBall.trailValue;  // 与拖尾小球价值一致（原来硬编码 20）
        if (big.value < 0.0f) {
            big.value = 0.0f;
        }
        if (big.value <= 0.0f) {
            big.dying = true;
        }
    }
    for (const BallObject &spawn : trail_spawns) {
        state.balls.push_back(spawn);
    }

    // 大球分裂：每个大球每 bigBall.splitIntervalSeconds 秒分裂成两个 value/2 的大球
    //（同位置生成、沿运动方向的垂直方向分离；子球重新计时，0 = 关闭）
    if (g_config.bigBall.splitIntervalSeconds > 0.0f) {
        std::vector<BallObject> split_spawns;
        std::vector<char> split_parent(state.balls.size(), 0);
        bool any_split = false;
        for (std::size_t i = 0; i < state.balls.size(); ++i) {
            BallObject &big = state.balls[i];
            if (!big.is_big || big.dying || big.value <= 0.0f) {
                continue;
            }
            big.age += 1.0f / 60.0f;
            if (big.age < g_config.bigBall.splitIntervalSeconds) {
                continue;
            }
            // 分离方向：运动方向的垂直方向（静止时随机）
            const float speed = std::sqrt(big.vx * big.vx + big.vy * big.vy);
            float nx = 0.0f;
            float ny = 0.0f;
            if (speed > 0.0001f) {
                nx = -big.vy / speed;
                ny = big.vx / speed;
            } else {
                const float angle = RandFloat() * 2.0f * static_cast<float>(M_PI);
                nx = std::cos(angle);
                ny = std::sin(angle);
            }
            const float kick = g_config.paintBalls.speed * g_config.bigBall.speedFactor;
            const float half_value = big.value * 0.5f;
            for (int k = 0; k < 2; ++k) {
                const float dir = (k == 0) ? 1.0f : -1.0f;
                BallObject child = big;  // 继承颜色等属性
                const float offset = big.radius * 0.5f;  // 沿分离方向错开一点，避免完全重叠
                child.x = big.x + nx * offset * dir;
                child.y = big.y + ny * offset * dir;
                child.vx = big.vx + nx * kick * dir;
                child.vy = big.vy + ny * kick * dir;
                child.value = half_value;
                child.radius = BigBallRadius(half_value);
                child.is_big = true;
                child.is_sniper = false;
                child.is_fragment = false;
                child.dying = false;
                child.big_ball_id = g_next_big_ball_id++;
                child.age = 0.0f;
                split_spawns.push_back(child);
            }
            split_parent[i] = 1;
            any_split = true;
        }
        if (any_split) {
            // 父球被两个子球取代：原地移除（压缩写法，避免遍历中修改容器）
            std::size_t write = 0;
            for (std::size_t i = 0; i < state.balls.size(); ++i) {
                if (split_parent[i] == 0) {
                    state.balls[write++] = state.balls[i];
                }
            }
            while (state.balls.size() > write) {
                state.balls.pop_back();
            }
            for (const BallObject &child : split_spawns) {
                state.balls.push_back(child);
            }
        }
    }

    // 狙击不稳定：每秒 selfDestructChance 概率自爆（提前爆炸，与碰撞爆炸相同）
    {
        const float chance_per_frame =
            1.0f - std::pow(1.0f - g_config.sniper.selfDestructChance, 1.0f / 60.0f);
        if (chance_per_frame > 0.0f) {
            std::vector<std::size_t> explode;
            for (std::size_t i = 0; i < state.balls.size(); ++i) {
                const BallObject &ball = state.balls[i];
                if (!ball.is_sniper || ball.dying) {
                    continue;
                }
                if (RandFloat() < chance_per_frame) {
                    explode.push_back(i);
                }
            }
            if (!explode.empty()) {
                std::vector<BallObject> explosion_spawns;
                for (std::size_t i : explode) {
                    SpawnSniperExplosion(explosion_spawns, state.balls[i]);
                }
                // 移除已自爆的狙击，追加碎片（与碰撞解析同款压缩写法）
                std::vector<char> dead(state.balls.size(), 0);
                for (std::size_t i : explode) {
                    dead[i] = 1;
                }
                std::size_t write = 0;
                for (std::size_t i = 0; i < state.balls.size(); ++i) {
                    if (!dead[i]) {
                        state.balls[write++] = state.balls[i];
                    }
                }
                while (state.balls.size() > write) {
                    state.balls.pop_back();
                }
                for (BallObject &spawn : explosion_spawns) {
                    state.balls.push_back(spawn);
                }
            }
        }
    }

    ResolvePaintBallCollisions(state.balls, state.shield_remaining, PURE_COLORS);

    // 大球死亡动画：原地慢慢缩小到 r=0 后删除（清掉残留速度，避免边飞边缩小）
    for (auto it = state.balls.begin(); it != state.balls.end();) {
        if (!it->dying) {
            ++it;
            continue;
        }
        it->vx = 0.0f;
        it->vy = 0.0f;
        it->radius -= 1.0f;
        if (it->radius <= 0.0f) {
            it = state.balls.erase(it);
        } else {
            ++it;
        }
    }

    // 基地占领判定：发射点既不是旧色也不是新色 -> 该颜色物理球转化为大球发射，基地死亡
    for (int color = 0; color < 4; ++color) {
        if (!state.color_alive[color]) {
            continue;
        }
        float center_x = 0.0f, center_y = 0.0f;
        GetColorBlockCenter(color, center_x, center_y);
        const int pixel_x = static_cast<int>(center_x);
        const int pixel_y = static_cast<int>(center_y);
        bool captured = false;
        for (int dy = -10; dy < 10 && !captured; ++dy) {
            for (int dx = -10; dx < 10; ++dx) {
                const int check_x = pixel_x + dx;
                const int check_y = pixel_y + dy;
                const bool is_old = CanvasPixelMatches(
                    canvas, WINDOW_WIDTH, check_x, check_y, PURE_COLORS[color]);
                const bool is_new = CanvasPixelMatches(
                    canvas, WINDOW_WIDTH, check_x, check_y, NEW_COLORS[color]);
                if (!is_old && !is_new) {
                    captured = true;
                    break;
                }
            }
        }
        if (captured) {
            state.machine_gun_ammo[color] = 0.0f;  // 该颜色机枪也被删除
            state.color_alive[color] = false;

            // 该色剩余物理球全部转化为大球，从基地射出
            for (const PhysicsBall &pb : state.physics_balls) {
                if (FindPhysicsColorIndex(pb, PURE_COLORS) != color) {
                    continue;
                }
                const float angle = RandFloat() * 2.0f * static_cast<float>(M_PI);
                state.balls.emplace_back(
                    center_x,
                    center_y,
                    g_config.paintBalls.speed * g_config.bigBall.speedFactor,
                    angle,
                    BigBallRadius(pb.value),
                    PURE_COLORS[color],
                    NEW_COLORS[color],
                    pb.value,
                    true,               // 大球
                    false,              // 不是狙击
                    g_next_big_ball_id++);
            }
            // 原物理球移除（已转化为大球）
            state.physics_balls.erase(
                std::remove_if(
                    state.physics_balls.begin(), state.physics_balls.end(),
                    [color](const PhysicsBall &ball) {
                        return FindPhysicsColorIndex(ball, PURE_COLORS) == color;
                    }),
                state.physics_balls.end());
        }
    }

    int alive_count = 0;
    for (int color = 0; color < 4; ++color) {
        if (state.color_alive[color]) {
            ++alive_count;
        }
    }

    // 场上是否还有已灭颜色的大球（基地死亡转化 / 战斗残留）：
    // 有则必须等它们全部死亡后才开始结束倒计时（它们仍可能反杀剩余基地）
    bool dead_color_big_exists = false;
    for (const BallObject &ball : state.balls) {
        if (!ball.is_big || ball.dying) {
            continue;  // 死亡动画中的大球已无威胁，不阻塞倒计时
        }
        if (!state.color_alive[FindPaintColorIndex(ball, PURE_COLORS)]) {
            dead_color_big_exists = true;
            break;
        }
    }

    if (alive_count <= 1 && !dead_color_big_exists && !state.game_over_started) {
        state.game_over_started = true;
        state.game_over_start_frame = state.frame_count;
        SDL_Log("One color remains (or all captured) and no enemy big balls, "
                "exiting in %d frames", g_config.gameOver.countdownFrames);
    }
    if (state.game_over_started &&
        state.frame_count - state.game_over_start_frame >=
            static_cast<Uint64>(std::max(1, g_config.gameOver.countdownFrames))) {
        SDL_Log("Game over, exiting");
        return false;
    }
    // 运行时长/帧数上限：--duration 秒 / --max-frames 帧，到时自动退出（正常收尾录像）
    if (state.run_duration_seconds > 0.0f &&
        static_cast<float>(SDL_GetTicks() - state.game_start_time) / 1000.0f >=
            state.run_duration_seconds) {
        SDL_Log("Reached run duration %.1fs, exiting", state.run_duration_seconds);
        return false;
    }
    if (state.max_frames > 0 && state.frame_count >= static_cast<Uint64>(state.max_frames)) {
        SDL_Log("Reached max frames %d, exiting", state.max_frames);
        return false;
    }

    // 升力阈值按 growthPerSecond 每秒复利增长，没有上限
    const float lift_growth =
        std::pow(1.0f + g_config.lift.growthPerSecond, 1.0f / 60.0f);
    state.lift_threshold *= lift_growth;
    for (float &threshold : state.weapon_lift_thresholds) {
        threshold *= lift_growth;
    }
    UpdatePhysicsBalls(state.physics_balls, 1.0f / 60.0f, state.balls,
                      PURE_COLORS, NEW_COLORS,
                      state.machine_gun_ammo, state.machine_gun_angle, state.blocking_circles,
                      state.lift_threshold, state.weapon_lift_thresholds, state.shield_remaining,
                      elapsed_minutes < 10.0f, state.shockwaves);

    // 物理球气泡拖尾：value 达到中间升力阈值（state.lift_threshold）的球冒气泡（仅显示层）
    for (const PhysicsBall &pb : state.physics_balls) {
        if (pb.value <= 0.0f || pb.value < state.lift_threshold) {
            continue;
        }
        // 沿运动反向偏移一点生成，形成拖尾感；每球每帧 2 个，保证拖尾密度
        SpawnPhysicsBubble(state.bubbles, pb.x - pb.vx * 0.02f, pb.y - pb.vy * 0.02f, pb.color);
        SpawnPhysicsBubble(state.bubbles, pb.x - pb.vx * 0.02f, pb.y - pb.vy * 0.02f, pb.color);
    }

    // 每 30 帧（0.5s）采样领土（HUD 与遥测共用一次），采集遥测推送给已连接的启动器
    if (state.frame_count % 30 == 0) {
        SampleTerritory(canvas, PURE_COLORS, state.hud_territory);
        CollectTelemetry(state.balls, state.physics_balls, state.hud_territory, PURE_COLORS,
                         state.shield_remaining, state.machine_gun_ammo, state.color_alive,
                         elapsed_minutes, telemetry);
    }
    return true;
}
