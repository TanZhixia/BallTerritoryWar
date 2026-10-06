#include "term.h"

#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace term {

namespace {

termios g_original;
bool g_raw = false;
bool g_ascii = false;

std::string BasicColor(int idx, int base)
{
    if (idx < 0) {
        idx = 0;
    }
    idx %= 16;
    char buf[16];
    if (idx < 8) {
        std::snprintf(buf, sizeof(buf), "\x1b[%dm", base + idx);
    } else {
        std::snprintf(buf, sizeof(buf), "\x1b[%dm", base + 60 + (idx - 8));
    }
    return std::string(buf);
}

std::string BasicFg(int idx) { return BasicColor(idx, 30); }

// 解码一个 UTF-8 码点，i 前进到下一个字符；非法字节按 U+FFFD 处理
unsigned int NextCodePoint(const std::string &s, std::size_t &i)
{
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
        ++i;
        return c;
    }
    int extra = 0;
    unsigned int cp = 0;
    if ((c & 0xE0) == 0xC0) {
        extra = 1;
        cp = c & 0x1Fu;
    } else if ((c & 0xF0) == 0xE0) {
        extra = 2;
        cp = c & 0x0Fu;
    } else if ((c & 0xF8) == 0xF0) {
        extra = 3;
        cp = c & 0x07u;
    } else {
        ++i;
        return 0xFFFD;
    }
    if (i + static_cast<std::size_t>(extra) >= s.size()) {
        ++i;
        return 0xFFFD;
    }
    for (int k = 0; k < extra; ++k) {
        const unsigned char cc = static_cast<unsigned char>(s[i + 1 + k]);
        if ((cc & 0xC0) != 0x80) {
            ++i;
            return 0xFFFD;
        }
        cp = (cp << 6) | (cc & 0x3Fu);
    }
    i += static_cast<std::size_t>(extra) + 1;
    return cp;
}

void AppendUtf8(std::string &out, unsigned int cp)
{
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// 码点显示宽度：CJK / 全角 / Emoji 记 2 列，控制字符记 0 列
int CodePointWidth(unsigned int cp)
{
    if (cp == 0 || cp < 32 || (cp >= 0x7F && cp < 0xA0)) {
        return 0;
    }
    if ((cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0x303E) ||
        (cp >= 0x3041 && cp <= 0x33FF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
        (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xA000 && cp <= 0xA4CF) ||
        (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
        (cp >= 0xFE30 && cp <= 0xFE4F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
        (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x1F300 && cp <= 0x1F64F) ||
        (cp >= 0x20000 && cp <= 0x3FFFD)) {
        return 2;
    }
    return 1;
}

}  // namespace

bool IsTty()
{
    return ::isatty(STDIN_FILENO) == 1 && ::isatty(STDOUT_FILENO) == 1;
}

bool EnterRawMode()
{
    if (!IsTty()) {
        return false;
    }
    if (::tcgetattr(STDIN_FILENO, &g_original) != 0) {
        return false;
    }
    termios raw = g_original;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= CS8;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (::tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
        return false;
    }
    g_raw = true;
    return true;
}

void LeaveRawMode()
{
    if (g_raw) {
        ::tcsetattr(STDIN_FILENO, TCSANOW, &g_original);
        g_raw = false;
    }
}

int Width()
{
    winsize ws;
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        return static_cast<int>(ws.ws_col);
    }
    return 100;
}

int Height()
{
    winsize ws;
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) {
        return static_cast<int>(ws.ws_row);
    }
    return 30;
}

Key ReadKey(int timeout_ms)
{
    Key key;
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    if (::select(STDIN_FILENO + 1, &readfds, nullptr, nullptr, &tv) <= 0) {
        return key;
    }

    unsigned char c = 0;
    if (::read(STDIN_FILENO, &c, 1) != 1) {
        return key;
    }

    if (c == 0x1B) {
        unsigned char seq[8] = {};
        int n = 0;
        while (n < 7) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(STDIN_FILENO, &fds);
            timeval wait;
            wait.tv_sec = 0;
            wait.tv_usec = 30000;  // 30ms 内没有后续字节则视为裸 ESC
            if (::select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &wait) <= 0) {
                break;
            }
            if (::read(STDIN_FILENO, &seq[n], 1) != 1) {
                break;
            }
            ++n;
            if (n >= 2 && seq[n - 1] >= '@' && seq[n - 1] <= '~') {
                break;
            }
        }
        if (n == 0) {
            key.type = Key::Type::Escape;
            return key;
        }
        if (seq[0] == '[' || seq[0] == 'O') {
            if (n >= 2) {
                switch (seq[n - 1]) {
                    case 'A': key.type = Key::Type::Up; return key;
                    case 'B': key.type = Key::Type::Down; return key;
                    case 'C': key.type = Key::Type::Right; return key;
                    case 'D': key.type = Key::Type::Left; return key;
                    case 'H': key.type = Key::Type::Home; return key;
                    case 'F': key.type = Key::Type::End; return key;
                    case '~': {
                        std::string digits(reinterpret_cast<char *>(seq + 1),
                                           static_cast<std::size_t>(n - 2));
                        switch (std::atoi(digits.c_str())) {
                            case 1: case 7: key.type = Key::Type::Home; return key;
                            case 4: case 8: key.type = Key::Type::End; return key;
                            case 3: key.type = Key::Type::Delete; return key;
                            case 5: key.type = Key::Type::PageUp; return key;
                            case 6: key.type = Key::Type::PageDown; return key;
                            default: key.type = Key::Type::Unknown; return key;
                        }
                    }
                    default: break;
                }
            }
            key.type = Key::Type::Unknown;
            return key;
        }
        key.type = Key::Type::Escape;
        return key;
    }

    switch (c) {
        case '\r':
        case '\n':
            key.type = Key::Type::Enter;
            return key;
        case 0x7F:
        case 0x08:
            key.type = Key::Type::Backspace;
            return key;
        case '\t':
            key.type = Key::Type::Tab;
            return key;
        case ' ':
            key.type = Key::Type::Space;
            return key;
        default:
            break;
    }
    if (c >= 32 && c < 127) {
        key.type = Key::Type::Char;
        key.ch = static_cast<char>(c);
        key.utf8.assign(1, static_cast<char>(c));
        return key;
    }
    if (c >= 0xC0) {
        // UTF-8 多字节字符（中文/emoji 等）：按首字节推出长度，补齐后续字节
        int extra = 0;
        if ((c & 0xE0) == 0xC0) {
            extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            extra = 2;
        } else if ((c & 0xF8) == 0xF0) {
            extra = 3;
        }
        if (extra > 0) {
            std::string sequence(1, static_cast<char>(c));
            bool complete = true;
            for (int k = 0; k < extra; ++k) {
                fd_set fds;
                FD_ZERO(&fds);
                FD_SET(STDIN_FILENO, &fds);
                timeval wait = {};
                wait.tv_sec = 0;
                wait.tv_usec = 30000;  // 终端会把同一字符的字节连续发出
                if (::select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &wait) <= 0) {
                    complete = false;
                    break;
                }
                unsigned char next = 0;
                if (::read(STDIN_FILENO, &next, 1) != 1) {
                    complete = false;
                    break;
                }
                sequence += static_cast<char>(next);
            }
            if (complete) {
                key.type = Key::Type::Char;
                key.utf8 = sequence;
                return key;
            }
        }
    }
    key.type = Key::Type::Unknown;
    return key;
}

