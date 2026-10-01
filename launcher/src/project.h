#ifndef BTW_LAUNCHER_PROJECT_H
#define BTW_LAUNCHER_PROJECT_H

#include <string>
#include <vector>

// ==================== 项目定位 / 环境探测 / 进程管理 ====================

struct EnvStatus
{
    std::string root;                 // 项目根目录
    std::string game_path;            // <root>/build/main
    bool game_exists = false;         // 游戏是否已构建
    bool ffmpeg = false;              // ffmpeg 是否可用（录像依赖）
    int music_count = 0;              // ~/.ball/music 下的音频文件数
    std::string music_dir;
    std::string config_path;          // ~/.ball/config.yaml
    bool config_exists = false;
    std::string launcher_config_path;
};

// 定位项目根：$BTW_PROJECT_DIR → 从可执行文件向上找 CMakeLists.txt →
// 从当前工作目录向上找
std::string ProjectRoot();
EnvStatus QueryEnv(const std::string &root);
std::string GameLogPath(const std::string &root);

// 在 <root>/build 执行 cmake .. && make；成功返回空串并把输出写入 log
std::string BuildGame(const std::string &root, std::string &log);

// fork/exec 启动游戏（工作目录 = 项目根，stdout/stderr 重定向到 build/game.log）
// 成功返回 pid > 0；失败返回 -1 并填 error
int SpawnGame(const std::string &root, const std::vector<std::string> &args,
              std::string &error);

// 非阻塞回收：子进程已退出返回 true 并填 exit_code
bool ReapGame(int pid, int &exit_code);
// 读取游戏日志尾部若干行
std::vector<std::string> TailGameLog(const std::string &root, int max_lines);

#endif  // BTW_LAUNCHER_PROJECT_H
