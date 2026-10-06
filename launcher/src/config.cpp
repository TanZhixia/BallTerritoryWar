#include "config.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

ConfigItem FloatItem(const char *key, const char *label, const char *desc, double min_value,
                     double max_value, double def, bool restart_only = false)
{
    ConfigItem item;
    item.key = key;
    item.label = label;
    item.desc = desc;
    item.type = ConfigItem::Type::Float;
    item.count = 1;
    item.min_value = min_value;
    item.max_value = max_value;
    item.defaults[0] = def;
    item.restart_only = restart_only;
    return item;
}

ConfigItem IntItem(const char *key, const char *label, const char *desc, double min_value,
                   double max_value, int def, bool restart_only = false)
{
    ConfigItem item = FloatItem(key, label, desc, min_value, max_value, def, restart_only);
    item.type = ConfigItem::Type::Int;
    return item;
}

ConfigItem ArrayItem(const char *key, const char *label, const char *desc, double min_value,
                     double max_value, double a, double b, double c)
{
    ConfigItem item = FloatItem(key, label, desc, min_value, max_value, a);
    item.type = ConfigItem::Type::FloatArray;
    item.count = 3;
    item.defaults[0] = a;
    item.defaults[1] = b;
    item.defaults[2] = c;
    return item;
}

std::vector<ConfigGroup> BuildSchema()
{
    std::vector<ConfigGroup> schema = {
        {"paintBalls", "画笔球", {
            FloatItem("speed", "速度", "画笔球飞行速度", 10, 400, 120.0),
            FloatItem("initialValue", "初始价值", "球的初始价值", 0, 100, 15.0),
            FloatItem("deleteValue", "删除阈值", "价值 ≤ 此值时删除", 0, 10, 0.0),
            FloatItem("radius", "半径", "普通球半径", 1, 10, 2.0),
            FloatItem("pixelCostBase", "涂画基础消耗", "涂画每像素基础消耗", 0, 10, 1.0),
            FloatItem("pixelCostPerMinute", "消耗增长间隔", "每 N 分钟消耗 +1", 0.5, 60, 3.0),
            FloatItem("pixelCostMax", "消耗上限", "涂画消耗上限", 1, 100, 16.0),
        }},
        {"physics", "物理球", {
            IntItem("countPerColor", "每队数量", "每队物理球数量", 1, 10, 4, true),
            FloatItem("initialValue", "初始价值", "物理球初始价值", 100, 1000000, 1000.0, true),
            FloatItem("radius", "半径", "物理球半径", 2, 30, 10.0),
            FloatItem("gravity", "重力", "机械区重力", 0, 2000, 200.0),
            FloatItem("launchSpeed", "发射速度", "重置/发射速度", 20, 500, 120.0),
            FloatItem("restitution", "反弹系数", "1 = 无损耗", 0, 1, 1.0),
        }},
        {"lift", "升力系统", {
            FloatItem("initialThreshold", "缺口初始阈值", "缺口升力初始阈值（无上限，按增长率一直增长）", 1000, 100000000, 100000.0),
            FloatItem("growthPerSecond", "每秒增长率", "0.01 = 1%", 0, 0.1, 0.01),
            FloatItem("acceleration", "升力加速度", "升力加速度", 0, 5000, 1200.0),
            ArrayItem("weaponInitial", "武器区初始阈值", "*8 / *4 / *2 升力初始阈值（无上限）", 1000, 100000000, 50000.0, 80000.0, 100000.0),
            FloatItem("highValueLimit", "前期高价值限制", "10 分钟内超过此值的球在霰弹/狙击列被顶回", 10000, 100000000, 2000000.0),
        }},
        {"machineGun", "机枪", {
            FloatItem("initialAmmo", "初始弹药", "每队初始弹药", 0, 10000000, 250000.0, true),
            FloatItem("drainDivisor", "消耗除数", "弹药消耗 = ammo / 此值（每帧）", 100, 100000, 12000.0),
            FloatItem("minBallValue", "单发最小价值", "单发子弹最小价值", 1, 1000, 20.0),
            FloatItem("maxBallsPerFrame", "每帧最多发射", "每色每帧发射上限", 1, 100, 10.0),
            FloatItem("speed", "子弹速度", "机枪子弹速度", 10, 500, 120.0),
            FloatItem("spreadDeg", "散射角度", "总散射角度（度）", 0, 30, 4.0),
            FloatItem("rotateDegPerFrame", "炮管转速", "无目标时每帧旋转（度）", 0, 10, 0.5),
            FloatItem("lockDistance", "锁定距离", "锁定敌方大球的距离", 50, 1000, 300.0),
        }},
        {"shotgun", "霰弹", {
            FloatItem("fragmentValue", "每份价值", "value / 此值 = 碎片数", 1, 1000, 50.0),
            IntItem("maxFragments", "碎片上限", "碎片数上限", 10, 10000, 1000),
            FloatItem("spreadDeg", "散射角度", "总散射角度（度）", 0, 30, 4.0),
            FloatItem("speed", "碎片速度", "碎片速度", 10, 500, 120.0),
        }},
        {"sniper", "狙击", {
            FloatItem("explosionValue", "爆炸每份价值", "爆炸碎片每份价值", 1, 1000, 50.0),
            IntItem("maxFragments", "爆炸碎片上限", "爆炸碎片数上限", 10, 10000, 1000),
            FloatItem("speed", "速度", "狙击/碎片速度", 10, 500, 120.0),
            FloatItem("jitter", "飞行抖动", "抖动幅度（像素），仅显示层", 0, 20, 3.0),
            IntItem("particlesPerFrame", "每帧粒子数", "飞行时生成的尾迹粒子", 0, 20, 3),
            FloatItem("selfDestructChance", "每秒自爆概率", "狙击不稳定，0 = 关闭", 0, 1, 0.01),
            FloatItem("dotTrailLife", "光点拖尾时长", "秒，0 = 关闭", 0, 1, 0.25),
            FloatItem("gravityRadius", "引力半径", "吸引此范围内所有球（含大球）", 0, 400, 150.0),
            FloatItem("gravityStrength", "引力常数 G", "a = G × 质量 / r²", 0, 100, 4.0),
            FloatItem("gravityMaxSpeed", "吸引速度上限", "被吸引球的速度上限（防失控）", 60, 2000, 600.0),
            FloatItem("gravityValueGain", "引力场增值", "引力场内的小球每秒 +value（0 = 关闭）", 0, 1000, 1.0),
        }},
        {"bigBall", "大球", {
            FloatItem("radiusLogOffset", "半径对数偏移", "半径 = (log(value) + 此值) × 2", 0, 20, 5.0),
            FloatItem("radiusMin", "半径下限", "大球半径下限", 1, 50, 2.0),
            FloatItem("speedFactor", "速度系数", "大球速度 = 画笔球速度 × 此值", 0.1, 2, 0.5),
            FloatItem("trailValue", "拖尾消耗", "每帧扣大球价值", 0, 500, 20.0),
            FloatItem("gravityRadius", "引力半径", "大球引力半径（吸引大球与狙击碎片）", 0, 1000, 260.0),
            FloatItem("gravityStrength", "大球引力常数", "敌对大球之间互吸（a = G × value / r²，同色不吸）", 0, 100, 0.5),
            FloatItem("gravityMaxSpeed", "吸引速度上限", "被吸引球的速度上限（防失控）", 60, 2000, 480.0),
            FloatItem("fragmentGravityStrength", "碎片引力常数", "狙击爆炸碎片被大球吸引（a = G × value / r²，0 = 关闭）", 0, 100, 0.5),
            FloatItem("splitIntervalSeconds", "分裂间隔", "大球每 N 秒分裂成两个 value/2（0 = 关闭）", 0, 600, 60.0),
        }},
        {"shield", "护盾", {
            FloatItem("initialValue", "初始护盾", "每队初始护盾", 0, 1000000000, 10000000.0, true),
            FloatItem("radius", "半径", "护盾圆半径", 20, 300, 80.0),
        }},
        {"combat", "战斗", {
            FloatItem("absorbRatio", "吞并倍数", "价值差 ≥ 此倍数直接吞并", 1, 100, 20.0),
            FloatItem("damageRatio", "伤害比例", "互扣大者价值的此比例", 0, 1, 0.1),
        }},
        {"gameOver", "结束条件", {
            IntItem("countdownFrames", "倒计时帧数", "60fps 下 3600 = 60 秒", 60, 36000, 3600),
        }},
        {"revive", "复活", {
            IntItem("minPhysicsBalls", "最少物理球", "至少这么多物理球才触发复活（只剩 1 个不复活，0 = 关闭）", 0, 16, 2),
            FloatItem("delaySeconds", "复活等待", "死亡后等这么多秒才复活（等待期内武器格转护盾）", 0, 300, 60.0),
            FloatItem("stopSeconds", "停止停顿", "最大的物理球停止移动的停顿时间（秒）", 0, 5, 0.5),
            FloatItem("flightSpeed", "飞行速度", "飞向炮塔的速度（像素/秒）", 20, 3000, 400.0),
            FloatItem("shieldValueScale", "护盾倍率", "物理球价值 → 护盾值的倍率", 0, 10, 1.0),
        }},
        {"startup", "开局", {
            FloatItem("openingShotgunValue", "开局霰弹总价值", "每队开局向中心发射的霰弹总价值", 0, 100000000, 2500000.0, true),
        }},
    };
    return schema;
}

