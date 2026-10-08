#ifndef BTW_RENDER_GPU_H
#define BTW_RENDER_GPU_H

#include <SDL3/SDL.h>

// ==================== SDL3 GPU 资源辅助 ====================
// 全屏四边形管线：canvas 纹理（调色）+ 战场网格叠加，两段 MSL 着色器。

extern const char *VS_QUAD_MSL;
extern const char *FS_GRID_MSL;
extern const char *FS_CANVAS_MSL;

// fs_grid 的狙击引力透镜：最多同时作用的狙击数（超出部分这一帧不扭曲）
constexpr int MAX_LENS_SNIPERS = 16;
// 狙击黑洞对战场网格的影响半径（像素）：超出此距离网格不受影响
constexpr float LENS_RANGE_PIXELS = 280.0f;
// 黑洞中心处网格线的最大位移（像素）：越靠近中心扭曲越强（falloff² 衰减）
constexpr float LENS_STRENGTH_PIXELS = 42.0f;

// 全屏着色器统一变量（逻辑分辨率 + 战场区域 + 输出分辨率 + 狙击引力透镜）
struct UIUniform
{
    float logical_width;
    float logical_height;
    float frame_x;
    float frame_y;
    float frame_size;
    float output_width;
    float output_height;
    float padding;
    // 狙击引力黑洞（fs_grid 用）：战场网格朝每个狙击位置弯曲
    float lens_count;     // 有效狙击数（0 = 不扭曲）
    float lens_range;     // 影响半径（像素）
    float lens_strength;  // 中心最大位移（像素）
    float lens_pad;       // 对齐占位
    float lens_x[MAX_LENS_SNIPERS];
    float lens_y[MAX_LENS_SNIPERS];
};

SDL_GPUShader *CreateShader(SDL_GPUDevice *device, const char *code,
                            const char *entrypoint, SDL_GPUShaderStage stage,
                            Uint32 num_uniform_buffers = 0, Uint32 num_samplers = 0);
SDL_GPUBuffer *CreateGPUBuffer(SDL_GPUDevice *device, SDL_GPUBufferUsageFlags usage,
                               Uint32 size);
SDL_GPUTransferBuffer *CreateTransferBuffer(SDL_GPUDevice *device, Uint32 size,
                                            SDL_GPUTransferBufferUsage usage);
bool UploadBufferData(SDL_GPUDevice *device, SDL_GPUBuffer *buffer,
                      const void *data, Uint32 size);
SDL_GPUGraphicsPipeline *CreateFullscreenPipeline(
    SDL_GPUDevice *device, SDL_GPUShader *vertex_shader, SDL_GPUShader *fragment_shader,
    SDL_GPUTextureFormat format,
    SDL_GPUSampleCount sample_count = SDL_GPU_SAMPLECOUNT_1);

#endif  // BTW_RENDER_GPU_H
