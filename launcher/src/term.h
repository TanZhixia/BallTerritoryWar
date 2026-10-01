#ifndef BTW_LAUNCHER_TERM_H
#define BTW_LAUNCHER_TERM_H

#include <string>
#include <vector>

// ==================== 终端抽象层 ====================
// 职责：raw 模式开关、按键解码（含方向键/翻页键的 CSI 序列）、
// UTF-8 显示宽度计算、单元格屏幕缓冲（可任意位置覆写、整帧刷新）。

namespace term {

struct Key
{
    enum class Type
    {
        None, Char, Up, Down, Left, Right, Enter, Escape, Backspace,
        Tab, Space, PageUp, PageDown, Home, End, Delete, Unknown,
    };
    Type type = Type::None;
    char ch = 0;          // ASCII 时的单字节（type == Char 且是 ASCII 时有效）
    std::string utf8;     // type == Char 时的完整 UTF-8 字符（含 ASCII 与中文）
    bool valid() const { return type != Type::None; }
};

bool IsTty();
// 进入 raw 模式（关闭行缓冲/回显）；非 tty 或失败返回 false
bool EnterRawMode();
void LeaveRawMode();
// 注册 SIGWINCH 处理，Resized() 消费一次窗口变化标记
void InstallResizeHandler();
bool Resized();

int Width();
int Height();

// 阻塞等待按键，最多 timeout_ms 毫秒；超时返回 type == None
Key ReadKey(int timeout_ms);

// ---------- UTF-8 与显示宽度 ----------
int DisplayWidth(const std::string &utf8);  // CJK 记 2 列，其余 1 列
std::string TruncateToWidth(const std::string &utf8, int max_width);
// 截断到 max_width，不足处用 '…' 或 "..." 提示省略
std::string EllipsizeToWidth(const std::string &utf8, int max_width);
std::string PadToWidth(const std::string &utf8, int width);
// 删除最后一个完整 UTF-8 字符（退格键用，避免把中文砍成半个字节）
void PopLastUtf8Char(std::string &text);

// ---------- ANSI ----------
namespace ansi {
std::string Reset();
std::string Bold();
std::string Dim();
std::string Underline();
std::string Reverse();
std::string Fg(int color_index);  // 基本 8/16 色（0-15）
std::string Bg(int color_index);
std::string Fg256(int color_index);
std::string FgRgb(int r, int g, int b);
std::string BgRgb(int r, int g, int b);
std::string AltScreen(bool on);
std::string HideCursor();
std::string ShowCursor();
}  // namespace ansi

// ---------- 屏幕缓冲 ----------
// 单元格网格：先画边框再填内容、任意位置覆写都不会互相干扰；
// 只在 Flush() 时整帧输出，减少闪烁。
class Screen
{
public:
    void Resize(int width, int height);
    void Clear();

    int width() const { return width_; }
    int height() const { return height_; }

    // 注册一种样式（ANSI 前缀，如 ansi::Fg(10) + ansi::Bold()），返回样式 id
    int Style(const std::string &ansi_prefix);

    void Put(int x, int y, const std::string &text, int style = 0);
    void Fill(int x, int y, int width, int height, const std::string &ch, int style = 0);
    void HLine(int x, int y, int length, const std::string &ch, int style = 0);
    // 画带标题的边框（只画框，内容由调用方填充）
    void Box(int x, int y, int width, int height, const std::string &title,
             int border_style = 0, int title_style = 0);

    void Flush();

private:
    struct Cell
    {
        unsigned int cp = ' ';
        int style = 0;
        bool cont = false;  // 宽字符占用的第二格
    };
    std::vector<Cell> cells_;
    std::vector<std::string> styles_;
    int width_ = 0;
    int height_ = 0;

    Cell *At(int x, int y);
};

// ASCII 边框模式（--ascii，供不支持 Unicode 制表符的终端使用）
void SetAsciiFrames(bool ascii);
bool AsciiFrames();

}  // namespace term

#endif  // BTW_LAUNCHER_TERM_H
