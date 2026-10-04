// ==================== 配置文件（~/.ball/config.yaml） ====================
//
// 极简 YAML 子集解析器：不依赖第三方库。
// 支持：'#' 注释、缩进嵌套映射（key: value）、标量（数字/布尔/字符串，可带引号）、
//       块序列（- item）
// 不支持：制表符缩进、锚点/别名、流式集合（[a, b]）、多行字符串 —— 遇到会报错并回退默认值。
//
// 修改配置后重启游戏生效；删除文件可恢复默认配置。

#include "core/config.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

GameConfig g_config;

namespace {

struct YamlNode
{
    enum class Type { Scalar, Map, Seq };
    Type type = Type::Scalar;
    std::string scalar;                              // Scalar 值原文
    std::vector<std::pair<std::string, YamlNode>> map;  // Map 条目
    std::vector<YamlNode> seq;                       // Seq 条目
};

std::string YamlTrim(const std::string &s)
{
    const auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && is_space(s[b])) {
        ++b;
    }
    while (e > b && is_space(s[e - 1])) {
        --e;
    }
    return s.substr(b, e - b);
}

// 去掉行尾注释（'#' 在引号内时不处理）
std::string YamlStripComment(const std::string &s)
{
    std::string out;
    char quote = 0;
    for (char c : s) {
        if (quote) {
            out += c;
            if (c == quote) {
                quote = 0;
            }
        } else if (c == '\'' || c == '"') {
            quote = c;
            out += c;
        } else if (c == '#') {
            break;
        } else {
            out += c;
        }
    }
    return out;
}

// 计算缩进；出现制表符返回 -1（不允许）
int YamlIndent(const std::string &line)
{
    int n = 0;
    for (char c : line) {
        if (c == ' ') {
            ++n;
        } else if (c == '\t') {
            return -1;
        } else {
            break;
        }
    }
    return n;
}

