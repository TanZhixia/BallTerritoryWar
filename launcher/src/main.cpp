// ==================== Ball Territory War · CLI 启动器 ====================
//
// 交互模式（默认）：终端 UI —— 环境状态、实时战况、启动选项、配置编辑。
// 非交互模式：status / build / args / launch / config / telemetry 子命令，
//             便于脚本化与自动化测试。
//
// 注意：启动器自身不会启动游戏，除非显式按下 l 或执行 launch 子命令。

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <vector>

#include "config.h"
#include "json.h"
#include "project.h"
#include "telemetry.h"
#include "term.h"
#include "tui.h"

namespace {

const char *const kVersion = "1.0.0";

void PrintUsage()
{
    std::printf(
        "Ball Territory War · CLI 启动器 v%s\n"
        "\n"
        "用法：\n"
        "  btw-launcher                    启动交互式终端界面（TUI）\n"
        "  btw-launcher tui                同上\n"
        "  btw-launcher status             打印环境状态（项目/构建/ffmpeg/音乐/配置）\n"
        "  btw-launcher build              执行 cmake + make 构建游戏\n"
        "  btw-launcher args               打印将要传给游戏的命令行（不启动）\n"
        "  btw-launcher launch [--dry-run] 启动游戏（--dry-run 只打印命令）\n"
        "  btw-launcher config list        列出全部配置项与当前值\n"
        "  btw-launcher config get <键>    查询单个配置项\n"
        "  btw-launcher config set <键> <值...>   写入 config.yaml（游戏在跑则同时实时生效）\n"
        "  btw-launcher config apply <键> <值...> 仅通过遥测 socket 实时生效（不写文件）\n"
        "  btw-launcher config reset       全部恢复默认值并写入\n"
        "  btw-launcher telemetry [-s 秒] [--raw]  打印遥测帧（默认 3 秒）\n"
        "\n"
        "启动选项（可与上面任意子命令组合，也会被记住到 ~/.ball/launcher.conf）：\n"
        "  --output <文件名>               录像输出文件名（默认 output.mp4）\n"
        "  录制视频 / 背景音乐 / 限制 60fps / 武器升力 恒定开启，不再提供开关；\n"
        "  如需关闭请直接给游戏传 --no-record / --no-music / --unlimited / --no-weapon-lift。\n"
        "\n"
        "界面选项：\n"
        "  --truecolor                     强制 24bit 真彩（默认按 COLORTERM 自动判断）\n"
        "  --16color                       退回基本 16 色\n"
        "  --no-color                      关闭所有颜色（等效 NO_COLOR 环境变量）\n"
        "  --ascii                         使用 ASCII 边框（终端不支持 Unicode 制表符时）\n"
        "  -h, --help                      显示本帮助\n"
        "  -v, --version                   显示版本\n",
        kVersion);
}

std::string ConfigSourceLabel()
{
    return ConfigFilePath();
}

const ConfigItem *FindItemByKey(const std::string &group, const std::string &key)
{
    for (const ConfigGroup &g : ConfigSchema()) {
        if (g.group != group) {
            continue;
        }
        for (const ConfigItem &item : g.items) {
            if (item.key == key) {
                return &item;
            }
        }
    }
    return nullptr;
}

// 解析用户输入的键：group.key（数组则返回 index = -1，count 取 item->count）
bool ResolveKey(const std::string &key, const ConfigItem *&item, int &index,
                std::string &group_out, std::string &error)
{
    const std::size_t first = key.find('.');
    if (first == std::string::npos) {
        error = "键需要形如 组.键（如 paintBalls.speed）";
        return false;
    }
    const std::string group = key.substr(0, first);
    std::string rest = key.substr(first + 1);
    const std::size_t second = rest.rfind('.');
    if (second != std::string::npos) {
        const std::string maybe_index = rest.substr(second + 1);
        const std::string base = rest.substr(0, second);
        const ConfigItem *base_item = FindItemByKey(group, base);
        if (base_item != nullptr && base_item->type == ConfigItem::Type::FloatArray) {
            char *end = nullptr;
            const long idx = std::strtol(maybe_index.c_str(), &end, 10);
            if (end != maybe_index.c_str() && *end == '\0' && idx >= 0 &&
                idx < base_item->count) {
                item = base_item;
                index = static_cast<int>(idx);
                group_out = group;
                return true;
            }
        }
        rest = key.substr(first + 1);
    }
    const ConfigItem *found = FindItemByKey(group, rest);
    if (found == nullptr) {
        error = "未知配置键：" + key;
        return false;
    }
    item = found;
    index = found->type == ConfigItem::Type::FloatArray ? -1 : -2;
    group_out = group;
    return true;
}

std::string FullKey(const std::string &group, const ConfigItem &item, int index)
{
    std::string key = group + "." + item.key;
    if (index >= 0) {
        key += "." + std::to_string(index);
    }
    return key;
}

int CmdStatus(const std::string &root)
{
    const EnvStatus env = QueryEnv(root);
    std::printf("项目根目录    %s\n", env.root.c_str());
    std::printf("游戏程序      %s%s\n", env.game_path.c_str(),
                env.game_exists ? "（已构建）" : "（未构建，请先 build）");
    std::printf("ffmpeg        %s\n", env.ffmpeg ? "可用" : "不可用（录像会被跳过）");
    std::printf("音乐目录      %s（%d 首）\n", env.music_dir.c_str(), env.music_count);
    std::printf("游戏配置      %s%s\n", env.config_path.c_str(),
                env.config_exists ? "（已存在）" : "（未生成，游戏首次运行或 config set 时创建）");
    std::printf("启动器配置    %s\n", env.launcher_config_path.c_str());

    TelemetryClient client;
    for (int i = 0; i < 4 && !client.connected(); ++i) {
        client.Poll();
    }
    std::printf("游戏状态      %s\n",
                client.connected() ? "运行中（遥测已连接）" : "未运行");
    return env.game_exists ? 0 : 1;
}

int CmdBuild(const std::string &root)
{
    std::printf("构建中：cmake + make（%s/build）\n", root.c_str());
    std::string log;
    const std::string error = BuildGame(root, log);
    std::fputs(log.c_str(), stdout);
    if (!error.empty()) {
        std::fprintf(stderr, "\n%s\n", error.c_str());
        return 1;
    }
    std::printf("\n构建完成：%s/build/main\n", root.c_str());
    return 0;
}

int CmdArgs(const std::string &root, const LaunchOptions &options)
{
    const std::vector<std::string> args = BuildGameArgs(options);
    std::printf("%s/build/main %s\n", root.c_str(), JoinArgs(args).c_str());
    return 0;
}

int CmdLaunch(const std::string &root, const LaunchOptions &options, bool dry_run)
{
    if (dry_run) {
        return CmdArgs(root, options);
    }
    std::string error;
    const std::vector<std::string> args = BuildGameArgs(options);
    const int pid = SpawnGame(root, args, error);
    if (pid <= 0) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    std::printf("游戏已启动（pid %d）\n日志：%s\n", pid, GameLogPath(root).c_str());
    return 0;
}

int CmdConfigList()
{
    std::map<std::string, std::string> values;
    std::string error;
    const bool loaded = LoadConfigFile(values, error);
    if (!loaded) {
        std::printf("提示：%s（下面显示默认值）\n\n", error.c_str());
    }
    for (const ConfigGroup &group : ConfigSchema()) {
        std::printf("[%s] %s\n", group.group.c_str(), group.label.c_str());
        for (const ConfigItem &item : group.items) {
            if (item.type == ConfigItem::Type::FloatArray) {
                for (int i = 0; i < item.count; ++i) {
                    const std::string key = FullKey(group.group, item, i);
                    const auto it = values.find(key);
                    std::printf("  %-32s %-14s %s\n", key.c_str(),
                                it != values.end() ? it->second.c_str()
                                                   : DefaultValueString(item, i).c_str(),
                                item.label.c_str());
                }
            } else {
                const std::string key = FullKey(group.group, item, -1);
                const auto it = values.find(key);
                std::printf("  %-32s %-14s %s%s\n", key.c_str(),
                            it != values.end() ? it->second.c_str()
                                               : DefaultValueString(item, -1).c_str(),
                            item.label.c_str(),
                            item.restart_only ? "（下次启动生效）" : "");
            }
        }
    }
    return 0;
}

int CmdConfigGet(const std::string &key)
{
    std::map<std::string, std::string> values;
    std::string error;
    LoadConfigFile(values, error);
    const auto it = values.find(key);
    if (it != values.end()) {
        std::printf("%s\n", it->second.c_str());
        return 0;
    }
    // 回退到默认值
    const std::size_t first = key.find('.');
    const std::size_t second = key.rfind('.');
    std::string group = key, item_key = key;
    int index = -1;
    if (first != std::string::npos) {
        group = key.substr(0, first);
        item_key = key.substr(first + 1);
        if (second != std::string::npos && second > first) {
            item_key = key.substr(first + 1, second - first - 1);
            index = std::atoi(key.c_str() + second + 1);
        }
    }
    const ConfigItem *item = FindItemByKey(group, item_key);
    if (item == nullptr) {
        std::fprintf(stderr, "未知配置键：%s\n", key.c_str());
        return 1;
    }
    std::printf("%s\n", DefaultValueString(*item, index).c_str());
    return 0;
}

int CmdConfigSet(const std::map<std::string, std::string> &updates, bool write_file)
{
    if (write_file) {
        std::string error;
        if (!WriteConfigFile(updates, error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        std::printf("已写入 %s（%zu 项）\n", ConfigSourceLabel().c_str(), updates.size());
    }

    TelemetryClient client;
    for (int i = 0; i < 4 && !client.connected(); ++i) {
        client.Poll();
    }
    if (!client.connected()) {
        std::printf("游戏未运行：%s\n",
                    write_file ? "改动将在下次启动时生效" : "无法实时应用");
        return write_file ? 0 : 1;
    }
    const std::vector<std::string> failures = client.ApplyConfig(updates);
    if (failures.empty()) {
        std::printf("已实时应用 %zu 项\n", updates.size());
        return 0;
    }
    for (const std::string &failure : failures) {
        std::fprintf(stderr, "失败：%s\n", failure.c_str());
    }
    return 1;
}

int ResolveAndNormalize(const std::string &key, const std::vector<std::string> &raw_values,
                        std::map<std::string, std::string> &updates)
{
    const ConfigItem *item = nullptr;
    int index = -1;
    std::string group;
    std::string error;
    if (!ResolveKey(key, item, index, group, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }

    if (item->type == ConfigItem::Type::FloatArray && index == -1) {
        if (raw_values.size() != static_cast<std::size_t>(item->count)) {
            std::fprintf(stderr, "%s 需要 %d 个数值（当前 %zu 个）\n", key.c_str(),
                         item->count, raw_values.size());
            return 1;
        }
        for (int i = 0; i < item->count; ++i) {
            const std::string normalized =
                NormalizeValue(*item, i, raw_values[static_cast<std::size_t>(i)], error);
            if (!error.empty()) {
                std::fprintf(stderr, "%s\n", error.c_str());
                return 1;
            }
            updates[FullKey(group, *item, i)] = normalized;
        }
        return 0;
    }

    if (raw_values.size() != 1) {
        std::fprintf(stderr, "%s 只需要 1 个数值\n", key.c_str());
        return 1;
    }
    const std::string normalized = NormalizeValue(*item, index, raw_values[0], error);
    if (!error.empty()) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    updates[FullKey(group, *item, index)] = normalized;
    return 0;
}

int CmdConfigReset(bool write_file)
{
    std::map<std::string, std::string> updates;
    for (const ConfigEntry &entry : ConfigEntries()) {
        updates[FullKey(entry.group, *entry.item, entry.index)] =
            DefaultValueString(*entry.item, entry.index);
    }
    return CmdConfigSet(updates, write_file);
}

int CmdTelemetry(double seconds, bool raw)
{
    TelemetryClient client;
    std::printf("连接 %s …\n", TelemetryClient::kSocketPath);
    for (int i = 0; i < 20 && !client.connected(); ++i) {
        client.Poll();
        struct timespec ts = {};
        ts.tv_nsec = 100 * 1000 * 1000;
        nanosleep(&ts, nullptr);
    }
    if (!client.connected()) {
        std::fprintf(stderr, "游戏未运行（%s 不存在或未监听）\n",
                     TelemetryClient::kSocketPath);
        return 1;
    }
    std::printf("已连接，采样 %.1f 秒…\n", seconds);
    const double deadline = NowSeconds() + seconds;
    int frames = 0;
    while (NowSeconds() < deadline) {
        client.Poll();
        const JsonValue *stats = client.stats();
        if (stats != nullptr && frames < 100000) {
            if (raw) {
                // 重新序列化关键字段，便于脚本处理
                const JsonValue *territory = stats->Find("territory");
                std::printf("{\"territory\":[");
                if (territory) {
                    for (std::size_t i = 0; i < territory->Size(); ++i) {
                        std::printf("%s%.4f", i ? "," : "",
                                    territory->At(i)->NumberOr(0.0));
                    }
                }
                std::printf("],\"totalBalls\":%.0f",
                            stats->Find("totalBalls") ? stats->Find("totalBalls")->NumberOr(0) : 0.0);
                std::printf(",\"fps\":%.1f}\n",
                            stats->Find("fps") ? stats->Find("fps")->NumberOr(0) : 0.0);
            } else {
                std::printf("领土 ");
                const JsonValue *territory = stats->Find("territory");
                for (int i = 0; i < 4 && territory; ++i) {
                    std::printf("%s%.1f%%", i ? " / " : "",
                                territory->At(static_cast<std::size_t>(i))->NumberOr(0.0) * 100.0);
                }
                std::printf("   总球数 %.0f   物理球 %.0f   FPS %.1f\n",
                            stats->Find("totalBalls") ? stats->Find("totalBalls")->NumberOr(0) : 0.0,
                            stats->Find("physicsCount") ? stats->Find("physicsCount")->NumberOr(0) : 0.0,
                            stats->Find("fps") ? stats->Find("fps")->NumberOr(0) : 0.0);
            }
            ++frames;
        }
        struct timespec ts = {};
        ts.tv_nsec = 100 * 1000 * 1000;
        nanosleep(&ts, nullptr);
    }
    std::printf("共收到 %d 帧遥测\n", frames);
    return 0;
}

}  // namespace

int main(int argc, char **argv)
{
    std::signal(SIGPIPE, SIG_IGN);

    LaunchOptions options = LoadLaunchOptions();
    bool ascii = false;
    std::vector<std::string> positional;
    // 颜色能力：默认按 COLORTERM / NO_COLOR 判断，可用命令行强制
    int color_override = 0;  // 0 = 自动，1 = 真彩，2 = 16 色，3 = 无色

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next_value = [&](const char *name) -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s 需要一个参数\n", name);
                std::exit(2);
            }
            return std::string(argv[++i]);
        };
        if (arg == "--output" || arg == "-o") {
            options.output = next_value("--output");
        } else if (arg == "--ascii") {
            ascii = true;
        } else if (arg == "--truecolor") {
            color_override = 1;
        } else if (arg == "--16color") {
            color_override = 2;
        } else if (arg == "--no-color") {
            color_override = 3;
        } else if (arg == "-h" || arg == "--help" || arg == "help") {
            PrintUsage();
            return 0;
        } else if (arg == "-v" || arg == "--version") {
            std::printf("btw-launcher %s\n", kVersion);
            return 0;
        } else {
            positional.push_back(arg);
        }
    }

