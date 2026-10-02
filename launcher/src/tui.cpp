#include "tui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

const char *const kTeamNames[4] = {"红队", "绿队", "蓝队", "黄队"};

std::string FormatNum(double value)
{
    char buf[48];
    const double abs_value = std::fabs(value);
    if (abs_value >= 1e9) {
        std::snprintf(buf, sizeof(buf), "%.2fB", value / 1e9);
    } else if (abs_value >= 1e6) {
        std::snprintf(buf, sizeof(buf), "%.2fM", value / 1e6);
    } else if (abs_value >= 1e3) {
        std::snprintf(buf, sizeof(buf), "%.1fk", value / 1e3);
    } else if (abs_value >= 1.0 || abs_value == 0.0) {
        std::snprintf(buf, sizeof(buf), "%.0f", value);
    } else {
        std::snprintf(buf, sizeof(buf), "%.3g", value);  // 0.1 / 0.01 等小数不丢精度
    }
    return std::string(buf);
}

std::string FormatTime(double seconds)
{
    if (seconds < 0) {
        seconds = 0;
    }
    const int total = static_cast<int>(seconds);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d:%02d", total / 60, total % 60);
    return std::string(buf);
}

std::vector<std::string> SplitMessage(const std::string &text)
{
    std::vector<std::string> lines;
    std::string current;
    for (char c : text) {
        if (c == '\n') {
            lines.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    lines.push_back(current);
    return lines;
}

}  // namespace

Tui::Tui(std::string root, LaunchOptions options, bool ascii_frames, ColorMode color_mode)
    : root_(std::move(root)), options_(std::move(options)), ascii_(ascii_frames),
      color_mode_(color_mode)
{
    term::SetAsciiFrames(ascii_);
    env_ = QueryEnv(root_);
    entries_ = ConfigEntries();
    RefreshConfigValues(false);
    if (!env_.game_exists) {
        SetMessage("游戏尚未构建：按 b 执行 cmake + make", true);
    } else {
        SetMessage("就绪：Enter/o 编辑输出文件名，c 打开配置，l 启动游戏。");
    }
}

void Tui::BuildStyles()
{
    const bool truecolor = (color_mode_ == ColorMode::TrueColor);
    const bool colored = (color_mode_ != ColorMode::None);
    const std::string reverse = colored ? term::ansi::Reverse() : std::string();
    const std::string bold = colored ? term::ansi::Bold() : std::string();

    // 前景：真彩用 24bit，否则退回基本 16 色
    auto fg = [&](int r, int g, int b, int basic) -> std::string {
        if (!colored) {
            return std::string();
        }
        return truecolor ? term::ansi::FgRgb(r, g, b) : term::ansi::Fg(basic);
    };
    // 面板底色（真彩才画底色，16 色靠反显）
    const std::string panel_bg = truecolor ? term::ansi::BgRgb(16, 19, 28) : reverse;
    const std::string select_bg = truecolor ? term::ansi::BgRgb(38, 46, 74) : reverse;

    styles_.normal = screen_.Style("");
    styles_.text = screen_.Style(fg(192, 202, 245, 7));
    styles_.dim = screen_.Style(fg(96, 106, 150, 8));
    styles_.border = screen_.Style(fg(59, 74, 107, 8));
    styles_.title = screen_.Style(bold + fg(200, 211, 245, 7));
    styles_.accent = screen_.Style(bold + fg(122, 162, 247, 12));
    styles_.accent2 = screen_.Style(bold + fg(187, 154, 247, 13));
    styles_.ok = screen_.Style(fg(158, 206, 106, 10));
    styles_.warn = screen_.Style(fg(224, 175, 104, 11));
    styles_.error = screen_.Style(bold + fg(247, 118, 142, 9));
    styles_.track = screen_.Style(fg(45, 51, 74, 8));
    styles_.selected = screen_.Style(bold + select_bg + fg(216, 222, 248, 15));
    styles_.selected_accent = screen_.Style(bold + select_bg + fg(122, 162, 247, 14));

    // 顶部状态栏 / 底部提示行：整行底色
    styles_.bar_bg = screen_.Style(panel_bg);
    styles_.bar_title = screen_.Style(bold + panel_bg + fg(122, 162, 247, 12));
    styles_.bar_text = screen_.Style(panel_bg + fg(192, 202, 245, 7));
    styles_.bar_dim = screen_.Style(panel_bg + fg(96, 106, 150, 8));
    styles_.bar_ok = screen_.Style(bold + panel_bg + fg(158, 206, 106, 10));
    styles_.bar_off = screen_.Style(panel_bg + fg(120, 130, 170, 8));
    styles_.bar_warn = screen_.Style(bold + panel_bg + fg(224, 175, 104, 11));

    // 队伍配色直接取游戏显示层（NEW_FRAME_PALETTE）
    static const int kTeamRgb[4][3] = {
        {255, 150, 150}, {100, 255, 100}, {180, 210, 255}, {255, 240, 140}};
    static const int kTeamBasic[4] = {9, 10, 12, 11};
    for (int i = 0; i < 4; ++i) {
        styles_.team[i] =
            screen_.Style(bold + fg(kTeamRgb[i][0], kTeamRgb[i][1], kTeamRgb[i][2],
                                    kTeamBasic[i]));
        styles_.bar_fill[i] =
            screen_.Style(fg(kTeamRgb[i][0], kTeamRgb[i][1], kTeamRgb[i][2], kTeamBasic[i]));
    }
    styles_.team_dim = screen_.Style(fg(96, 106, 150, 8));
}

int Tui::Run()
{
    if (!term::EnterRawMode()) {
        std::fprintf(stderr,
                     "错误：CLI 启动器需要交互式终端（TTY）。\n"
                     "非交互用法请见 btw-launcher --help（status / config / args / build 等子命令）。\n");
        return 1;
    }
    term::InstallResizeHandler();

    std::string out = term::ansi::AltScreen(true) + term::ansi::HideCursor();
    std::fwrite(out.data(), 1, out.size(), stdout);
    std::fflush(stdout);

    screen_.Resize(term::Width(), term::Height());
    BuildStyles();

    while (!quit_) {
        telemetry_.Poll();
        if (game_running_) {
            int exit_code = 0;
            if (ReapGame(game_pid_, exit_code)) {
                game_running_ = false;
                game_exit_code_ = exit_code;
                SetMessage("游戏已退出（退出码 " + std::to_string(exit_code) +
                           "），按 l 可再次启动");
            }
        }
        screen_.Resize(term::Width(), term::Height());
        Draw();
        const term::Key key = term::ReadKey(250);
        if (key.valid()) {
            HandleKey(key);
        }
    }

    out = term::ansi::Reset() + term::ansi::ShowCursor() + term::ansi::AltScreen(false);
    std::fwrite(out.data(), 1, out.size(), stdout);
    std::fflush(stdout);
    term::LeaveRawMode();
    return 0;
}

void Tui::SetMessage(const std::string &text, bool is_error)
{
    message_ = text;
    message_error_ = is_error;
}

const ConfigEntry *Tui::SelectedEntry() const
{
    if (config_selection_ < 0 ||
        config_selection_ >= static_cast<int>(entries_.size())) {
        return nullptr;
    }
    return &entries_[static_cast<std::size_t>(config_selection_)];
}

std::string Tui::EntryKey(const ConfigEntry &entry) const
{
    std::string key = entry.group + "." + entry.item->key;
    if (entry.index >= 0) {
        key += "." + std::to_string(entry.index);
    }
    return key;
}

bool Tui::HasUnsavedConfigChanges() const
{
    for (const auto &kv : config_values_) {
        const auto it = config_original_.find(kv.first);
        if (it != config_original_.end() && it->second != kv.second) {
            return true;
        }
    }
    return false;
}

const std::string &Tui::ValueOf(const std::string &key) const
{
    static const std::string empty;
    const auto it = config_values_.find(key);
    return it == config_values_.end() ? empty : it->second;
}

// ==================== 绘制 ====================

void Tui::Draw()
{
    screen_.Clear();
    const int width = screen_.width();
    const int height = screen_.height();

    if (width < 76 || height < 22) {
        screen_.Fill(0, 0, width, height, " ", styles_.bar_bg);
        screen_.Put(2, std::max(0, height / 2 - 1),
                    "终端窗口太小：需要至少 76×22，当前 " + std::to_string(width) + "×" +
                        std::to_string(height),
                    styles_.error);
        screen_.Put(2, std::max(0, height / 2), "调整窗口大小后界面会自动重排",
                    styles_.bar_dim);
        screen_.Flush();
        return;
    }

    // 面板内容区：最宽 120 列并居中，超宽终端下不会被拉散
    content_w_ = std::min(width, 120);
    content_x_ = (width - content_w_) / 2;

    DrawStatusBar();
    const int top = 1;
    const int bottom = height - 2;
    if (view_ == View::Main) {
        DrawMain(top, bottom);
    } else {
        DrawConfig(top, bottom);
    }
    DrawFooter();
    screen_.Flush();
}

void Tui::DrawStatusBar()
{
    const int width = screen_.width();
    screen_.Fill(0, 0, width, 1, " ", styles_.bar_bg);

    screen_.Put(1, 0, "Ball Territory War", styles_.bar_title);
    screen_.Put(21, 0, "CLI 启动器", styles_.bar_dim);

    const bool live = telemetry_.connected();
    const std::string badge = live ? "● LIVE" : "○ OFFLINE";
    std::string game;
    if (game_running_) {
        game = "游戏运行中 · pid " + std::to_string(game_pid_);
    } else if (game_exit_code_ != 0) {
        game = "游戏已退出（" + std::to_string(game_exit_code_) + "）";
    } else {
        game = "游戏未启动";
    }
    const int badge_w = term::DisplayWidth(badge);
    int rx = width - (badge_w + 3 + term::DisplayWidth(game) + 2);
    if (rx < 26) {
        rx = 26;
    }
    screen_.Put(rx, 0, badge, live ? styles_.bar_ok : styles_.bar_off);
    screen_.Put(rx + badge_w + 3, 0, game, styles_.bar_text);
}

void Tui::DrawFooter()
{
    const int width = screen_.width();
    const int y = screen_.height() - 1;
    screen_.Fill(0, y, width, 1, " ", styles_.bar_bg);

    if (confirm_ != Confirm::None) {
        const char *question = "有未保存的配置改动，仍要退出？   y 确认   其它键取消";
        if (confirm_ == Confirm::Launch) {
            question = "启动游戏？（会打开游戏窗口）   y 确认   其它键取消";
        } else if (confirm_ == Confirm::ResetDefaults) {
            question = "恢复全部默认值？（仅改内存，需再按 w 写入）   y 确认   其它键取消";
        }
        screen_.Put(2, y, question, styles_.bar_warn);
        return;
    }

    if (editing_output_ || editing_) {
        const std::string label = editing_output_ ? "输出文件名" : edit_key_hint_;
        const std::string head = label + "：";
        screen_.Put(2, y, head, styles_.bar_dim);
        const int head_w = term::DisplayWidth(head);
        screen_.Put(2 + head_w, y, edit_buffer_ + "_", styles_.bar_title);
        screen_.Put(2 + head_w + term::DisplayWidth(edit_buffer_) + 3, y, "Enter 确认   Esc 取消",
                    styles_.bar_dim);
        return;
    }

    struct Hint
    {
        const char *key;
        const char *text;
    };
    static const Hint kMainHints[] = {
        {"Enter / o", "编辑输出文件名"}, {"l", "启动"}, {"b", "构建"},
        {"c", "配置"}, {"r", "刷新环境"}, {"q", "退出"},
    };
    static const Hint kConfigHints[] = {
        {"↑↓ PgUp PgDn", "浏览"}, {"Enter", "编辑"}, {"w", "保存并应用"},
        {"a", "仅实时应用"}, {"d", "恢复默认"}, {"r", "重读"}, {"Esc", "返回"}, {"q", "退出"},
    };
    const bool main_view = (view_ == View::Main);
    const Hint *hints = main_view ? kMainHints : kConfigHints;
    const int count = main_view ? 6 : 8;

    int x = 2;
    for (int i = 0; i < count; ++i) {
        const std::string key = hints[i].key;
        const std::string text = hints[i].text;
        if (x + term::DisplayWidth(key) + 1 + term::DisplayWidth(text) > width - 2) {
            break;
        }
        screen_.Put(x, y, key, styles_.bar_title);
        x += term::DisplayWidth(key) + 1;
        screen_.Put(x, y, text, styles_.bar_dim);
        x += term::DisplayWidth(text) + 3;
    }
}

void Tui::DrawMessageBox(int x, int y, int width, int height)
{
    screen_.Box(x, y, width, height, "消息 / 游戏日志", styles_.border, styles_.title);
    const int interior = height - 2;
    if (interior <= 0) {
        return;
    }
    int row = 0;
    const std::vector<std::string> lines = SplitMessage(message_);
    for (const std::string &line : lines) {
        if (row >= interior) {
            break;
        }
        screen_.Put(x + 2, y + 1 + row, term::EllipsizeToWidth(line, width - 4),
                    message_error_ ? styles_.error : styles_.text);
        ++row;
    }

    if (!game_running_ && build_tail_.empty()) {
        return;
    }
    std::vector<std::string> log = TailGameLog(root_, interior - row);
    if (!build_tail_.empty() && row < interior) {
        const std::vector<std::string> tail = SplitMessage(build_tail_);
        for (const std::string &line : tail) {
            if (row >= interior) {
                break;
            }
            screen_.Put(x + 2, y + 1 + row, term::EllipsizeToWidth(line, width - 4),
                        styles_.dim);
            ++row;
        }
    }
    for (std::size_t i = 0; i < log.size() && row < interior; ++i, ++row) {
        const std::string prefix = (i == 0) ? "日志 " : "     ";
        screen_.Put(x + 2, y + 1 + row,
                    term::EllipsizeToWidth(prefix + log[i], width - 4), styles_.dim);
    }
}

void Tui::DrawMain(int top, int bottom)
{
    const int cx = content_x_;
    const int cw = content_w_;
    int y = top;

    // ---------- 环境 ----------
    const int env_h = 4;
    screen_.Box(cx, y, cw, env_h, "环境", styles_.border, styles_.title);
    screen_.Put(cx + 2, y + 1, "项目", styles_.dim);
    screen_.Put(cx + 8, y + 1, term::EllipsizeToWidth(root_, cw - 12), styles_.text);
    {
        int x = cx + 2;
        auto chip = [&](const std::string &label, const std::string &value, bool ok) {
            const std::string mark = (ok ? "● " : "○ ") + value;
            const int need = term::DisplayWidth(label) + 2 + term::DisplayWidth(mark) + 3;
            if (x + need > cx + cw - 2) {
                return;
            }
            screen_.Put(x, y + 2, label, styles_.dim);
            x += term::DisplayWidth(label) + 2;
            screen_.Put(x, y + 2, mark, ok ? styles_.ok : styles_.warn);
            x += term::DisplayWidth(mark) + 3;
        };
        chip("游戏", env_.game_exists ? "build/main 已构建" : "未构建，按 b 构建",
             env_.game_exists);
        chip("录像", env_.ffmpeg ? "ffmpeg 可用" : "ffmpeg 缺失，跳过录像", env_.ffmpeg);
        chip("音乐", std::to_string(env_.music_count) + " 首", env_.music_count > 0);
        chip("配置", env_.config_exists ? "config.yaml 已存在" : "config.yaml 未生成",
             env_.config_exists);
    }
    y += env_h;

    // ---------- 实时战况 ----------
    const int stat_h = 10;
    screen_.Box(cx, y, cw, stat_h, "实时战况 · 遥测 /tmp/btw_telemetry.sock", styles_.border,
                styles_.title);
    {
        const JsonValue *stats = telemetry_.stats();
        const int bar_w = std::max(8, std::min(30, cw - 74));
        const int col_team = cx + 2;
        const int col_bar = col_team + 6;
        const int col_pct = col_bar + bar_w + 2;
        const int col_big = col_pct + 8;
        const int col_shield = col_big + 6;
        const int col_ammo = col_shield + 11;
        const int col_phys = col_ammo + 12;

        screen_.Put(col_team, y + 1, "队伍", styles_.dim);
        screen_.Put(col_bar, y + 1, "领土占比", styles_.dim);
        screen_.Put(col_pct, y + 1, "百分比", styles_.dim);
        screen_.Put(col_big, y + 1, "大球", styles_.dim);
        screen_.Put(col_shield, y + 1, "护盾", styles_.dim);
        screen_.Put(col_ammo, y + 1, "弹药", styles_.dim);
        screen_.Put(col_phys, y + 1, "物理球", styles_.dim);
        screen_.HLine(cx + 1, y + 2, cw - 2, "─", styles_.border);

        if (!stats) {
            screen_.Put(col_team, y + 4,
                        telemetry_.connected() ? "已连接，等待第一帧遥测…"
                                               : "等待遥测：游戏未运行（按 l 启动）",
                        styles_.dim);
        } else {
            auto array_at = [&](const char *key, int index) -> const JsonValue * {
                const JsonValue *array = stats->Find(key);
                return array ? array->At(static_cast<std::size_t>(index)) : nullptr;
            };
            for (int team = 0; team < 4; ++team) {
                const int row = y + 3 + team;
                const bool alive = array_at("alive", team)
                                       ? array_at("alive", team)->BoolOr(true)
                                       : true;
                const double territory = array_at("territory", team)
                                             ? array_at("territory", team)->NumberOr(0.0)
                                             : 0.0;
                screen_.Put(col_team, row, term::PadToWidth(kTeamNames[team], 6),
                            alive ? styles_.team[team] : styles_.team_dim);

                int filled = alive ? static_cast<int>(territory * bar_w + 0.5) : 0;
                if (filled > bar_w) {
                    filled = bar_w;
                }
                std::string fill;
                std::string track;
                for (int i = 0; i < filled; ++i) {
                    fill += "█";
                }
                for (int i = filled; i < bar_w; ++i) {
                    track += "░";
                }
                screen_.Put(col_bar, row, fill, styles_.bar_fill[team]);
                screen_.Put(col_bar + filled, row, track, styles_.track);

                char pct[32];
                if (alive) {
                    std::snprintf(pct, sizeof(pct), "%.1f%%", territory * 100.0);
                } else {
                    std::snprintf(pct, sizeof(pct), "已灭");
                }
                screen_.Put(col_pct, row, pct, alive ? styles_.text : styles_.error);

                const std::string big =
                    array_at("bigBalls", team)
                        ? std::to_string(static_cast<int>(
                              array_at("bigBalls", team)->NumberOr(0.0)))
                        : "-";
                const std::string shield =
                    alive && array_at("shields", team)
                        ? FormatNum(array_at("shields", team)->NumberOr(0.0))
                        : "-";
                const std::string ammo =
                    alive && array_at("ammo", team)
                        ? FormatNum(array_at("ammo", team)->NumberOr(0.0))
                        : "-";
                const std::string phys =
                    array_at("physicsValues", team)
                        ? FormatNum(array_at("physicsValues", team)->NumberOr(0.0))
                        : "-";
                const int value_style = alive ? styles_.text : styles_.dim;
                screen_.Put(col_big, row, term::PadToWidth(big, 6), value_style);
                screen_.Put(col_shield, row, term::PadToWidth(shield, 11), value_style);
                screen_.Put(col_ammo, row, term::PadToWidth(ammo, 12), value_style);
                screen_.Put(col_phys, row, term::PadToWidth(phys, 10), value_style);
            }

            const JsonValue *total = stats->Find("totalBalls");
            const JsonValue *phys_count = stats->Find("physicsCount");
            const JsonValue *elapsed = stats->Find("elapsed");
            const JsonValue *fps = stats->Find("fps");
            screen_.HLine(cx + 1, y + stat_h - 3, cw - 2, "─", styles_.border);
            int sx = cx + 2;
            auto stat_pair = [&](const std::string &label, const std::string &value) {
                screen_.Put(sx, y + stat_h - 2, label, styles_.dim);
                sx += term::DisplayWidth(label) + 1;
                screen_.Put(sx, y + stat_h - 2, value, styles_.accent);
                sx += term::DisplayWidth(value) + 4;
            };
            stat_pair("总球数", total ? std::to_string(static_cast<int>(total->NumberOr(0))) : "-");
            stat_pair("物理球",
                      phys_count ? std::to_string(static_cast<int>(phys_count->NumberOr(0))) : "-");
            stat_pair("时间", FormatTime(elapsed ? elapsed->NumberOr(0.0) : 0.0));
            if (fps) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.1f", fps->NumberOr(0.0));
                stat_pair("FPS", buf);
            }
        }
    }
    y += stat_h;

    // ---------- 启动选项 ----------
    const int opt_h = 4;
    screen_.Box(cx, y, cw, opt_h, "启动选项", styles_.border, styles_.title);
    screen_.Fill(cx + 1, y + 1, cw - 2, 1, " ", styles_.selected);
    screen_.Put(cx + 2, y + 1, ">", styles_.selected_accent);
    screen_.Put(cx + 4, y + 1, "输出文件", styles_.selected);
    screen_.Put(cx + 24, y + 1, term::EllipsizeToWidth(options_.output, cw - 28),
                styles_.selected_accent);
    screen_.Put(cx + 2, y + 2, "录制视频 / 背景音乐 / 限制 60fps / 武器升力：恒定开启",
                styles_.dim);
    y += opt_h;

    // ---------- 消息 / 日志 ----------
    const int msg_h = bottom - y + 1;
    if (msg_h >= 3) {
        DrawMessageBox(cx, y, cw, msg_h);
    }
}