void PopLastUtf8Char(std::string &text)
{
    if (text.empty()) {
        return;
    }
    std::size_t index = text.size() - 1;
    while (index > 0 && (static_cast<unsigned char>(text[index]) & 0xC0) == 0x80) {
        --index;  // 跳过续字节，定位到首字节
    }
    text.erase(index);
}

int DisplayWidth(const std::string &utf8)
{
    int width = 0;
    std::size_t i = 0;
    while (i < utf8.size()) {
        width += CodePointWidth(NextCodePoint(utf8, i));
    }
    return width;
}

std::string TruncateToWidth(const std::string &utf8, int max_width)
{
    if (max_width <= 0) {
        return std::string();
    }
    std::string out;
    int width = 0;
    std::size_t i = 0;
    while (i < utf8.size()) {
        const std::size_t start = i;
        const unsigned int cp = NextCodePoint(utf8, i);
        const int w = CodePointWidth(cp);
        if (width + w > max_width) {
            break;
        }
        out.append(utf8, start, i - start);
        width += w;
    }
    return out;
}

std::string EllipsizeToWidth(const std::string &utf8, int max_width)
{
    if (DisplayWidth(utf8) <= max_width) {
        return utf8;
    }
    const std::string mark = AsciiFrames() ? ".." : "…";
    const int mark_w = DisplayWidth(mark);
    if (max_width <= mark_w) {
        return TruncateToWidth(utf8, max_width);
    }
    return TruncateToWidth(utf8, max_width - mark_w) + mark;
}

std::string PadToWidth(const std::string &utf8, int width)
{
    const int current = DisplayWidth(utf8);
    if (current >= width) {
        return utf8;
    }
    return utf8 + std::string(static_cast<std::size_t>(width - current), ' ');
}

void SetAsciiFrames(bool ascii)
{
    g_ascii = ascii;
}

bool AsciiFrames()
{
    return g_ascii;
}

// ==================== ANSI ====================

namespace ansi {

std::string Reset() { return "\x1b[0m"; }
std::string Bold() { return "\x1b[1m"; }
std::string Reverse() { return "\x1b[7m"; }

std::string Fg(int color_index) { return BasicFg(color_index); }

std::string FgRgb(int r, int g, int b)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "\x1b[38;2;%d;%d;%dm", r, g, b);
    return std::string(buf);
}

std::string BgRgb(int r, int g, int b)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "\x1b[48;2;%d;%d;%dm", r, g, b);
    return std::string(buf);
}