std::string Trim(const std::string &s)
{
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) {
        ++b;
    }
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) {
        --e;
    }
    return s.substr(b, e - b);
}

// 去掉行尾注释（引号内的 # 不算注释）
std::string StripComment(const std::string &s)
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

int IndentOf(const std::string &line)
{
    int n = 0;
    for (char c : line) {
        if (c == ' ') {
            ++n;
        } else {
            break;
        }
    }
    return n;
}

std::string Unquote(const std::string &s)
{
    if (s.size() >= 2 && s.front() == s.back() &&
        (s.front() == '\'' || s.front() == '"')) {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

struct LineInfo
{
    bool has_key = false;
    std::string key;              // dotted key
    std::size_t value_begin = 0;  // 值在原始行中的区间 [begin, end)
    std::size_t value_end = 0;
    bool value_present = false;
};

// 扫描 YAML 行，得到每行对应的 dotted key 与值区间（用于读取与行级改写）
std::vector<LineInfo> ScanYaml(const std::vector<std::string> &lines)
{
    std::vector<LineInfo> out(lines.size());
    std::string group;
    std::string array_key;
    int array_index = 0;

    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string &raw = lines[i];
        const std::string content = Trim(StripComment(raw));
        LineInfo info;
        if (content.empty()) {
            out[i] = info;
            continue;
        }
        const int indent = IndentOf(raw);

        // 定位值区间
        std::size_t colon = raw.find(':');
        const std::size_t dash = content.rfind("- ", 0) == 0 ? raw.find('-') : std::string::npos;

        if (indent == 0) {
            array_key.clear();
            array_index = 0;
            if (!content.empty() && content.back() == ':') {
                group = Trim(content.substr(0, content.size() - 1));
            } else if (colon != std::string::npos) {
                const std::string key = Trim(raw.substr(0, colon));
                info.has_key = true;
                info.key = key;
                group.clear();
                std::size_t begin = colon + 1;
                while (begin < raw.size() && raw[begin] == ' ') {
                    ++begin;
                }
                info.value_begin = begin;
                std::size_t end = raw.find('#', begin);
                if (end == std::string::npos) {
                    end = raw.size();
                }
                while (end > begin && std::isspace(static_cast<unsigned char>(raw[end - 1]))) {
                    --end;
                }
                info.value_end = end;
                info.value_present = end > begin;
            }
        } else if (dash != std::string::npos && !array_key.empty()) {
            info.has_key = true;
            info.key = group + "." + array_key + "." + std::to_string(array_index++);
            std::size_t begin = dash + 1;
            while (begin < raw.size() && raw[begin] == ' ') {
                ++begin;
            }
            info.value_begin = begin;
            std::size_t end = raw.find('#', begin);
            if (end == std::string::npos) {
                end = raw.size();
            }
            while (end > begin && std::isspace(static_cast<unsigned char>(raw[end - 1]))) {
                --end;
            }
            info.value_end = end;
            info.value_present = end > begin;
        } else if (!content.empty() && content.back() == ':') {
            array_key = Trim(content.substr(0, content.size() - 1));
            array_index = 0;
        } else if (colon != std::string::npos) {
            array_key.clear();
            const std::string key = Trim(raw.substr(0, colon));
            info.has_key = true;
            info.key = group.empty() ? key : group + "." + key;
            std::size_t begin = colon + 1;
            while (begin < raw.size() && raw[begin] == ' ') {
                ++begin;
            }
            info.value_begin = begin;
            std::size_t end = raw.find('#', begin);
            if (end == std::string::npos) {
                end = raw.size();
            }
            while (end > begin && std::isspace(static_cast<unsigned char>(raw[end - 1]))) {
                --end;
            }
            info.value_end = end;
            info.value_present = end > begin;
        }
        out[i] = info;
    }
    return out;
}

std::string FormatNumber(double value)
{
    char buf[64];
    if (value == static_cast<double>(static_cast<long long>(value))) {
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(value));
        return std::string(buf);
    }
    std::snprintf(buf, sizeof(buf), "%.6g", value);
    return std::string(buf);
}