void Tui::DrawConfig(int top, int bottom)
{
    const int cx = content_x_;
    const int cw = content_w_;
    const int height = bottom - top + 1;
    std::string title = "配置 · " + env_.config_path;
    title += telemetry_.connected() ? "（游戏在线，可实时生效）" : "（游戏离线，仅写文件）";
    screen_.Box(cx, top, cw, height, title, styles_.border, styles_.title);

    struct Row
    {
        bool is_header = false;
        std::string group_label;
        std::string group_key;
        int entry = -1;
    };
    std::vector<Row> rows;
    std::string last_group;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const ConfigEntry &entry = entries_[i];
        if (entry.group != last_group) {
            last_group = entry.group;
            Row header;
            header.is_header = true;
            header.group_key = entry.group;
            for (const ConfigGroup &group : ConfigSchema()) {
                if (group.group == entry.group) {
                    header.group_label = group.label;
                    break;
                }
            }
            rows.push_back(header);
        }
        Row row;
        row.entry = static_cast<int>(i);
        rows.push_back(row);
    }

    const int list_top = top + 1;
    const int list_bottom = bottom - 3;
    const int visible = std::max(1, list_bottom - list_top + 1);

    int selected_row = 0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].entry == config_selection_) {
            selected_row = static_cast<int>(i);
            break;
        }
    }
    if (selected_row < config_scroll_) {
        config_scroll_ = selected_row;
    }
    if (selected_row >= config_scroll_ + visible) {
        config_scroll_ = selected_row - visible + 1;
    }
    const int max_scroll = std::max(0, static_cast<int>(rows.size()) - visible);
    config_scroll_ = std::max(0, std::min(config_scroll_, max_scroll));

    for (int i = 0; i < visible; ++i) {
        const int row_index = config_scroll_ + i;
        if (row_index >= static_cast<int>(rows.size())) {
            break;
        }
        const Row &row = rows[static_cast<std::size_t>(row_index)];
        const int row_y = list_top + i;
        if (row.is_header) {
            const std::string label = " " + row.group_label + "  " + row.group_key + " ";
            screen_.Put(cx + 1, row_y, "──", styles_.border);
            screen_.Put(cx + 3, row_y, label, styles_.accent2);
            const int used = cx + 3 + term::DisplayWidth(label);
            if (used < cx + cw - 2) {
                screen_.HLine(used, row_y, cx + cw - 2 - used, "─", styles_.border);
            }
            continue;
        }
        const ConfigEntry &entry = entries_[static_cast<std::size_t>(row.entry)];
        const std::string key = EntryKey(entry);
        const bool selected = (row.entry == config_selection_);
        const bool changed = config_original_.find(key) != config_original_.end() &&
                             config_original_[key] != ValueOf(key);
        std::string label = entry.item->label;
        if (entry.item->type == ConfigItem::Type::FloatArray) {
            static const char *const element_labels[3] = {"*8", "*4", "*2"};
            label = std::string(element_labels[entry.index < 3 ? entry.index : 0]) + " " + label;
        }
        char range_buf[64];
        std::snprintf(range_buf, sizeof(range_buf), "[%s ~ %s]",
                      FormatNum(entry.item->min_value).c_str(),
                      FormatNum(entry.item->max_value).c_str());

        if (selected) {
            screen_.Fill(cx + 1, row_y, cw - 2, 1, " ", styles_.selected);
        }
        const int label_style = selected ? styles_.selected : styles_.text;
        const int value_style = selected ? styles_.selected_accent
                                         : (changed ? styles_.accent : styles_.text);
        screen_.Put(cx + 2, row_y, selected ? ">" : " ", styles_.selected_accent);
        screen_.Put(cx + 4, row_y, term::PadToWidth(term::EllipsizeToWidth(label, 22), 24),
                    label_style);
        screen_.Put(cx + 30, row_y, term::PadToWidth(term::EllipsizeToWidth(ValueOf(key), 16), 18),
                    value_style);
        screen_.Put(cx + 50, row_y, term::EllipsizeToWidth(range_buf, cw - 54), styles_.dim);
        if (entry.item->restart_only) {
            screen_.Put(cx + cw - 9, row_y, "重启", styles_.warn);
        }
        if (changed) {
            screen_.Put(cx + cw - 4, row_y, "●", styles_.warn);
        }
    }

    screen_.Put(cx + 2, bottom - 2, term::EllipsizeToWidth(message_, cw - 4),
                message_error_ ? styles_.error : styles_.dim);

    if (editing_) {
        screen_.Put(cx + 2, bottom - 1,
                    term::EllipsizeToWidth("编辑 " + edit_key_hint_ + " = " + edit_buffer_ + "_",
                                           cw - 4),
                    styles_.accent);
    } else {
        const ConfigEntry *selected_entry = SelectedEntry();
        std::string info;
        if (selected_entry != nullptr) {
            info = selected_entry->item->desc.empty() ? selected_entry->item->label
                                                      : selected_entry->item->desc;
            info = EntryKey(*selected_entry) + "  ·  " + info;
            if (selected_entry->item->restart_only) {
                info += "（下次启动游戏生效）";
            }
        }
        screen_.Put(cx + 2, bottom - 1, term::EllipsizeToWidth(info, cw - 4), styles_.dim);
    }
}

