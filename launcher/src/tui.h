#ifndef BTW_LAUNCHER_TUI_H
#define BTW_LAUNCHER_TUI_H

#include <map>
#include <string>
#include <vector>

#include "config.h"
#include "project.h"
#include "telemetry.h"
#include "term.h"

// ==================== 交互式终端界面 ====================
// 主面板：环境状态 + 实时战况（遥测）+ 启动选项
// 配置面板：11 组 56 键的可视化编辑，支持行级写回 YAML + SETCONFIG 实时生效
class Tui
{
public:
    // 终端配色能力：真彩（24bit）/ 基本 16 色 / 关闭颜色
    enum class ColorMode { Basic, TrueColor, None };

    Tui(std::string root, LaunchOptions options, bool ascii_frames,
        ColorMode color_mode = ColorMode::Basic);

    int Run();

private:
    enum class View { Main, Config };

    // 所有样式都是 Screen 里注册过的 ANSI 前缀 id
    struct StyleSet
    {
        int normal = 0;
        int text = 0;
        int dim = 0;
        int border = 0;
        int title = 0;
        int accent = 0;
        int accent2 = 0;
        int ok = 0;
        int warn = 0;
        int error = 0;
        int selected = 0;        // 选中行（可带底色）
        int selected_accent = 0; // 选中行里的强调文字（共享底色）
        int bar_warn = 0;
        int track = 0;      // 进度条底槽
        int bar_bg = 0;     // 状态栏/提示行底色
        int bar_title = 0;
        int bar_dim = 0;
        int bar_text = 0;
        int bar_ok = 0;
        int bar_off = 0;
        int team[4] = {0, 0, 0, 0};
        int team_dim = 0;
        int bar_fill[4] = {0, 0, 0, 0};
    };

    void BuildStyles();
    void Draw();
    void DrawMain(int top, int bottom);
    void DrawConfig(int top, int bottom);
    void DrawStatusBar();
    void DrawFooter();
    void DrawMessageBox(int x, int y, int width, int height);

    void HandleKey(const term::Key &key);
    void HandleMainKey(const term::Key &key);
    void HandleConfigKey(const term::Key &key);
    void HandleOutputEditKey(const term::Key &key);

    void RefreshConfigValues(bool reset_to_defaults);
    bool CollectConfigUpdates(std::map<std::string, std::string> &updates,
                              bool only_changed) const;
    void ApplyConfig(bool write_file);
    void ResetConfigDefaults();
    void SaveOptionsQuietly();
    void LaunchGame();
    void RunBuild();
    void SetMessage(const std::string &text, bool is_error = false);
    const ConfigEntry *SelectedEntry() const;
    bool HasUnsavedConfigChanges() const;
    std::string EntryKey(const ConfigEntry &entry) const;
    const std::string &ValueOf(const std::string &key) const;

    std::string root_;
    EnvStatus env_;
    LaunchOptions options_;
    TelemetryClient telemetry_;
    std::vector<ConfigEntry> entries_;
    std::map<std::string, std::string> config_values_;
    std::map<std::string, std::string> config_original_;
    StyleSet styles_;

    term::Screen screen_;  // 单元格屏幕缓冲
    int content_x_ = 0;    // 面板内容区（超宽终端下居中限宽）
    int content_w_ = 0;
    View view_ = View::Main;
    int config_selection_ = 0;
    int config_scroll_ = 0;
    bool editing_ = false;         // 配置面板：编辑数值
    bool editing_output_ = false;  // 主面板：编辑输出文件名
    std::string edit_buffer_;
    std::string edit_key_hint_;  // 编辑提示（配置键或“输出文件”）

    std::string message_;
    std::string build_tail_;
    bool message_error_ = false;
    enum class Confirm { None, Launch, ResetDefaults, QuitWithChanges };
    Confirm confirm_ = Confirm::None;

    int game_pid_ = -1;
    bool game_running_ = false;
    int game_exit_code_ = 0;

    bool quit_ = false;
    bool ascii_ = false;
    ColorMode color_mode_ = ColorMode::Basic;
};

#endif  // BTW_LAUNCHER_TUI_H
