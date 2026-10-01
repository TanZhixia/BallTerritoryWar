#include "project.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#include "config.h"

namespace {

bool IsProjectRoot(const std::string &dir)
{
    if (dir.empty()) {
        return false;
    }
    return std::filesystem::exists(dir + "/CMakeLists.txt") &&
           std::filesystem::exists(dir + "/src/main.cpp");
}

// 从 path 起逐级向上查找项目根
std::string AscendToRoot(std::string path)
{
    for (int i = 0; i < 12 && !path.empty(); ++i) {
        if (IsProjectRoot(path)) {
            return path;
        }
        const std::string parent = std::filesystem::path(path).parent_path().string();
        if (parent == path) {
            break;
        }
        path = parent;
    }
    return std::string();
}

std::string ExecutablePath()
{
#if defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0) {
        char resolved[4096];
        if (::realpath(buf, resolved) != nullptr) {
            return std::string(resolved);
        }
        return std::string(buf);
    }
#else
    char buf[4096];
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        return std::string(buf);
    }
#endif
    return std::string();
}

bool CommandExists(const std::string &name)
{
    const std::string cmd = "command -v " + name + " >/dev/null 2>&1";
    return std::system(cmd.c_str()) == 0;
}

int CountMusicFiles(const std::string &dir, std::string &resolved)
{
    resolved = dir;
    if (!std::filesystem::is_directory(dir)) {
        return 0;
    }
    static const char *const kExtensions[] = {".mp3", ".m4a", ".wav",
                                              ".flac", ".aac", ".ogg"};
    int count = 0;
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        for (const char *candidate : kExtensions) {
            if (ext == candidate) {
                ++count;
                break;
            }
        }
    }
    return count;
}

std::string HomeDir()
{
    const char *home = std::getenv("HOME");
    return home ? std::string(home) : std::string();
}

}  // namespace

std::string ProjectRoot()
{
    if (const char *dir = std::getenv("BTW_PROJECT_DIR")) {
        if (dir[0] != '\0' && IsProjectRoot(dir)) {
            return dir;
        }
    }
    const std::string exe = ExecutablePath();
    if (!exe.empty()) {
        // build/btw-launcher → 从所在目录向上找
        const std::string found = AscendToRoot(
            std::filesystem::path(exe).parent_path().string());
        if (!found.empty()) {
            return found;
        }
    }
    std::error_code ec;
    const std::string cwd = std::filesystem::current_path(ec).string();
    const std::string found = AscendToRoot(cwd);
    if (!found.empty()) {
        return found;
    }
    return cwd;
}

EnvStatus QueryEnv(const std::string &root)
{
    EnvStatus env;
    env.root = root;
    env.game_path = root + "/build/main";
    env.game_exists = std::filesystem::exists(env.game_path);
    env.ffmpeg = CommandExists("ffmpeg");
    env.music_dir = HomeDir() + "/.ball/music";
    env.music_count = CountMusicFiles(env.music_dir, env.music_dir);
    env.config_path = ConfigFilePath();
    env.config_exists = std::filesystem::exists(env.config_path);
    env.launcher_config_path = LauncherConfPath();
    return env;
}

std::string GameLogPath(const std::string &root)
{
    return root + "/build/game.log";
}

std::string BuildGame(const std::string &root, std::string &log)
{
    const std::string build_dir = root + "/build";
    std::error_code ec;
    std::filesystem::create_directories(build_dir, ec);

    const unsigned int cores = std::max(1u, std::thread::hardware_concurrency());
    const std::string cmd = "cd '" + build_dir + "' && cmake .. && make -j" +
                            std::to_string(cores) + " 2>&1";
    FILE *pipe = ::popen(cmd.c_str(), "r");
    if (!pipe) {
        return "无法执行构建命令";
    }
    log.clear();
    char buf[4096];
    while (std::fgets(buf, sizeof(buf), pipe) != nullptr) {
        log += buf;
    }
    const int status = ::pclose(pipe);
    if (status != 0) {
        return "构建失败（退出码 " + std::to_string(status / 256) + "）";
    }
    return std::string();
}

int SpawnGame(const std::string &root, const std::vector<std::string> &args,
              std::string &error)
{
    const std::string game = root + "/build/main";
    if (!std::filesystem::exists(game)) {
        error = "未找到游戏程序：" + game + "（请先构建）";
        return -1;
    }

    std::vector<std::string> argv_storage;
    argv_storage.push_back(game);
    for (const std::string &a : args) {
        argv_storage.push_back(a);
    }
    std::vector<char *> argv;
    for (std::string &s : argv_storage) {
        argv.push_back(const_cast<char *>(s.c_str()));
    }
    argv.push_back(nullptr);

    const pid_t pid = ::fork();
    if (pid < 0) {
        error = std::string("fork 失败：") + std::strerror(errno);
        return -1;
    }
    if (pid == 0) {
        // 子进程：切到项目根目录，输出重定向到 build/game.log
        if (::chdir(root.c_str()) != 0) {
            _exit(127);
        }
        const std::string log_path = GameLogPath(root);
        const int fd = ::open(log_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            ::dup2(fd, STDOUT_FILENO);
            ::dup2(fd, STDERR_FILENO);
            ::close(fd);
        }
        ::execv(game.c_str(), argv.data());
        _exit(127);
    }
    error.clear();
    return static_cast<int>(pid);
}

bool ReapGame(int pid, int &exit_code)
{
    if (pid <= 0) {
        return false;
    }
    int status = 0;
    const pid_t result = ::waitpid(pid, &status, WNOHANG);
    if (result == pid) {
        exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        return true;
    }
    return false;
}

std::vector<std::string> TailGameLog(const std::string &root, int max_lines)
{
    std::vector<std::string> lines;
    std::ifstream in(GameLogPath(root));
    if (!in) {
        return lines;
    }
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line);
        if (static_cast<int>(lines.size()) > max_lines * 4) {
            lines.erase(lines.begin(), lines.begin() + max_lines * 2);
        }
    }
    if (static_cast<int>(lines.size()) > max_lines) {
        lines.erase(lines.begin(), lines.end() - max_lines);
    }
    return lines;
}
