#ifndef BTW_GAME_PHYSICS_H
#define BTW_GAME_PHYSICS_H

#include <SDL3/SDL.h>

#include <vector>

#include "core/effects.h"
#include "core/entities.h"

// ==================== 物理与战斗 ====================
// 左侧机械区的物理球循环（重力/挡板/倍率带/武器格）、机枪 AI、
// 战场实体（大球/狙击）的引力、碰撞与吞并规则。

// 大球半径 = (log(value) + radiusLogOffset) × 2（下限 radiusMin）
float BigBallRadius(float value);

// 狙击爆炸：按 explosionValue 把价值切成最多 maxFragments 份，360° 飞散
void SpawnSniperExplosion(std::vector<BallObject> &spawns, const BallObject &sniper);
// 狙击引力场：吸引半径内所有非狙击球（含大球），按 a = G*M/r^2 加速，狙击自身不动；
// 同时给半径内的“小球”（非大球、非狙击）每秒 +sniper.gravityValueGain 价值
void ApplySniperGravity(std::vector<BallObject> &balls, float dt);
// 大球引力场：敌对大球之间、以及狙击爆炸碎片，都按大球 value 线性吸引
//（a = G × value / r²；同色不吸、己方大球不吸引自己的碎片；速度上限 gravityMaxSpeed 兜底）
void ApplyBigBallGravity(std::vector<BallObject> &balls, const SDL_FColor *pure_colors, float dt);

extern int g_next_big_ball_id;
extern bool g_weapon_lift_enabled;  // *8/*4/*2 及霰弹/狙击列升力开关（--no-weapon-lift 关闭，中间升力保留）

// 推进左侧机械区一帧：升力/重力/挡板/倍率带/五种武器格/机枪发射
// revival_wait：各颜色复活等待倒计时（秒）。>0 的颜色处于"死亡等待复活"状态，
// 其物理球落入任意武器格都不发射武器，而是把价值转化为该队护盾（可传 nullptr）
void UpdatePhysicsBalls(std::vector<PhysicsBall> &balls, float dt,
                        std::vector<BallObject> &paint_balls,
                        const SDL_FColor *pure_colors,
                        const SDL_FColor *new_colors,
                        float machine_gun_ammo[4],
                        float machine_gun_angle[4],
                        const std::vector<StaticCircle> &static_circles,
                        float &lift_threshold,
                        const float weapon_lift_thresholds[3],
                        float shield_remaining[4],
                        bool high_value_lift_enabled,
                        std::vector<Shockwave> &waves,
                        const float revival_wait[4] = nullptr);

// 推进"复活飞行"：先原地停顿 stopSeconds，再以 revive.flightSpeed 飞向炮塔；
// 返回 true 表示本帧到达炮塔（调用方据此执行复活流程）
bool UpdateRevivingBall(PhysicsBall &ball, float dt);

// 在显示画布上画物理球（圆 + 价值文字）；复活飞行中的球跳过（由
// DrawRevivingPhysicsBalls 画在最上层，飞行过程更醒目）
void DrawPhysicsBalls(std::vector<Uint8> &canvas, int canvas_width, int canvas_height,
                      const std::vector<PhysicsBall> &balls,
                      const SDL_FColor *pure_colors);
// 只画"复活飞行中"的物理球（画在显示层最上方）
void DrawRevivingPhysicsBalls(std::vector<Uint8> &canvas, int canvas_width, int canvas_height,
                              const std::vector<PhysicsBall> &balls);
// 战场实体的碰撞与伤害结算（护盾吸收 / 大球-大球 / 大球-小球 / 小-小空间网格）
void ResolvePaintBallCollisions(std::vector<BallObject> &balls,
                                float shield_remaining[4],
                                const SDL_FColor *pure_colors);

#endif  // BTW_GAME_PHYSICS_H