std::string YamlUnquote(const std::string &s)
{
    if (s.size() >= 2 && s.front() == s.back() &&
        (s.front() == '\'' || s.front() == '"')) {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

struct YamlCursor
{
    const std::vector<std::string> &lines;
    std::size_t index = 0;
};

// 跳到下一个有效行（跳过空行/纯注释行）。
// 返回 false 表示：到达文件尾、或下一个有效行缩进 < indent（本层级结束）、或解析错误。
bool YamlSkipTo(YamlCursor &cur, int indent, std::string &error)
{
    while (cur.index < cur.lines.size()) {
        const std::string t = YamlTrim(YamlStripComment(cur.lines[cur.index]));
        if (t.empty()) {
            ++cur.index;
            continue;
        }
        const int line_indent = YamlIndent(cur.lines[cur.index]);
        if (line_indent < 0) {
            error = "第 " + std::to_string(cur.index + 1) + " 行使用了制表符缩进（不允许）";
            return false;
        }
        if (line_indent < indent) {
            return false;  // 父级内容结束
        }
        if (line_indent > indent) {
            error = "第 " + std::to_string(cur.index + 1) + " 行缩进跳级（意外缩进）";
            return false;
        }
        return true;
    }
    return false;
}

// 查看下一个有效行的实际缩进（不消费行）；无有效行返回 -1，制表符缩进报错。
int YamlNextIndent(YamlCursor &cur, std::string &error)
{
    YamlCursor peek = cur;
    while (peek.index < peek.lines.size()) {
        const std::string t = YamlTrim(YamlStripComment(peek.lines[peek.index]));
        if (t.empty()) {
            ++peek.index;
            continue;
        }
        const int indent = YamlIndent(peek.lines[peek.index]);
        if (indent < 0) {
            error = "第 " + std::to_string(peek.index + 1) + " 行使用了制表符缩进（不允许）";
            return -1;
        }
        return indent;
    }
    return -1;
}

// 解析缩进 == indent 的一个节点（映射或序列）；该层级无内容时返回 false（非错误）。
bool YamlParseNode(YamlCursor &cur, YamlNode &node, int indent, std::string &error);

// 用下一个有效行的实际缩进解析嵌套块（缩进比父级深时）；
// 否则返回 false（值视为空标量）。
bool YamlParseNested(YamlCursor &cur, YamlNode &node, int parent_indent, std::string &error)
{
    std::string peek_err;
    const int child_indent = YamlNextIndent(cur, peek_err);
    if (!peek_err.empty()) {
        error = peek_err;
        return false;
    }
    if (child_indent <= parent_indent) {
        return false;  // 无嵌套内容
    }
    if (!YamlParseNode(cur, node, child_indent, error)) {
        if (!error.empty()) {
            return false;
        }
        node.type = YamlNode::Type::Scalar;
        node.scalar.clear();
    }
    return true;
}

// 解析缩进 == indent 的一个节点（映射或序列）；该层级无内容时返回 false（非错误）。
bool YamlParseNode(YamlCursor &cur, YamlNode &node, int indent, std::string &error)
{
    node = YamlNode{};
    if (!YamlSkipTo(cur, indent, error)) {
        return false;
    }
    if (!error.empty()) {
        return false;
    }

    const std::string content = YamlTrim(YamlStripComment(cur.lines[cur.index]));

    if (content == "-" || content.rfind("- ", 0) == 0) {
        // ---------- 序列 ----------
        node.type = YamlNode::Type::Seq;
        while (true) {
            if (!YamlSkipTo(cur, indent, error)) {
                return error.empty();
            }
            if (!error.empty()) {
                return false;
            }
            const std::string c = YamlTrim(YamlStripComment(cur.lines[cur.index]));
            if (!(c == "-" || c.rfind("- ", 0) == 0)) {
                return true;  // 序列结束
            }
            const std::string rest = YamlTrim(YamlStripComment(c.substr(1)));
            ++cur.index;
            YamlNode item;
            if (rest.empty()) {
                // "- " 后无内容：可能是更深的嵌套块
                if (!YamlParseNested(cur, item, indent, error)) {
                    if (!error.empty()) {
                        return false;
                    }
                    item.type = YamlNode::Type::Scalar;
                    item.scalar.clear();
                }
            } else if (rest.find(':') != std::string::npos) {
                error = "序列中的映射（- key: value）暂不支持（第 " +
                        std::to_string(cur.index) + " 行附近）";
                return false;
            } else {
                item.type = YamlNode::Type::Scalar;
                item.scalar = YamlUnquote(rest);
            }
            node.seq.push_back(std::move(item));
        }
    }

    // ---------- 映射 ----------
    node.type = YamlNode::Type::Map;
    while (true) {
        if (!YamlSkipTo(cur, indent, error)) {
            return error.empty();
        }
        if (!error.empty()) {
            return false;
        }
        const std::string c = YamlTrim(YamlStripComment(cur.lines[cur.index]));
        if (c == "-" || c.rfind("- ", 0) == 0) {
            return true;  // 父级是序列，本映射结束
        }
        const std::size_t colon = c.find(':');
        if (colon == std::string::npos) {
            error = "第 " + std::to_string(cur.index + 1) + " 行无法解析（需要 key: value）：" + c;
            return false;
        }
        const std::string key = YamlTrim(c.substr(0, colon));
        const std::string rest = YamlTrim(YamlStripComment(c.substr(colon + 1)));
        ++cur.index;
        YamlNode value;
        if (rest.empty()) {
            // 空值：可能是更深的嵌套块（用实际缩进解析）
            if (!YamlParseNested(cur, value, indent, error)) {
                if (!error.empty()) {
                    return false;
                }
                value.type = YamlNode::Type::Scalar;
                value.scalar.clear();
            }
        } else {
            value.type = YamlNode::Type::Scalar;
            value.scalar = YamlUnquote(rest);
        }
        node.map.emplace_back(key, std::move(value));
    }
}

bool YamlToDouble(const std::string &s, double &out)
{
    if (s == "true" || s == "True" || s == "TRUE") {
        out = 1.0;
        return true;
    }
    if (s == "false" || s == "False" || s == "FALSE") {
        out = 0.0;
        return true;
    }
    if (s.empty()) {
        return false;
    }
    char *end = nullptr;
    out = std::strtod(s.c_str(), &end);
    return end != s.c_str() && *end == '\0';
}

const YamlNode *YamlFind(const YamlNode &node, const std::string &key)
{
    if (node.type != YamlNode::Type::Map) {
        return nullptr;
    }
    for (const auto &kv : node.map) {
        if (kv.first == key) {
            return &kv.second;
        }
    }
    return nullptr;
}

// 各字段加载辅助：缺失/格式错误 → 保留结构体默认值并记入 warnings
void YamlNum(const YamlNode &group, const char *key, float &target, std::string &warnings)
{
    const YamlNode *v = YamlFind(group, key);
    if (!v) {
        warnings += std::string(key) + " 缺失；";
        return;
    }
    double d = 0.0;
    if (v->type != YamlNode::Type::Scalar || !YamlToDouble(v->scalar, d)) {
        warnings += std::string(key) + " 格式错误；";
        return;
    }
    target = static_cast<float>(d);
}

void YamlInt(const YamlNode &group, const char *key, int &target, std::string &warnings)
{
    const YamlNode *v = YamlFind(group, key);
    if (!v) {
        warnings += std::string(key) + " 缺失；";
        return;
    }
    double d = 0.0;
    if (v->type != YamlNode::Type::Scalar || !YamlToDouble(v->scalar, d)) {
        warnings += std::string(key) + " 格式错误；";
        return;
    }
    target = static_cast<int>(d);
}

void YamlNumArray(const YamlNode &group, const char *key, float *target, int count,
                  std::string &warnings)
{
    const YamlNode *v = YamlFind(group, key);
    if (!v) {
        warnings += std::string(key) + " 缺失；";
        return;
    }
    if (v->type != YamlNode::Type::Seq || v->seq.size() != static_cast<std::size_t>(count)) {
        warnings += std::string(key) + " 应为 " + std::to_string(count) + " 项列表；";
        return;
    }
    for (int i = 0; i < count; ++i) {
        double d = 0.0;
        if (v->seq[static_cast<std::size_t>(i)].type != YamlNode::Type::Scalar ||
            !YamlToDouble(v->seq[static_cast<std::size_t>(i)].scalar, d)) {
            warnings += std::string(key) + " 第 " + std::to_string(i) + " 项格式错误；";
            return;
        }
        target[i] = static_cast<float>(d);
    }
}

const char *DefaultConfigYaml()
{
    return R"(# Ball Territory War 配置文件
# 修改后重启游戏生效；删除本文件可恢复默认配置。
# 支持语法：key: value、缩进嵌套、列表（- 开头）、# 注释；不要用制表符缩进。

paintBalls:
  speed: 120.0          # 画笔球速度
  initialValue: 15.0    # 球初始价值
  deleteValue: 0.0      # 价值 <= 此值删除
  radius: 2.0           # 普通球半径
  pixelCostBase: 1.0    # 涂画基础消耗
  pixelCostPerMinute: 3.0  # 涂画消耗随时间增长：每 N 分钟 +1
  pixelCostMax: 16.0    # 涂画消耗上限

physics:
  countPerColor: 4      # 每队物理球数量
  initialValue: 1000.0  # 物理球初始价值
  radius: 10.0          # 物理球半径
  gravity: 200.0        # 重力
  launchSpeed: 120.0    # 重置/发射速度
  restitution: 1.0      # 反弹系数（1 = 无损耗）

lift:
  initialThreshold: 100000.0   # 缺口升力初始阈值（无上限，按增长率一直增长）
  growthPerSecond: 0.01        # 阈值每秒增长率（0.01 = 1%）
  acceleration: 1200.0         # 升力加速度
  weaponInitial:
    - 50000.0                  # *8 升力初始阈值（无上限）
    - 80000.0                  # *4
    - 100000.0                 # *2
  highValueLimit: 2000000.0    # 前期(10分钟内)超过此价值的球在霰弹/狙击列被顶回

machineGun:
  initialAmmo: 250000.0    # 每队初始弹药
  drainDivisor: 12000.0    # 弹药消耗 = ammo / 此值（每帧）
  minBallValue: 20.0       # 单发子弹最小价值
  maxBallsPerFrame: 10.0   # 每帧每色最多发射数
  speed: 120.0             # 子弹速度
  spreadDeg: 4.0           # 散射总角度
  rotateDegPerFrame: 0.5   # 无目标时炮管旋转速度（度/帧）
  lockDistance: 300.0      # 锁定敌方大球的距离

shotgun:
  fragmentValue: 50.0      # 每份价值（value / 此值 = 碎片数）
  maxFragments: 1000       # 碎片数上限
  spreadDeg: 4.0           # 散射总角度
  speed: 120.0             # 碎片速度

sniper:
  explosionValue: 50.0     # 爆炸碎片每份价值
  maxFragments: 1000       # 爆炸碎片数上限
  speed: 120.0             # 狙击/碎片速度
  jitter: 3.0              # 飞行抖动幅度（像素，仅显示层）
  particlesPerFrame: 3     # 飞行时每帧生成的粒子数
  selfDestructChance: 0.01 # 每秒自爆概率（狙击不稳定，0 = 关闭）
  dotTrailLife: 0.25     # 光点拖尾时长（秒，0 = 关闭，仅显示层）
  gravityRadius: 150.0    # 引力吸引半径（像素）
  gravityStrength: 4.0    # 引力常数 G（a = G * 质量 / r^2，质量 = 狙击价值）
  gravityMaxSpeed: 600.0  # 被吸引球的速度上限（防失控）
  gravityValueGain: 1.0   # 引力场内的小球每秒 +value（0 = 关闭；大球与狙击不计）

bigBall:
  radiusLogOffset: 5.0     # 大球半径 = (log(value) + 此值) × 2
  radiusMin: 2.0           # 大球半径下限
  speedFactor: 0.5         # 大球速度 = 画笔球速度 × 此值
  trailValue: 20.0         # 拖尾小球价值（每帧扣大球此值）
  gravityRadius: 260.0     # 大球引力半径（像素，大球之间与吸引狙击碎片共用）
  gravityStrength: 0.5     # 大球之间的引力常数（质量 = sqrt(value)，避免高价值大球失控）
  gravityMaxSpeed: 480.0   # 被吸引球的速度上限
  fragmentGravityStrength: 0.5  # 狙击爆炸碎片被大球吸引的引力常数（a = G × 大球value / r²，0 = 关闭）
  splitIntervalSeconds: 60.0    # 大球每 N 秒分裂成两个 value/2 的大球（0 = 关闭）

shield:
  initialValue: 10000000.0  # 每队初始护盾
  radius: 80.0              # 护盾圆半径

combat:
  absorbRatio: 20.0         # 价值差 ≥ 此倍数直接吞并
  damageRatio: 0.1          # 否则互扣大者价值的此比例

gameOver:
  countdownFrames: 3600     # 结束倒计时帧数（60fps 下 60 秒）

startup:
  openingShotgunValue: 2500000.0  # 开局每队向中心发射的霰弹总价值

revive:
  minPhysicsBalls: 2       # 至少这么多物理球才触发复活（只剩 1 个不复活；0 = 关闭复活）
  delaySeconds: 60.0       # 死亡后等这么多秒才复活（等待期内武器格转护盾；0 = 立即复活）
  stopSeconds: 0.5         # 最大的物理球停止移动的停顿时间（秒）
  flightSpeed: 400.0       # 飞向炮塔的速度（像素/秒）
  shieldValueScale: 1.0    # 物理球价值 → 护盾值的倍率
)";
}

}  // namespace