    const std::string root = ProjectRoot();

    if (positional.empty() || positional[0] == "tui") {
        // 注意：这里不落盘启动选项；只有用户真正改动开关（或执行 launch）时才写
        // ~/.ball/launcher.conf，避免仅打开界面就写用户目录。
        Tui::ColorMode color_mode = Tui::ColorMode::Basic;
        if (color_override == 1) {
            color_mode = Tui::ColorMode::TrueColor;
        } else if (color_override == 2) {
            color_mode = Tui::ColorMode::Basic;
        } else if (color_override == 3) {
            color_mode = Tui::ColorMode::None;
        } else if (const char *no_color = std::getenv("NO_COLOR")) {
            (void)no_color;
            color_mode = Tui::ColorMode::None;
        } else if (const char *term_color = std::getenv("COLORTERM")) {
            const std::string value = term_color;
            if (value.find("truecolor") != std::string::npos ||
                value.find("24bit") != std::string::npos) {
                color_mode = Tui::ColorMode::TrueColor;
            }
        }
        Tui tui(root, options, ascii, color_mode);
        return tui.Run();
    }

    const std::string command = positional[0];
    if (command == "status" || command == "env") {
        return CmdStatus(root);
    }
    if (command == "build") {
        return CmdBuild(root);
    }
    if (command == "args") {
        return CmdArgs(root, options);
    }
    if (command == "launch") {
        bool dry_run = false;
        for (std::size_t i = 1; i < positional.size(); ++i) {
            if (positional[i] == "--dry-run") {
                dry_run = true;
            }
        }
        std::string error;
        SaveLaunchOptions(options, error);
        return CmdLaunch(root, options, dry_run);
    }
    if (command == "telemetry") {
        double seconds = 3.0;
        bool raw = false;
        for (std::size_t i = 1; i < positional.size(); ++i) {
            if (positional[i] == "--raw") {
                raw = true;
            } else if ((positional[i] == "-s" || positional[i] == "--seconds") &&
                       i + 1 < positional.size()) {
                seconds = std::atof(positional[++i].c_str());
            }
        }
        return CmdTelemetry(seconds, raw);
    }
    if (command == "config") {
        if (positional.size() < 2) {
            std::fprintf(stderr, "用法：config list|get|set|apply|reset …\n");
            return 2;
        }
        const std::string action = positional[1];
        if (action == "list") {
            return CmdConfigList();
        }
        if (action == "get") {
            if (positional.size() < 3) {
                std::fprintf(stderr, "用法：config get <键>\n");
                return 2;
            }
            return CmdConfigGet(positional[2]);
        }
        if (action == "reset") {
            return CmdConfigReset(true);
        }
        if (action == "set" || action == "apply") {
            if (positional.size() < 4) {
                std::fprintf(stderr, "用法：config %s <键> <值...>\n", action.c_str());
                return 2;
            }
            std::map<std::string, std::string> updates;
            std::vector<std::string> values(positional.begin() + 3, positional.end());
            if (ResolveAndNormalize(positional[2], values, updates) != 0) {
                return 2;
            }
            return CmdConfigSet(updates, action == "set");
        }
        std::fprintf(stderr, "未知 config 子命令：%s\n", action.c_str());
        return 2;
    }

    std::fprintf(stderr, "未知命令：%s（--help 查看用法）\n", command.c_str());
    return 2;
}
