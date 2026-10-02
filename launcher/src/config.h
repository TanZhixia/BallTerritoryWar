#ifndef BTW_LAUNCHER_CONFIG_H
#define BTW_LAUNCHER_CONFIG_H

#include <map>
#include <string>
#include <vector>

// ==================== 游戏配置（~/.ball/config.yaml） ====================
// 与 src/core/config.cpp 的键一一对应（11 组 56 键）。启动器自己读写 YAML：
// 读取用于展示，写入采用「行级替换」以保留注释与结构；文件不存在时按 schema 生成。

struct ConfigItem
{
    enum class Type { Float, Int, FloatArray };

    std::string key;        // speed
    std::string label;      // 速度
    std::string desc;       // 说明（也用于生成 YAML 注释）
    Type type = Type::Float;
    int count = 1;          // FloatArray 的元素个数
    double min_value = 0.0;
    double max_value = 0.0;
    double step = 1.0;
    double defaults[3] = {0.0, 0.0, 0.0};
    bool restart_only = false;  // 仅下次启动生效
};

struct ConfigGroup
{
    std::string group;   // paintBalls
    std::string label;   // 画笔球
    std::vector<ConfigItem> items;
};

const std::vector<ConfigGroup> &ConfigSchema();

// 扁平化后的配置项（数组按元素展开）
struct ConfigEntry
{
    const ConfigItem *item = nullptr;
    int index = -1;        // -1 = 标量；否则数组下标
    std::string group;     // 所属分组（拼 dotted key 用）
};
std::vector<ConfigEntry> ConfigEntries();

std::string ConfigFilePath();    // ~/.ball/config.yaml
std::string LauncherConfPath();  // ~/.ball/launcher.conf

// 读取全部键值：dotted key -> 原始字符串（数组元素为单个数值）
bool LoadConfigFile(std::map<std::string, std::string> &values, std::string &error);
// 行级改写指定键（保留注释/结构）；文件不存在时先生成默认文件。
// 文件中缺失的键（例如游戏新增的配置项）会自动补写到所属分组，
// 补写的键通过 appended 回传（可为 nullptr）。
bool WriteConfigFile(const std::map<std::string, std::string> &updates, std::string &error,
                     std::vector<std::string> *appended = nullptr);
// 按 schema 生成的默认 YAML 文本（带注释，可被游戏解析）
std::string GenerateDefaultYaml();
// 按 schema 的范围校验并规整数值（越界夹取），失败返回空串并填 error
std::string NormalizeValue(const ConfigItem &item, int index, const std::string &raw,
                           std::string &error);
// 默认值字符串（数组元素取对应下标）
std::string DefaultValueString(const ConfigItem &item, int index);

// ==================== 启动选项 ====================
// 录制视频 / 背景音乐 / 限制 60fps / 武器升力一律保持游戏默认（开启），
// 启动器不再暴露开关，也不传对应的关闭参数；这里只保留录像输出文件名。
struct LaunchOptions
{
    std::string output = "output.mp4";
};

LaunchOptions LoadLaunchOptions();
bool SaveLaunchOptions(const LaunchOptions &options, std::string &error);
// 规范化录像输出名：没有可识别扩展名（mp4/mkv/mov/...）时补上 .mp4，
// 否则 ffmpeg 会因为无法判断封装格式而放弃录制
std::string NormalizeOutputName(const std::string &name);
// 组装传给 build/main 的参数（不含程序名）
std::vector<std::string> BuildGameArgs(const LaunchOptions &options);
std::string JoinArgs(const std::vector<std::string> &args);

#endif  // BTW_LAUNCHER_CONFIG_H