// ==================== 交互 ====================

void Tui::HandleKey(const term::Key &key)
{
    if (confirm_ != Confirm::None) {
        if (key.type == term::Key::Type::Char &&
            (key.ch == 'y' || key.ch == 'Y')) {
            const Confirm action = confirm_;
            confirm_ = Confirm::None;
            if (action == Confirm::Launch) {
                LaunchGame();
            } else if (action == Confirm::ResetDefaults) {
                ResetConfigDefaults();
            } else if (action == Confirm::QuitWithChanges) {
                quit_ = true;
            }
        } else if (key.type == term::Key::Type::Escape ||
                   key.type == term::Key::Type::Char || key.type == term::Key::Type::Enter) {
            confirm_ = Confirm::None;
            SetMessage("已取消");
        }
        return;
    }

    if (editing_output_) {
        HandleOutputEditKey(key);
        return;
    }
    if (editing_) {
        HandleConfigKey(key);
        return;
    }

    if (key.type == term::Key::Type::Char && key.ch == 'q') {
        if (view_ == View::Config && HasUnsavedConfigChanges()) {
            confirm_ = Confirm::QuitWithChanges;
        } else {
            quit_ = true;
        }
        return;
    }
    if (key.type == term::Key::Type::Char && key.ch == 'c' && view_ == View::Main) {
        view_ = View::Config;
        RefreshConfigValues(false);
        SetMessage("修改后按 w 保存到 config.yaml 并实时应用；a 仅实时应用不写文件");
        return;
    }
    if (key.type == term::Key::Type::Escape && view_ == View::Config) {
        view_ = View::Main;
        return;
    }

    if (view_ == View::Main) {
        HandleMainKey(key);
    } else {
        HandleConfigKey(key);
    }
}