std::vector<std::string> SplitLines(const std::string &text)
{
    std::vector<std::string> lines;
    std::string current;
    for (char c : text) {
        if (c == '\n') {
            lines.push_back(current);
            current.clear();
        } else if (c != '\r') {
            current += c;
        }
    }
    if (!current.empty()) {
        lines.push_back(current);
    }
    return lines;
}

bool ReadFileText(const std::string &path, std::string &out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    out = buffer.str();
    return true;
}

std::string ItemComment(const std::string &group, const std::string &key, int index)
{
    for (const ConfigGroup &g : ConfigSchema()) {
        if (g.group != group) {
            continue;
        }
        for (const ConfigItem &item : g.items) {
            if (item.key != key) {
                continue;
            }
            if (index >= 0) {
                static const char *const labels[3] = {"*8", "*4", "*2"};
                return labels[index < 3 ? index : 0];
            }
            return item.desc.empty() ? item.label : (item.label + "：" + item.desc);
        }
    }
    return "新增配置项";
}

// 找到顶层分组 "group:" 的行区间 [begin, end)
bool FindGroupRange(const std::vector<std::string> &lines, const std::string &group,
                    std::size_t &begin, std::size_t &end)
{
    bool found = false;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string content = Trim(StripComment(lines[i]));
        if (content.empty() || IndentOf(lines[i]) != 0) {
            continue;
        }
        if (found) {
            end = i;
            return true;
        }
        if (content.back() == ':' &&
            Trim(content.substr(0, content.size() - 1)) == group) {
            begin = i;
            found = true;
        }
    }
    if (found) {
        end = lines.size();
        return true;
    }
    return false;
}

