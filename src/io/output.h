#ifndef BTW_IO_OUTPUT_H
#define BTW_IO_OUTPUT_H

#include <cstdio>
#include <string>

#include <SDL3/SDL.h>

// ==================== 录像输出（ffmpeg 管道） ====================
// 逐帧把 GPU 回读的 RGBA/BGRA 像素转成 rgb24 写入 ffmpeg stdin；
// 可选把 ~/.ball/music 下的音频随机播放列表混入音轨。

extern std::string g_music_playlist_path;  // 音乐播放列表路径（退出时清理）

// 规范化录像输出名：没有可识别扩展名（mp4/mkv/mov/...）时补上 .mp4。
// ffmpeg 靠扩展名决定封装格式，缺扩展名会直接报
// "Unable to choose an output format" 并放弃录制。
std::string NormalizeOutputPath(const std::string &path);

// 启动 ffmpeg 子进程；失败（如未安装 ffmpeg）返回 nullptr
FILE *StartRecorder(int width, int height, bool with_music, const char *output_path);
void WriteReadbackToRecorder(FILE *recorder, const void *data, int width, int height,
                             SDL_GPUTextureFormat format);

#endif  // BTW_IO_OUTPUT_H