void Tui::HandleMainKey(const term::Key &key)
{
    switch (key.type) {
        case term::Key::Type::Enter:
            editing_output_ = true;
            edit_buffer_ = options_.output;
            edit_key_hint_ = "输出文件";
            break;
        case term::Key::Type::Char:
            if (key.ch == 'l') {
                confirm_ = Confirm::Launch;
            } else if (key.ch == 'b') {
                RunBuild();
            } else if (key.ch == 'r') {
                env_ = QueryEnv(root_);
                SetMessage("环境状态已刷新");
            } else if (key.ch == 'o') {
                editing_output_ = true;
                edit_buffer_ = options_.output;
                edit_key_hint_ = "输出文件";
            }
            break;
        default:
            break;
    }
}

void Tui::HandleOutputEditKey(const term::Key &key)
{
    if (key.type == term::Key::Type::Escape) {
        editing_output_ = false;
        edit_buffer_.clear();
        SetMessage("已取消编辑输出文件名");
        return;
    }
    if (key.type == term::Key::Type::Enter) {
        if (edit_buffer_.empty()) {
            SetMessage("输出文件名不能为空", true);
            return;
        }
        const std::string normalized = NormalizeOutputName(edit_buffer_);
        const bool added_extension = (normalized != edit_buffer_);
        options_.output = normalized;
        editing_output_ = false;
        SaveOptionsQuietly();
        SetMessage("输出文件名已设为 " + options_.output +
                   (added_extension ? "（自动补上 .mp4：ffmpeg 需要扩展名判断封装格式）" : ""));
        return;
    }
    if (key.type == term::Key::Type::Backspace) {
        term::PopLastUtf8Char(edit_buffer_);
        return;
    }
    if (key.type == term::Key::Type::Char) {
        if (!key.utf8.empty()) {
            edit_buffer_ += key.utf8;  // 支持中文/emoji 等完整 UTF-8 字符
        } else if (key.ch >= 32) {
            edit_buffer_ += key.ch;
        }
    }
}