// 把文件中缺失的键补写进去：优先插到所属分组末尾，分组不存在则在文件末尾新建分组
void AppendMissingKeys(std::vector<std::string> &lines,
                       std::map<std::string, std::string> &pending)
{
    for (auto it = pending.begin(); it != pending.end();) {
        const std::string dotted = it->first;
        const std::size_t first_dot = dotted.find('.');
        if (first_dot == std::string::npos) {
            ++it;
            continue;
        }
        const std::string group = dotted.substr(0, first_dot);
        std::string rest = dotted.substr(first_dot + 1);
        int index = -1;
        const std::size_t second_dot = rest.rfind('.');
        if (second_dot != std::string::npos) {
            index = std::atoi(rest.c_str() + second_dot + 1);
            rest = rest.substr(0, second_dot);
        }

        std::size_t begin = 0;
        std::size_t end = 0;
        if (!FindGroupRange(lines, group, begin, end)) {
            ++it;  // 分组不存在，留到后面新建
            continue;
        }

        std::size_t insert_at = end;
        while (insert_at > begin + 1 && Trim(lines[insert_at - 1]).empty()) {
            --insert_at;  // 插在分组最后一行之后、空行之前
        }

        if (index < 0) {
            lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insert_at),
                         "  " + rest + ": " + it->second + "  # " + ItemComment(group, rest, -1));
        } else {
            std::size_t array_line = lines.size();
            for (std::size_t i = begin + 1; i < end && i < lines.size(); ++i) {
                const std::string content = Trim(StripComment(lines[i]));
                if (content == rest + ":") {
                    array_line = i;
                    break;
                }
            }
            const std::string element =
                "    - " + it->second + "  # " + ItemComment(group, rest, index);
            if (array_line == lines.size()) {
                lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insert_at),
                             "  " + rest + ":");
                lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insert_at) + 1, element);
            } else {
                std::size_t last_item = array_line;
                for (std::size_t i = array_line + 1; i < end && i < lines.size(); ++i) {
                    const std::string content = Trim(StripComment(lines[i]));
                    if (content.empty()) {
                        continue;
                    }
                    if (content == "-" || content.rfind("- ", 0) == 0) {
                        last_item = i;
                    } else {
                        break;
                    }
                }
                lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(last_item) + 1, element);
            }
        }
        it = pending.erase(it);
    }

    if (pending.empty()) {
        return;
    }

    // 剩余的键所属分组不存在：在文件末尾新建分组
    std::vector<std::string> order;
    std::map<std::string, std::vector<std::string>> blocks;
    std::map<std::string, std::map<std::string, bool>> array_header;
    for (const auto &kv : pending) {
        const std::size_t first_dot = kv.first.find('.');
        const std::string group =
            first_dot == std::string::npos ? kv.first : kv.first.substr(0, first_dot);
        std::string rest = first_dot == std::string::npos ? kv.first : kv.first.substr(first_dot + 1);
        int index = -1;
        const std::size_t second_dot = rest.rfind('.');
        if (second_dot != std::string::npos) {
            index = std::atoi(rest.c_str() + second_dot + 1);
            rest = rest.substr(0, second_dot);
        }
        if (blocks.find(group) == blocks.end()) {
            order.push_back(group);
        }
        if (index < 0) {
            blocks[group].push_back("  " + rest + ": " + kv.second + "  # " +
                                    ItemComment(group, rest, -1));
        } else {
            if (!array_header[group][rest]) {
                blocks[group].push_back("  " + rest + ":");
                array_header[group][rest] = true;
            }
            blocks[group].push_back("    - " + kv.second + "  # " +
                                    ItemComment(group, rest, index));
        }
    }
    for (const std::string &group : order) {
        std::string label = group;
        for (const ConfigGroup &g : ConfigSchema()) {
            if (g.group == group) {
                label = g.label;
                break;
            }
        }
        lines.push_back(std::string());
        lines.push_back(group + ":  # " + label + "（启动器补充）");
        for (const std::string &line : blocks[group]) {
            lines.push_back(line);
        }
    }
    pending.clear();
}

