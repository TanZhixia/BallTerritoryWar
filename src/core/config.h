#ifndef BTW_CORE_CONFIG_H
#define BTW_CORE_CONFIG_H

#include <string>
#include <vector>

// ==================== 游戏配置（~/.ball/config.yaml） ====================
// 所有可调平衡参数集中在 GameConfig；LoadConfig() 从 YAML 加载，缺省用内置默认值。

struct PaintBallsConfig
{
    float speed = 120.0f;              // 画笔球速度
    float initialValue = 15.0f;        // 球初始价值
    float deleteValue = 0.0f;          // 价值 <= 此值删除
    float radius = 2.0f;               // 普通球半径
    float pixelCostBase = 1.0f;        // 涂画基础消耗
    float pixelCostPerMinute = 3.0f;   // 涂画消耗随时间增长：每 N 分钟 +1
    float pixelCostMax = 16.0f;        // 涂画消耗上限
};

struct PhysicsConfig
{
    int countPerColor = 4;             // 每队物理球数量
    float initialValue = 1000.0f;      // 物理球初始价值
    float radius = 10.0f;              // 物理球半径
    float gravity = 200.0f;            // 重力
    float launchSpeed = 120.0f;        // 重置/发射速度
    float restitution = 1.0f;          // 反弹系数（1 = 无损耗）
};

struct LiftConfig
{
    // 升力阈值只按 growthPerSecond 无限增长，没有上限
    float initialThreshold = 100000.0f;   // 缺口升力初始阈值
    float growthPerSecond = 0.01f;        // 阈值每秒增长率（1%）
    float acceleration = 1200.0f;         // 升力加速度
    float weaponInitial[3] = {50000.0f, 80000.0f, 100000.0f};  // *8 *4 *2 升力初始阈值
    float highValueLimit = 2000000.0f;    // 前期限制：超过此价值的球在霰弹/狙击列被顶回
};

struct MachineGunConfig
{
    float initialAmmo = 250000.0f;     // 每队初始弹药
    float drainDivisor = 12000.0f;     // 弹药消耗 = ammo / 此值（每帧）
    float minBallValue = 20.0f;        // 单发子弹最小价值
    float maxBallsPerFrame = 10.0f;    // 每帧每色最多发射数
    float speed = 120.0f;              // 子弹速度
    float spreadDeg = 4.0f;            // 散射总角度
    float rotateDegPerFrame = 0.5f;    // 无目标时炮管旋转速度（度/帧）
    float lockDistance = 300.0f;       // 锁定敌方大球的距离
};

struct ShotgunConfig
{
    float fragmentValue = 50.0f;       // 每份价值（value / 此值 = 碎片数）
    int maxFragments = 1000;           // 碎片数上限
    float spreadDeg = 4.0f;            // 散射总角度
    float speed = 120.0f;              // 碎片速度
};

struct SniperConfig
{
    float explosionValue = 50.0f;      // 爆炸碎片每份价值
    int maxFragments = 1000;           // 爆炸碎片数上限
    float speed = 120.0f;              // 狙击/碎片速度
    float jitter = 3.0f;               // 飞行抖动幅度（像素，仅显示层）
    int particlesPerFrame = 3;         // 飞行时每帧生成的粒子数
    float selfDestructChance = 0.01f;  // 每秒自爆概率（不稳定，0 = 关闭）
    float dotTrailLife = 0.25f;        // 光点拖尾时长（秒，0 = 关闭，仅显示层）
    float gravityRadius = 150.0f;      // 引力吸引半径（像素）
    float gravityStrength = 4.0f;      // 引力常数 G（a = G * 质量 / r^2）
    float gravityMaxSpeed = 600.0f;    // 被吸引球的速度上限（防失控）
    float gravityValueGain = 1.0f;     // 引力场内的“小球”每秒 +value（0 = 关闭）
};

struct BigBallConfig
{
    float radiusLogOffset = 5.0f;      // 大球半径 = (log(value) + 此值) × 2
    float radiusMin = 2.0f;            // 大球半径下限
    float speedFactor = 0.5f;          // 大球速度 = 画笔球速度 × 此值
    float trailValue = 20.0f;          // 拖尾小球价值（每帧扣大球此值）
    float gravityRadius = 260.0f;      // 大球引力半径（像素）
    float gravityStrength = 0.5f;      // 敌对大球之间的引力常数（a = G × value / r²）
    float gravityMaxSpeed = 480.0f;    // 被吸引球速度上限（防高价值大球互吸失控）
    // 狙击爆炸碎片被大球吸引的引力常数（a = G × 大球 value / r²，0 = 关闭；
    // 同色（己方）大球不吸引碎片；复用上面的引力半径与速度上限）
    float fragmentGravityStrength = 0.5f;
    // 大球每多少秒分裂成两个 value/2 的大球（0 = 关闭）
    float splitIntervalSeconds = 60.0f;
};

struct ShieldConfig
{
    float initialValue = 10000000.0f;  // 每队初始护盾
    float radius = 80.0f;              // 护盾圆半径
};

struct CombatConfig
{
    float absorbRatio = 20.0f;         // 价值差 ≥ 此倍数直接吞并
    float damageRatio = 0.1f;          // 否则互扣大者价值的此比例
};

struct GameOverConfig
{
    int countdownFrames = 3600;        // 结束倒计时帧数（60fps 下 60 秒）
};

struct StartupConfig
{
    float openingShotgunValue = 2500000.0f;  // 开局每队向中心发射的霰弹总价值
};

struct GameConfig
{
    PaintBallsConfig paintBalls;
    PhysicsConfig physics;
    LiftConfig lift;
    MachineGunConfig machineGun;
    ShotgunConfig shotgun;
    SniperConfig sniper;
    BigBallConfig bigBall;
    ShieldConfig shield;
    CombatConfig combat;
    GameOverConfig gameOver;
    StartupConfig startup;
};

extern GameConfig g_config;  // 启动时由 LoadConfig() 填充
bool LoadConfig();           // 从 ~/.ball/config.yaml 加载；不存在则生成默认文件
// 按点分键（如 "paintBalls.speed" / "lift.weaponInitial.0"）实时应用单个配置项，
// 供遥测 socket 的 SETCONFIG 命令调用；成功返回 true。
bool ApplyConfigKey(const std::string &key, const std::vector<std::string> &values,
                    std::string &error);

#endif  // BTW_CORE_CONFIG_H