void Tui::HandleConfigKey(const term::Key &key)
{
    if (editing_) {
        if (key.type == term::Key::Type::Escape) {
            editing_ = false;
            edit_buffer_.clear();
            SetMessage("已取消编辑");
            return;
        }
        if (key.type == term::Key::Type::Enter) {
            const ConfigEntry *entry = SelectedEntry();
            if (entry == nullptr) {
                editing_ = false;
                return;
            }
            std::string error;
            const std::string normalized =
                NormalizeValue(*entry->item, entry->index, edit_buffer_, error);
            if (!error.empty()) {
                SetMessage("无效值：" + error, true);
                return;
            }
            config_values_[EntryKey(*entry)] = normalized;
            editing_ = false;
            SetMessage("已修改 " + EntryKey(*entry) + " = " + normalized +
                       "（按 w 保存并应用）");
            return;
        }
        if (key.type == term::Key::Type::Backspace) {
            if (!edit_buffer_.empty()) {
                edit_buffer_.pop_back();
            }
            return;
        }
        if (key.type == term::Key::Type::Char) {
            const char c = key.ch;
            if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+' || c == 'e') {
                if (message_error_) {
                    SetMessage("");  // 开始修正后清掉上一次的错误提示
                }
                edit_buffer_ += c;
            }
            return;
        }
        return;
    }

    switch (key.type) {
        case term::Key::Type::Up:
            if (config_selection_ > 0) {
                --config_selection_;
            }
            break;
        case term::Key::Type::Down:
            if (config_selection_ + 1 < static_cast<int>(entries_.size())) {
                ++config_selection_;
            }
            break;
        case term::Key::Type::PageUp:
            config_selection_ = std::max(0, config_selection_ - 10);
            break;
        case term::Key::Type::PageDown:
            config_selection_ =
                std::min(static_cast<int>(entries_.size()) - 1, config_selection_ + 10);
            break;
        case term::Key::Type::Home:
            config_selection_ = 0;
            break;
        case term::Key::Type::End:
            config_selection_ = static_cast<int>(entries_.size()) - 1;
            break;
        case term::Key::Type::Enter: {
            const ConfigEntry *entry = SelectedEntry();
            if (entry != nullptr) {
                editing_ = true;
                edit_buffer_ = ValueOf(EntryKey(*entry));
                edit_key_hint_ = EntryKey(*entry);
            }
            break;
        }
        case term::Key::Type::Char:
            if (key.ch == 'j') {
                if (config_selection_ + 1 < static_cast<int>(entries_.size())) {
                    ++config_selection_;
                }
            } else if (key.ch == 'k') {
                if (config_selection_ > 0) {
                    --config_selection_;
                }
            } else if (key.ch == 'w') {
                ApplyConfig(true);
            } else if (key.ch == 'a') {
                ApplyConfig(false);
            } else if (key.ch == 'd') {
                confirm_ = Confirm::ResetDefaults;
            } else if (key.ch == 'r') {
                RefreshConfigValues(false);
                SetMessage("已从 " + env_.config_path + " 重新读取");
            }
            break;
        default:
            break;
    }
}