std::string HomeDir()
{
    const char *home = std::getenv("HOME");
    return home ? std::string(home) : std::string();
}

}  // namespace

const std::vector<ConfigGroup> &ConfigSchema()
{
    static const std::vector<ConfigGroup> schema = BuildSchema();
    return schema;
}

std::vector<ConfigEntry> ConfigEntries()
{
    std::vector<ConfigEntry> entries;
    for (const ConfigGroup &group : ConfigSchema()) {
        for (const ConfigItem &item : group.items) {
            if (item.type == ConfigItem::Type::FloatArray) {
                for (int i = 0; i < item.count; ++i) {
                    entries.push_back(ConfigEntry{&item, i, group.group});
                }
            } else {
                entries.push_back(ConfigEntry{&item, -1, group.group});
            }
        }
    }
    return entries;
}

std::string ConfigFilePath()
{
    const std::string home = HomeDir();
    return home.empty() ? std::string(".ball/config.yaml") : home + "/.ball/config.yaml";
}

std::string LauncherConfPath()
{
    const std::string home = HomeDir();
    return home.empty() ? std::string(".ball/launcher.conf") : home + "/.ball/launcher.conf";
}

std::string DefaultValueString(const ConfigItem &item, int index)
{
    const int i = index < 0 ? 0 : index;
    return FormatNumber(item.defaults[i]);
}

