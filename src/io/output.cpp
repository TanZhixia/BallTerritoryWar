#include "io/output.h"

#include <dirent.h>
#include <filesystem>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

std::string g_music_playlist_path;  // 音乐播放列表路径（退出时清理）

std::string NormalizeOutputPath(const std::string &path)
{
    if (path.empty()) {
        return "output.mp4";
    }
    static const char *const kKnown[] = {".mp4", ".m4v",  ".mkv", ".mov",
                                         ".avi", ".webm", ".ts",  ".flv"};
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    for (const char *ext : kKnown) {
        const std::size_t len = std::strlen(ext);
        if (lower.size() > len && lower.compare(lower.size() - len, len, ext) == 0) {
            return path;  // 已有可识别扩展名
        }
    }
    return path + ".mp4";  // 例如 “S2第二集” / “S2.第二集” → 追加 .mp4
}

// 音乐目录：~/.ball/music（用户数据目录，不随构建产物被清理）
static std::string GetMusicDir()
{
    const char *home = SDL_GetUserFolder(SDL_FOLDER_HOME);
    if (!home || !home[0]) {
        home = std::getenv("HOME");
    }
    if (!home || !home[0]) {
        return std::string();
    }
    std::string home_str(home);
    while (!home_str.empty() && home_str.back() == '/') {
        home_str.pop_back();  // SDL_GetUserFolder 返回的 home 可能带尾斜杠
    }
    return home_str + "/.ball/music";
}

// 确保音乐目录存在（首次运行自动创建）
static bool EnsureMusicDir()
{
    const std::string dir = GetMusicDir();
    if (dir.empty()) {
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return !ec;
}

static bool BuildMusicPlaylist(std::string &out_path)
{
    const std::string music_dir = GetMusicDir();
    if (music_dir.empty()) {
        return false;
    }

    DIR *dir = opendir(music_dir.c_str());
    if (!dir) {
        return false;
    }

    std::vector<std::string> files;
    while (dirent *ent = readdir(dir)) {
        const std::string name = ent->d_name;
        if (name == "." || name == "..") {
            continue;
        }
        const std::size_t dot = name.rfind('.');
        if (dot == std::string::npos) {
            continue;
        }
        std::string ext = name.substr(dot + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == "mp3" || ext == "m4a" || ext == "wav" ||
            ext == "flac" || ext == "aac" || ext == "ogg") {
            files.push_back(music_dir + "/" + name);
        }
    }
    closedir(dir);
    if (files.empty()) {
        return false;
    }

    std::srand(static_cast<unsigned int>(SDL_GetTicks()));
    char path[256];
    std::snprintf(path, sizeof(path), "/tmp/btw_music_%d.txt", getpid());
    FILE *f = std::fopen(path, "w");
    if (!f) {
        return false;
    }
    std::fprintf(f, "ffconcat version 1.0\n");
    for (int i = 0; i < 120; ++i) {
        const std::string &file = files[std::rand() % files.size()];
        std::string escaped;
        for (char ch : file) {
            if (ch == '\'') {
                escaped += "'\\''";
            } else {
                escaped += ch;
            }
        }
        std::fprintf(f, "file '%s'\n", escaped.c_str());
    }
    std::fclose(f);
    out_path = path;
    return true;
}

FILE *StartRecorder(int width, int height, bool with_music, const char *output_path)
{
    char command[1024];
    // 兜底：无论调用方传什么，都保证有可识别的容器扩展名（幂等）
    const std::string normalized = NormalizeOutputPath(output_path ? output_path : "");
    // 输出路径进入 shell 命令：转义单引号防注入/空格截断
    std::string escaped;
    for (const char *p = normalized.c_str(); *p; ++p) {
        if (*p == '\'') {
            escaped += "'\\''";
        } else {
            escaped += *p;
        }
    }
    if (with_music && EnsureMusicDir() && BuildMusicPlaylist(g_music_playlist_path)) {
        std::snprintf(command, sizeof(command),
                      "ffmpeg -y -loglevel error -f rawvideo -pix_fmt rgb24 -s %dx%d -r 60 "
                      "-i - -safe 0 -i '%s' "
                      "-map 0:v -map 1:a -c:v libx264 -preset ultrafast -pix_fmt yuv420p "
                      "-c:a aac -b:a 192k -shortest '%s'",  // -shortest：视频为主，音轨(数小时playlist)更长，以视频为界完整收尾
                      width, height, g_music_playlist_path.c_str(), escaped.c_str());
        SDL_Log("Music audio will be mixed into %s", escaped.c_str());
    } else {
        std::snprintf(command, sizeof(command),
                      "ffmpeg -y -loglevel error -f rawvideo -pix_fmt rgb24 -s %dx%d -r 60 "
                      "-i - -an -c:v libx264 -preset ultrafast -pix_fmt yuv420p '%s'",
                      width, height, escaped.c_str());
    }
    return popen(command, "w");
}

void WriteReadbackToRecorder(FILE *recorder, const void *data, int width, int height,
                                    SDL_GPUTextureFormat format)
{
    if (!recorder) {
        return;
    }

    const bool bgra = format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM ||
                      format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB;
    const Uint8 *src = static_cast<const Uint8 *>(data);
    static std::vector<Uint8> rgb;
    rgb.resize(static_cast<std::size_t>(width) * height * 3);

    for (int i = 0; i < width * height; ++i) {
        if (bgra) {
            rgb[static_cast<std::size_t>(i) * 3 + 0] = src[2];
            rgb[static_cast<std::size_t>(i) * 3 + 1] = src[1];
            rgb[static_cast<std::size_t>(i) * 3 + 2] = src[0];
        } else {
            rgb[static_cast<std::size_t>(i) * 3 + 0] = src[0];
            rgb[static_cast<std::size_t>(i) * 3 + 1] = src[1];
            rgb[static_cast<std::size_t>(i) * 3 + 2] = src[2];
        }
        src += 4;
    }

    std::fwrite(rgb.data(), 1, rgb.size(), recorder);
}