// ==================== 动作 ====================

void Tui::RefreshConfigValues(bool reset_to_defaults)
{
    config_values_.clear();
    if (!reset_to_defaults) {
        std::map<std::string, std::string> loaded;
        std::string error;
        if (LoadConfigFile(loaded, error)) {
            config_values_ = loaded;
        } else if (env_.config_exists) {
            SetMessage(error, true);
        }
    }
    for (const ConfigEntry &entry : entries_) {
        const std::string key = EntryKey(entry);
        if (config_values_.find(key) == config_values_.end()) {
            config_values_[key] = DefaultValueString(*entry.item, entry.index);
        }
    }
    config_original_ = config_values_;
}

bool Tui::CollectConfigUpdates(std::map<std::string, std::string> &updates,
                               bool only_changed) const
{
    for (const auto &kv : config_values_) {
        const auto it = config_original_.find(kv.first);
        if (only_changed && it != config_original_.end() && it->second == kv.second) {
            continue;
        }
        updates[kv.first] = kv.second;
    }
    return !updates.empty();
}

void Tui::ApplyConfig(bool write_file)
{
    std::map<std::string, std::string> updates;
    if (!CollectConfigUpdates(updates, true)) {
        SetMessage("没有需要应用的改动");
        return;
    }

    std::vector<std::string> appended;
    if (write_file) {
        std::string error;
        if (!WriteConfigFile(updates, error, &appended)) {
            SetMessage(error, true);
            return;
        }
        config_original_ = config_values_;
    }

    if (!telemetry_.connected()) {
        if (write_file) {
            SetMessage("已写入 " + env_.config_path + "（游戏未运行，下次启动生效）" +
                       (appended.empty() ? "" : "，新增 " + std::to_string(appended.size()) +
                                                   " 个配置项"));
        } else {
            SetMessage("游戏未运行，无法实时应用；按 w 可仅写入文件", true);
        }
        return;
    }

    const std::vector<std::string> failures = telemetry_.ApplyConfig(updates);
    if (failures.empty()) {
        SetMessage("已" + std::string(write_file ? "写入并应用 " : "实时应用 ") +
                   std::to_string(updates.size()) + " 项，游戏内即时生效" +
                   (appended.empty() ? "" : "（其中 " + std::to_string(appended.size()) +
                                               " 项是本次新增到配置文件）"));
    } else {
        SetMessage("部分项失败：" + failures.front() +
                       (failures.size() > 1
                            ? "（共 " + std::to_string(failures.size()) + " 项失败）"
                            : ""),
                   true);
    }
}