std::string NormalizeValue(const ConfigItem &item, int index, const std::string &raw,
                           std::string &error)
{
    (void)index;
    const std::string trimmed = Trim(raw);
    if (trimmed.empty()) {
        error = "值为空";
        return std::string();
    }
    char *end = nullptr;
    const double value = std::strtod(trimmed.c_str(), &end);
    if (end == trimmed.c_str() || *end != '\0') {
        error = "不是合法数字：" + trimmed;
        return std::string();
    }
    double clamped = value;
    if (clamped < item.min_value) {
        clamped = item.min_value;
    }
    if (clamped > item.max_value) {
        clamped = item.max_value;
    }
    if (item.type == ConfigItem::Type::Int) {
        clamped = static_cast<double>(static_cast<long long>(clamped));
    }
    error.clear();
    return FormatNumber(clamped);
}

bool LoadConfigFile(std::map<std::string, std::string> &values, std::string &error)
{
    const std::string path = ConfigFilePath();
    std::string text;
    if (!ReadFileText(path, text)) {
        error = "配置文件不存在：" + path;
        return false;
    }
    const std::vector<std::string> lines = SplitLines(text);
    const std::vector<LineInfo> infos = ScanYaml(lines);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (!infos[i].has_key || !infos[i].value_present) {
            continue;
        }
        values[infos[i].key] = Unquote(Trim(lines[i].substr(
            infos[i].value_begin, infos[i].value_end - infos[i].value_begin)));
    }
    error.clear();
    return true;
}

std::string GenerateDefaultYaml()
{
    std::string out;
    out += "# Ball Territory War 配置文件（由 CLI 启动器生成）\n";
    out += "# 修改后重启游戏生效；删除本文件可恢复默认配置。\n";
    out += "# 语法：key: value、缩进嵌套、- 列表、# 注释；不要用制表符缩进。\n\n";
    for (const ConfigGroup &group : ConfigSchema()) {
        out += group.group + ":  # " + group.label + "\n";
        for (const ConfigItem &item : group.items) {
            const std::string comment =
                item.desc.empty() ? item.label : (item.label + "：" + item.desc);
            if (item.type == ConfigItem::Type::FloatArray) {
                out += "  " + item.key + ":  # " + comment + "\n";
                static const char *const labels[3] = {"*8", "*4", "*2"};
                for (int i = 0; i < item.count; ++i) {
                    out += "    - " + DefaultValueString(item, i) + "  # " + labels[i] + "\n";
                }
            } else {
                out += "  " + item.key + ": " + DefaultValueString(item, -1) + "  # " +
                       comment + (item.restart_only ? "（下次启动生效）" : "") + "\n";
            }
        }
        out += "\n";
    }
    return out;
}