std::string AltScreen(bool on) { return on ? "\x1b[?1049h" : "\x1b[?1049l"; }
std::string HideCursor() { return "\x1b[?25l"; }
std::string ShowCursor() { return "\x1b[?25h"; }

}  // namespace ansi

// ==================== Screen ====================

void Screen::Resize(int width, int height)
{
    width_ = width > 0 ? width : 1;
    height_ = height > 0 ? height : 1;
    cells_.assign(static_cast<std::size_t>(width_) * height_, Cell{});
    if (styles_.empty()) {
        styles_.push_back(std::string());  // 0 = 默认样式
    }
}

void Screen::Clear()
{
    cells_.assign(static_cast<std::size_t>(width_) * height_, Cell{});
}

int Screen::Style(const std::string &ansi_prefix)
{
    for (std::size_t i = 0; i < styles_.size(); ++i) {
        if (styles_[i] == ansi_prefix) {
            return static_cast<int>(i);
        }
    }
    styles_.push_back(ansi_prefix);
    return static_cast<int>(styles_.size() - 1);
}

Screen::Cell *Screen::At(int x, int y)
{
    if (x < 0 || y < 0 || x >= width_ || y >= height_) {
        return nullptr;
    }
    return &cells_[static_cast<std::size_t>(y) * width_ + x];
}

void Screen::Put(int x, int y, const std::string &text, int style)
{
    std::size_t i = 0;
    int cx = x;
    while (i < text.size()) {
        const unsigned int cp = NextCodePoint(text, i);
        if (cp == '\n') {
            continue;
        }
        const int w = CodePointWidth(cp);
        if (w == 0) {
            continue;
        }
        if (cx >= width_) {
            break;
        }
        if (Cell *cell = At(cx, y)) {
            cell->cp = cp;
            cell->style = style;
            cell->cont = false;
        }
        if (w == 2) {
            if (Cell *cont = At(cx + 1, y)) {
                cont->cp = 0;
                cont->style = style;
                cont->cont = true;
            }
        }
        cx += w;
    }
}

void Screen::Fill(int x, int y, int width, int height, const std::string &ch, int style)
{
    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            Put(x + col, y + row, ch, style);
        }
    }
}

void Screen::HLine(int x, int y, int length, const std::string &ch, int style)
{
    for (int i = 0; i < length; ++i) {
        Put(x + i, y, ch, style);
    }
}

void Screen::Box(int x, int y, int width, int height, const std::string &title,
                 int border_style, int title_style)
{
    if (width < 4 || height < 2) {
        return;
    }
    const bool ascii = AsciiFrames();
    const std::string tl = ascii ? "+" : "╭";
    const std::string tr = ascii ? "+" : "╮";
    const std::string bl = ascii ? "+" : "╰";
    const std::string br = ascii ? "+" : "╯";
    const std::string hz = ascii ? "-" : "─";
    const std::string vt = ascii ? "|" : "│";

    Put(x, y, tl, border_style);
    if (!title.empty() && width > 8) {
        const std::string label = " " + EllipsizeToWidth(title, width - 6) + " ";
        Put(x + 1, y, label, title_style);
        const int used = 1 + DisplayWidth(label);
        HLine(x + used, y, width - used - 1, hz, border_style);
    } else {
        HLine(x + 1, y, width - 2, hz, border_style);
    }
    Put(x + width - 1, y, tr, border_style);

    for (int row = 1; row < height - 1; ++row) {
        Put(x, y + row, vt, border_style);
        Put(x + width - 1, y + row, vt, border_style);
    }

    Put(x, y + height - 1, bl, border_style);
    HLine(x + 1, y + height - 1, width - 2, hz, border_style);
    Put(x + width - 1, y + height - 1, br, border_style);
}

void Screen::Flush()
{
    std::string out = "\x1b[H";
    for (int y = 0; y < height_; ++y) {
        int last = -1;
        for (int x = 0; x < width_; ++x) {
            const Cell &cell = cells_[static_cast<std::size_t>(y) * width_ + x];
            if (cell.cont) {
                continue;
            }
            if (cell.cp != ' ' || cell.style != 0) {
                last = x;
            }
        }
        int current_style = -1;
        for (int x = 0; x <= last; ++x) {
            const Cell &cell = cells_[static_cast<std::size_t>(y) * width_ + x];
            if (cell.cont) {
                continue;
            }
            if (cell.style != current_style) {
                out += ansi::Reset();
                if (cell.style >= 0 && cell.style < static_cast<int>(styles_.size())) {
                    out += styles_[static_cast<std::size_t>(cell.style)];
                }
                current_style = cell.style;
            }
            AppendUtf8(out, cell.cp);
        }
        out += ansi::Reset();
        out += "\x1b[K";
        if (y + 1 < height_) {
            out += "\r\n";
        }
    }
    std::fwrite(out.data(), 1, out.size(), stdout);
    std::fflush(stdout);
}

}  // namespace term