bool LoadConfig()
{
    const char *home = SDL_GetUserFolder(SDL_FOLDER_HOME);
    if (!home || !home[0]) {
        home = std::getenv("HOME");
    }
    if (!home || !home[0]) {
        SDL_Log("config: 无法定位用户目录，使用默认配置");
        return false;
    }
    std::string home_str(home);
    while (!home_str.empty() && home_str.back() == '/') {
        home_str.pop_back();  // SDL_GetUserFolder 返回的 home 可能带尾斜杠
    }
    const std::string dir = home_str + "/.ball";
    const std::string path = dir + "/config.yaml";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    std::string text;
    if (!std::filesystem::exists(path)) {
        text = DefaultConfigYaml();
        FILE *f = std::fopen(path.c_str(), "w");
        if (f) {
            std::fwrite(text.data(), 1, text.size(), f);
            std::fclose(f);
            SDL_Log("config: 已生成默认配置文件 %s", path.c_str());
        }
    } else {
        FILE *f = std::fopen(path.c_str(), "rb");
        if (f) {
            char buf[65536];
            const std::size_t n = std::fread(buf, 1, sizeof(buf), f);
            std::fclose(f);
            text.assign(buf, n);
        }
    }

    // 拆行（保留空行，解析器会跳过）
    std::vector<std::string> lines;
    std::string current;
    for (char ch : text) {
        if (ch == '\n') {
            lines.push_back(current);
            current.clear();
        } else {
            current += ch;
        }
    }
    if (!current.empty()) {
        lines.push_back(current);
    }

    YamlNode root;
    std::string error;
    YamlCursor cursor{lines, 0};
    if (!YamlParseNode(cursor, root, 0, error)) {
        SDL_Log("config: 解析失败（%s），使用默认配置", error.c_str());
        return false;
    }
    if (root.type != YamlNode::Type::Map) {
        SDL_Log("config: 顶层必须是映射，使用默认配置");
        return false;
    }

    std::string warnings;

    if (const YamlNode *g = YamlFind(root, "paintBalls")) {
        YamlNum(*g, "speed", g_config.paintBalls.speed, warnings);
        YamlNum(*g, "initialValue", g_config.paintBalls.initialValue, warnings);
        YamlNum(*g, "deleteValue", g_config.paintBalls.deleteValue, warnings);
        YamlNum(*g, "radius", g_config.paintBalls.radius, warnings);
        YamlNum(*g, "pixelCostBase", g_config.paintBalls.pixelCostBase, warnings);
        YamlNum(*g, "pixelCostPerMinute", g_config.paintBalls.pixelCostPerMinute, warnings);
        YamlNum(*g, "pixelCostMax", g_config.paintBalls.pixelCostMax, warnings);
    } else {
        warnings += "paintBalls 组缺失；";
    }
    if (const YamlNode *g = YamlFind(root, "physics")) {
        YamlInt(*g, "countPerColor", g_config.physics.countPerColor, warnings);
        YamlNum(*g, "initialValue", g_config.physics.initialValue, warnings);
        YamlNum(*g, "radius", g_config.physics.radius, warnings);
        YamlNum(*g, "gravity", g_config.physics.gravity, warnings);
        YamlNum(*g, "launchSpeed", g_config.physics.launchSpeed, warnings);
        YamlNum(*g, "restitution", g_config.physics.restitution, warnings);
    } else {
        warnings += "physics 组缺失；";
    }
    if (const YamlNode *g = YamlFind(root, "lift")) {
        YamlNum(*g, "initialThreshold", g_config.lift.initialThreshold, warnings);
        YamlNum(*g, "growthPerSecond", g_config.lift.growthPerSecond, warnings);
        YamlNum(*g, "acceleration", g_config.lift.acceleration, warnings);
        YamlNumArray(*g, "weaponInitial", g_config.lift.weaponInitial, 3, warnings);
        YamlNum(*g, "highValueLimit", g_config.lift.highValueLimit, warnings);
    } else {
        warnings += "lift 组缺失；";
    }
    if (const YamlNode *g = YamlFind(root, "machineGun")) {
        YamlNum(*g, "initialAmmo", g_config.machineGun.initialAmmo, warnings);
        YamlNum(*g, "drainDivisor", g_config.machineGun.drainDivisor, warnings);
        YamlNum(*g, "minBallValue", g_config.machineGun.minBallValue, warnings);
        YamlNum(*g, "maxBallsPerFrame", g_config.machineGun.maxBallsPerFrame, warnings);
        YamlNum(*g, "speed", g_config.machineGun.speed, warnings);
        YamlNum(*g, "spreadDeg", g_config.machineGun.spreadDeg, warnings);
        YamlNum(*g, "rotateDegPerFrame", g_config.machineGun.rotateDegPerFrame, warnings);
        YamlNum(*g, "lockDistance", g_config.machineGun.lockDistance, warnings);
    } else {
        warnings += "machineGun 组缺失；";
    }
    if (const YamlNode *g = YamlFind(root, "shotgun")) {
        YamlNum(*g, "fragmentValue", g_config.shotgun.fragmentValue, warnings);
        YamlInt(*g, "maxFragments", g_config.shotgun.maxFragments, warnings);
        YamlNum(*g, "spreadDeg", g_config.shotgun.spreadDeg, warnings);
        YamlNum(*g, "speed", g_config.shotgun.speed, warnings);
    } else {
        warnings += "shotgun 组缺失；";
    }
    if (const YamlNode *g = YamlFind(root, "sniper")) {
        YamlNum(*g, "explosionValue", g_config.sniper.explosionValue, warnings);
        YamlInt(*g, "maxFragments", g_config.sniper.maxFragments, warnings);
        YamlNum(*g, "speed", g_config.sniper.speed, warnings);
        YamlNum(*g, "jitter", g_config.sniper.jitter, warnings);
        YamlInt(*g, "particlesPerFrame", g_config.sniper.particlesPerFrame, warnings);
        YamlNum(*g, "selfDestructChance", g_config.sniper.selfDestructChance, warnings);
        YamlNum(*g, "dotTrailLife", g_config.sniper.dotTrailLife, warnings);
        YamlNum(*g, "gravityRadius", g_config.sniper.gravityRadius, warnings);
        YamlNum(*g, "gravityStrength", g_config.sniper.gravityStrength, warnings);
        YamlNum(*g, "gravityMaxSpeed", g_config.sniper.gravityMaxSpeed, warnings);
        YamlNum(*g, "gravityValueGain", g_config.sniper.gravityValueGain, warnings);
    } else {
        warnings += "sniper 组缺失；";
    }
    if (const YamlNode *g = YamlFind(root, "bigBall")) {
        YamlNum(*g, "radiusLogOffset", g_config.bigBall.radiusLogOffset, warnings);
        YamlNum(*g, "radiusMin", g_config.bigBall.radiusMin, warnings);
        YamlNum(*g, "speedFactor", g_config.bigBall.speedFactor, warnings);
        YamlNum(*g, "trailValue", g_config.bigBall.trailValue, warnings);
        YamlNum(*g, "gravityRadius", g_config.bigBall.gravityRadius, warnings);
        YamlNum(*g, "gravityStrength", g_config.bigBall.gravityStrength, warnings);
        YamlNum(*g, "gravityMaxSpeed", g_config.bigBall.gravityMaxSpeed, warnings);
        YamlNum(*g, "fragmentGravityStrength", g_config.bigBall.fragmentGravityStrength,
                warnings);
        YamlNum(*g, "splitIntervalSeconds", g_config.bigBall.splitIntervalSeconds, warnings);
    } else {
        warnings += "bigBall 组缺失；";
    }
    if (const YamlNode *g = YamlFind(root, "shield")) {
        YamlNum(*g, "initialValue", g_config.shield.initialValue, warnings);
        YamlNum(*g, "radius", g_config.shield.radius, warnings);
    } else {
        warnings += "shield 组缺失；";
    }
    if (const YamlNode *g = YamlFind(root, "combat")) {
        YamlNum(*g, "absorbRatio", g_config.combat.absorbRatio, warnings);
        YamlNum(*g, "damageRatio", g_config.combat.damageRatio, warnings);
    } else {
        warnings += "combat 组缺失；";
    }
    if (const YamlNode *g = YamlFind(root, "gameOver")) {
        YamlInt(*g, "countdownFrames", g_config.gameOver.countdownFrames, warnings);
    } else {
        warnings += "gameOver 组缺失；";
    }
    if (const YamlNode *g = YamlFind(root, "revive")) {
        YamlInt(*g, "minPhysicsBalls", g_config.revive.minPhysicsBalls, warnings);
        YamlNum(*g, "delaySeconds", g_config.revive.delaySeconds, warnings);
        YamlNum(*g, "stopSeconds", g_config.revive.stopSeconds, warnings);
        YamlNum(*g, "flightSpeed", g_config.revive.flightSpeed, warnings);
        YamlNum(*g, "shieldValueScale", g_config.revive.shieldValueScale, warnings);
    } else {
        warnings += "revive 组缺失；";
    }
    if (const YamlNode *g = YamlFind(root, "startup")) {
        YamlNum(*g, "openingShotgunValue", g_config.startup.openingShotgunValue, warnings);
    } else {
        warnings += "startup 组缺失；";
    }

    if (!warnings.empty()) {
        SDL_Log("config: 部分字段使用默认值：%s", warnings.c_str());
    }
    g_config.filePath = path;
    SDL_Log("config: 已加载 %s（gravity=%.0f, speed=%.0f, ammo=%.0f）",
            path.c_str(), g_config.physics.gravity, g_config.paintBalls.speed,
            g_config.machineGun.initialAmmo);
    return true;
}