bool WriteConfigFile(const std::map<std::string, std::string> &updates, std::string &error,
                    std::vector<std::string> *appended)
{
    const std::string path = ConfigFilePath();
    std::string text;
    if (!ReadFileText(path, text)) {
        text = GenerateDefaultYaml();
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    }

    std::vector<std::string> lines = SplitLines(text);
    const std::vector<LineInfo> infos = ScanYaml(lines);

    std::map<std::string, std::string> pending = updates;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (!infos[i].has_key || !infos[i].value_present) {
            continue;
        }
        const auto it = pending.find(infos[i].key);
        if (it == pending.end()) {
            continue;
        }
        lines[i] = lines[i].substr(0, infos[i].value_begin) + it->second +
                   lines[i].substr(infos[i].value_end);
        pending.erase(it);
    }

    // 文件里没有的键（例如游戏版本更新后新增的配置项）直接补写进所属分组，
    // 而不是整次写入失败；appended 回传补写的键，便于界面提示。
    if (!pending.empty()) {
        if (appended != nullptr) {
            for (const auto &kv : pending) {
                appended->push_back(kv.first);
            }
        }
        AppendMissingKeys(lines, pending);
    }

    std::string joined;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        joined += lines[i];
        joined += "\n";
    }

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "无法写入配置文件：" + path;
        return false;
    }
    out.write(joined.data(), static_cast<std::streamsize>(joined.size()));
    out.close();
    error.clear();
    return true;
}

// ==================== 启动选项 ====================

LaunchOptions LoadLaunchOptions()
{
    LaunchOptions options;
    std::string text;
    if (!ReadFileText(LauncherConfPath(), text)) {
        return options;
    }
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = Trim(line.substr(0, eq));
        const std::string value = Trim(line.substr(eq + 1));
        if (key == "output" && !value.empty()) {
            options.output = value;
        }
        // 旧版本写入的 record/music/limit_fps/weapon_lift 已被忽略（这些开关恒为开）
    }
    return options;
}

bool SaveLaunchOptions(const LaunchOptions &options, std::string &error)
{
    const std::string path = LauncherConfPath();
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "无法写入：" + path;
        return false;
    }
    out << "# Ball Territory War CLI 启动器配置\n";
    out << "# 录制视频 / 背景音乐 / 限制 60fps / 武器升力 恒定开启，不在此配置\n";
    out << "output=" << options.output << "\n";
    out.close();
    error.clear();
    return true;
}

std::string NormalizeOutputName(const std::string &name)
{
    if (name.empty()) {
        return "output.mp4";
    }
    static const char *const kKnown[] = {".mp4", ".m4v",  ".mkv", ".mov",
                                         ".avi", ".webm", ".ts",  ".flv"};
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    for (const char *ext : kKnown) {
        const std::size_t len = std::strlen(ext);
        if (lower.size() > len && lower.compare(lower.size() - len, len, ext) == 0) {
            return name;
        }
    }
    return name + ".mp4";  // 例如 “S2第二集” / “S2.第二集” → 追加 .mp4
}

std::vector<std::string> BuildGameArgs(const LaunchOptions &options)
{
    // 不传 --no-record / --no-music / --unlimited / --no-weapon-lift：
    // 游戏默认即为 录制开 + 音乐开 + 限制 60fps + 武器升力开。
    std::vector<std::string> args;
    args.push_back("--output");
    args.push_back(NormalizeOutputName(options.output));  // 入口统一补扩展名
    return args;
}

std::string JoinArgs(const std::vector<std::string> &args)
{
    std::string out;
    for (const std::string &arg : args) {
        if (!out.empty()) {
            out += ' ';
        }
        if (arg.find(' ') != std::string::npos) {
            out += "'" + arg + "'";
        } else {
            out += arg;
        }
    }
    return out;
}