void Tui::ResetConfigDefaults()
{
    RefreshConfigValues(true);
    SetMessage("已载入全部默认值（内存中），按 w 写入并应用，按 r 放弃改动");
}

void Tui::SaveOptionsQuietly()
{
    std::string error;
    if (!SaveLaunchOptions(options_, error)) {
        SetMessage(error, true);
    }
}

void Tui::LaunchGame()
{
    SaveOptionsQuietly();
    std::string error;
    const std::vector<std::string> args = BuildGameArgs(options_);
    const int pid = SpawnGame(root_, args, error);
    if (pid <= 0) {
        SetMessage(error, true);
        return;
    }
    game_pid_ = pid;
    game_running_ = true;
    game_exit_code_ = 0;
    build_tail_.clear();
    SetMessage("游戏已启动（pid " + std::to_string(pid) + "）：build/main " +
               JoinArgs(args) + "\n输出日志：build/game.log");
}

void Tui::RunBuild()
{
    SetMessage("构建中…（cmake + make），请稍候");
    screen_.Resize(term::Width(), term::Height());
    Draw();

    std::string log;
    const std::string error = BuildGame(root_, log);
    env_ = QueryEnv(root_);

    std::vector<std::string> lines = SplitMessage(log);
    std::string tail;
    const int keep = 3;
    for (int i = std::max(0, static_cast<int>(lines.size()) - keep);
         i < static_cast<int>(lines.size()); ++i) {
        if (lines[static_cast<std::size_t>(i)].empty()) {
            continue;
        }
        if (!tail.empty()) {
            tail += "\n";
        }
        tail += lines[static_cast<std::size_t>(i)];
    }
    build_tail_ = tail;

    if (error.empty()) {
        SetMessage("构建完成：" + env_.game_path + (tail.empty() ? "" : "\n" + tail));
    } else {
        SetMessage(error + (tail.empty() ? "" : "\n" + tail), true);
    }
}