// ==================== 实时配置应用（SETCONFIG 协议） ====================
// key 为点分键：组.键 或 组.键.下标（数组）；values 为空格分隔的标量。

bool ApplyConfigKey(const std::string &key, const std::vector<std::string> &values,
                    std::string &error)
{
    float f = 0.0f;
    int i = 0;
    bool b = false;

    auto get_num = [&](std::size_t idx, float &out) -> bool {
        if (idx >= values.size()) {
            error = key + " 缺少第 " + std::to_string(idx + 1) + " 个值";
            return false;
        }
        char *end = nullptr;
        const float v = std::strtof(values[idx].c_str(), &end);
        if (end == values[idx].c_str() || *end != '\0') {
            error = key + " 值非法: " + values[idx];
            return false;
        }
        out = v;
        return true;
    };
    auto get_int = [&](std::size_t idx, int &out) -> bool {
        float v = 0.0f;
        if (!get_num(idx, v)) {
            return false;
        }
        out = static_cast<int>(v);
        return true;
    };
    auto get_bool = [&](std::size_t idx, bool &out) -> bool {
        if (idx >= values.size()) {
            error = key + " 缺少值";
            return false;
        }
        const std::string &s = values[idx];
        if (s == "true" || s == "1") {
            out = true;
            return true;
        }
        if (s == "false" || s == "0") {
            out = false;
            return true;
        }
        error = key + " 布尔值非法: " + s;
        return false;
    };
    // 数组键（组.键.下标）：直接按前缀匹配
    auto apply_array_branch = [&](const char *prefix, float *target, int count) -> bool {
        const std::size_t prefix_len = std::strlen(prefix);
        if (key.compare(0, prefix_len, prefix) != 0) {
            return false;  // 不匹配，交给后面的分支
        }
        const std::size_t dot = key.rfind('.');
        const int idx = std::atoi(key.c_str() + dot + 1);
        if (idx < 0 || idx >= count) {
            error = key + " 下标越界";
            return false;  // 匹配但非法：直接失败
        }
        if (!get_num(0, f)) {
            return false;
        }
        target[idx] = f;
        return true;
    };

    // paintBalls
    if (key == "paintBalls.speed") { if (get_num(0, f)) g_config.paintBalls.speed = f; else return false; }
    else if (key == "paintBalls.initialValue") { if (get_num(0, f)) g_config.paintBalls.initialValue = f; else return false; }
    else if (key == "paintBalls.deleteValue") { if (get_num(0, f)) g_config.paintBalls.deleteValue = f; else return false; }
    else if (key == "paintBalls.radius") { if (get_num(0, f)) g_config.paintBalls.radius = f; else return false; }
    else if (key == "paintBalls.pixelCostBase") { if (get_num(0, f)) g_config.paintBalls.pixelCostBase = f; else return false; }
    else if (key == "paintBalls.pixelCostPerMinute") { if (get_num(0, f)) g_config.paintBalls.pixelCostPerMinute = f; else return false; }
    else if (key == "paintBalls.pixelCostMax") { if (get_num(0, f)) g_config.paintBalls.pixelCostMax = f; else return false; }
    // physics
    else if (key == "physics.countPerColor") { if (get_int(0, i)) g_config.physics.countPerColor = i; else return false; }
    else if (key == "physics.initialValue") { if (get_num(0, f)) g_config.physics.initialValue = f; else return false; }
    else if (key == "physics.radius") { if (get_num(0, f)) g_config.physics.radius = f; else return false; }
    else if (key == "physics.gravity") { if (get_num(0, f)) g_config.physics.gravity = f; else return false; }
    else if (key == "physics.launchSpeed") { if (get_num(0, f)) g_config.physics.launchSpeed = f; else return false; }
    else if (key == "physics.restitution") { if (get_num(0, f)) g_config.physics.restitution = f; else return false; }
    // lift
    else if (key == "lift.initialThreshold") { if (get_num(0, f)) g_config.lift.initialThreshold = f; else return false; }
    else if (key == "lift.growthPerSecond") { if (get_num(0, f)) g_config.lift.growthPerSecond = f; else return false; }
    else if (key == "lift.acceleration") { if (get_num(0, f)) g_config.lift.acceleration = f; else return false; }
    else if (apply_array_branch("lift.weaponInitial.", g_config.lift.weaponInitial, 3)) {}
    else if (key == "lift.highValueLimit") { if (get_num(0, f)) g_config.lift.highValueLimit = f; else return false; }
    // machineGun
    else if (key == "machineGun.initialAmmo") { if (get_num(0, f)) g_config.machineGun.initialAmmo = f; else return false; }
    else if (key == "machineGun.drainDivisor") { if (get_num(0, f)) g_config.machineGun.drainDivisor = f; else return false; }
    else if (key == "machineGun.minBallValue") { if (get_num(0, f)) g_config.machineGun.minBallValue = f; else return false; }
    else if (key == "machineGun.maxBallsPerFrame") { if (get_num(0, f)) g_config.machineGun.maxBallsPerFrame = f; else return false; }
    else if (key == "machineGun.speed") { if (get_num(0, f)) g_config.machineGun.speed = f; else return false; }
    else if (key == "machineGun.spreadDeg") { if (get_num(0, f)) g_config.machineGun.spreadDeg = f; else return false; }
    else if (key == "machineGun.rotateDegPerFrame") { if (get_num(0, f)) g_config.machineGun.rotateDegPerFrame = f; else return false; }
    else if (key == "machineGun.lockDistance") { if (get_num(0, f)) g_config.machineGun.lockDistance = f; else return false; }
    // shotgun
    else if (key == "shotgun.fragmentValue") { if (get_num(0, f)) g_config.shotgun.fragmentValue = f; else return false; }
    else if (key == "shotgun.maxFragments") { if (get_int(0, i)) g_config.shotgun.maxFragments = i; else return false; }
    else if (key == "shotgun.spreadDeg") { if (get_num(0, f)) g_config.shotgun.spreadDeg = f; else return false; }
    else if (key == "shotgun.speed") { if (get_num(0, f)) g_config.shotgun.speed = f; else return false; }
    // sniper
    else if (key == "sniper.explosionValue") { if (get_num(0, f)) g_config.sniper.explosionValue = f; else return false; }
    else if (key == "sniper.maxFragments") { if (get_int(0, i)) g_config.sniper.maxFragments = i; else return false; }
    else if (key == "sniper.speed") { if (get_num(0, f)) g_config.sniper.speed = f; else return false; }
    else if (key == "sniper.jitter") { if (get_num(0, f)) g_config.sniper.jitter = f; else return false; }
    else if (key == "sniper.particlesPerFrame") { if (get_int(0, i)) g_config.sniper.particlesPerFrame = i; else return false; }
    else if (key == "sniper.selfDestructChance") { if (get_num(0, f)) g_config.sniper.selfDestructChance = f; else return false; }
    else if (key == "sniper.dotTrailLife") { if (get_num(0, f)) g_config.sniper.dotTrailLife = f; else return false; }
    else if (key == "sniper.gravityRadius") { if (get_num(0, f)) g_config.sniper.gravityRadius = f; else return false; }
    else if (key == "sniper.gravityStrength") { if (get_num(0, f)) g_config.sniper.gravityStrength = f; else return false; }
    else if (key == "sniper.gravityMaxSpeed") { if (get_num(0, f)) g_config.sniper.gravityMaxSpeed = f; else return false; }
    else if (key == "sniper.gravityValueGain") { if (get_num(0, f)) g_config.sniper.gravityValueGain = f; else return false; }
    // bigBall
    else if (key == "bigBall.radiusLogOffset") { if (get_num(0, f)) g_config.bigBall.radiusLogOffset = f; else return false; }
    else if (key == "bigBall.radiusMin") { if (get_num(0, f)) g_config.bigBall.radiusMin = f; else return false; }
    else if (key == "bigBall.speedFactor") { if (get_num(0, f)) g_config.bigBall.speedFactor = f; else return false; }
    else if (key == "bigBall.trailValue") { if (get_num(0, f)) g_config.bigBall.trailValue = f; else return false; }
    else if (key == "bigBall.gravityRadius") { if (get_num(0, f)) g_config.bigBall.gravityRadius = f; else return false; }
    else if (key == "bigBall.gravityStrength") { if (get_num(0, f)) g_config.bigBall.gravityStrength = f; else return false; }
    else if (key == "bigBall.gravityMaxSpeed") { if (get_num(0, f)) g_config.bigBall.gravityMaxSpeed = f; else return false; }
    else if (key == "bigBall.fragmentGravityStrength") { if (get_num(0, f)) g_config.bigBall.fragmentGravityStrength = f; else return false; }
    else if (key == "bigBall.splitIntervalSeconds") { if (get_num(0, f)) g_config.bigBall.splitIntervalSeconds = f; else return false; }
    // shield
    else if (key == "shield.initialValue") { if (get_num(0, f)) g_config.shield.initialValue = f; else return false; }
    else if (key == "shield.radius") { if (get_num(0, f)) g_config.shield.radius = f; else return false; }
    // combat
    else if (key == "combat.absorbRatio") { if (get_num(0, f)) g_config.combat.absorbRatio = f; else return false; }
    else if (key == "combat.damageRatio") { if (get_num(0, f)) g_config.combat.damageRatio = f; else return false; }
    // gameOver
    else if (key == "gameOver.countdownFrames") { if (get_int(0, i)) g_config.gameOver.countdownFrames = i; else return false; }
    // revive
    else if (key == "revive.minPhysicsBalls") { if (get_int(0, i)) g_config.revive.minPhysicsBalls = i; else return false; }
    else if (key == "revive.delaySeconds") { if (get_num(0, f)) g_config.revive.delaySeconds = f; else return false; }
    else if (key == "revive.stopSeconds") { if (get_num(0, f)) g_config.revive.stopSeconds = f; else return false; }
    else if (key == "revive.flightSpeed") { if (get_num(0, f)) g_config.revive.flightSpeed = f; else return false; }
    else if (key == "revive.shieldValueScale") { if (get_num(0, f)) g_config.revive.shieldValueScale = f; else return false; }
    // startup
    else if (key == "startup.openingShotgunValue") { if (get_num(0, f)) g_config.startup.openingShotgunValue = f; else return false; }
    else {
        if (!error.empty()) {
            return false;  // 前面的数组分支匹配但失败（已记录具体原因）
        }
        error = "未知配置键: " + key;
        return false;
    }

    std::string joined;
    for (const std::string &v : values) {
        if (!joined.empty()) joined += ' ';
        joined += v;
    }
    SDL_Log("config: SETCONFIG %s = %s", key.c_str(), joined.c_str());
    return true;
}
