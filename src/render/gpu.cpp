#include "render/gpu.h"
const char *VS_QUAD_MSL = R"(
#include <metal_stdlib>
using namespace metal;

struct VSInput
{
    float2 local_pos [[attribute(0)]];
};

struct VSOutput
{
    float4 position [[position]];
    float2 uv;
};

vertex VSOutput vs_quad(VSInput in [[stage_in]])
{
    VSOutput out;
    out.position = float4(in.local_pos, 0.0, 1.0);
    out.uv = in.local_pos * 0.5 + 0.5;
    return out;
}
)";

const char *FS_GRID_MSL = R"(
#include <metal_stdlib>
using namespace metal;

struct VSOutput
{
    float4 position [[position]];
    float2 uv;
};

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
    float lens_count;
    float lens_range;
    float lens_strength;
    float lens_pad;
    float lens_x[16];
    float lens_y[16];
};

fragment float4 fs_grid(VSOutput in [[stage_in]], constant UIUniform &uniform [[buffer(0)]])
{
    float px = in.uv.x * uniform.logical_width;
    float py = (1.0 - in.uv.y) * uniform.logical_height;
    float2 p = float2(px - uniform.frame_x, py - uniform.frame_y);

    // ---- 狙击引力黑洞：把网格采样点朝每个黑洞方向径向拉近，
    //      越靠近中心拉得越多（falloff²），网格线看起来被引力吸向黑洞 ----
    const int lens_n = int(uniform.lens_count);
    for (int i = 0; i < lens_n; ++i) {
        float2 c = float2(uniform.lens_x[i] - uniform.frame_x,
                          uniform.lens_y[i] - uniform.frame_y);
        float2 d = p - c;
        const float r = length(d);
        if (r >= uniform.lens_range || r < 0.001) {
            continue;
        }
        const float falloff = 1.0 - r / uniform.lens_range;  // 中心 1 → 边缘 0
        p += (d / r) * (uniform.lens_strength * falloff * falloff);
    }

    const float gx = p.x;
    const float gy = p.y;
    if (gx < 0.0 || gx >= uniform.frame_size || gy < 0.0 || gy >= uniform.frame_size) {
        discard_fragment();
    }

    const float cell = uniform.frame_size / 10.0;
    const float line = 1.0;
    float fx = fmod(gx, cell);
    float fy = fmod(gy, cell);
    if (fx >= line && fy >= line) {
        discard_fragment();
    }
    return float4(0.5, 0.5, 0.5, 1.0);
}
)";

const char *FS_CANVAS_MSL = R"(
#include <metal_stdlib>
using namespace metal;

struct VSOutput
{
    float4 position [[position]];
    float2 uv;
};

fragment float4 fs_canvas(VSOutput in [[stage_in]],
                          texture2d<float> canvas_texture [[texture(0)]],
                          sampler canvas_sampler [[sampler(0)]])
{
    float4 color = canvas_texture.sample(canvas_sampler, float2(in.uv.x, 1.0 - in.uv.y));

    // ---- 现代化调色：饱和度 + 对比度 ----
    float luma = dot(color.rgb, float3(0.299, 0.587, 0.114));
    color.rgb = mix(float3(luma), color.rgb, 1.12);       // 饱和度 +12%
    color.rgb = (color.rgb - 0.5) * 1.05 + 0.5;           // 对比度 +5%

    // ---- 亮部辉光（柔和 bloom 感）----
    float bright = max(max(color.r, color.g), color.b);
    color.rgb += max(bright - 0.78, 0.0) * 0.30 * color.rgb;

    // ---- 暗角（按屏幕比例修正，避免椭圆）----
    float2 p = in.uv - 0.5;
    float d = length(p * float2(1.0, 0.625));
    color.rgb *= 1.0 - 0.24 * smoothstep(0.42, 0.78, d);

    color.a = 1.0;
    return color;
}
)";

SDL_GPUShader *CreateShader(SDL_GPUDevice *device, const char *code,
                                   const char *entrypoint, SDL_GPUShaderStage stage,
                                   Uint32 num_uniform_buffers, Uint32 num_samplers)
{
    SDL_GPUShaderCreateInfo info = {};
    info.code = reinterpret_cast<const Uint8 *>(code);
    info.code_size = SDL_strlen(code);
    info.entrypoint = entrypoint;
    info.format = SDL_GPU_SHADERFORMAT_MSL;
    info.stage = stage;
    info.num_uniform_buffers = num_uniform_buffers;
    info.num_samplers = num_samplers;
    return SDL_CreateGPUShader(device, &info);
}

SDL_GPUBuffer *CreateGPUBuffer(SDL_GPUDevice *device, SDL_GPUBufferUsageFlags usage,
                                      Uint32 size)
{
    SDL_GPUBufferCreateInfo info = {};
    info.usage = usage;
    info.size = size;
    return SDL_CreateGPUBuffer(device, &info);
}

SDL_GPUTransferBuffer *CreateTransferBuffer(SDL_GPUDevice *device, Uint32 size,
                                                   SDL_GPUTransferBufferUsage usage)
{
    SDL_GPUTransferBufferCreateInfo info = {};
    info.usage = usage;
    info.size = size;
    return SDL_CreateGPUTransferBuffer(device, &info);
}

bool UploadBufferData(SDL_GPUDevice *device, SDL_GPUBuffer *buffer,
                             const void *data, Uint32 size)
{
    SDL_GPUTransferBuffer *transfer =
        CreateTransferBuffer(device, size, SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD);
    if (!transfer) {
        return false;
    }

    void *mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return false;
    }
    SDL_memcpy(mapped, data, size);
    SDL_UnmapGPUTransferBuffer(device, transfer);

    SDL_GPUCommandBuffer *command_buffer = SDL_AcquireGPUCommandBuffer(device);
    if (!command_buffer) {
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return false;
    }

    SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(command_buffer);
    if (!copy_pass) {
        SDL_CancelGPUCommandBuffer(command_buffer);
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return false;
    }

    SDL_GPUTransferBufferLocation source = {};
    source.transfer_buffer = transfer;
    source.offset = 0;

    SDL_GPUBufferRegion destination = {};
    destination.buffer = buffer;
    destination.offset = 0;
    destination.size = size;

    SDL_UploadToGPUBuffer(copy_pass, &source, &destination, false);
    SDL_EndGPUCopyPass(copy_pass);
    SDL_SubmitGPUCommandBuffer(command_buffer);
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return true;
}

SDL_GPUGraphicsPipeline *CreateFullscreenPipeline(
    SDL_GPUDevice *device, SDL_GPUShader *vertex_shader, SDL_GPUShader *fragment_shader,
    SDL_GPUTextureFormat format,
    SDL_GPUSampleCount sample_count)
{
    SDL_GPUVertexBufferDescription vertex_buffer_description = {};
    vertex_buffer_description.slot = 0;
    vertex_buffer_description.pitch = 2 * sizeof(float);
    vertex_buffer_description.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vertex_buffer_description.instance_step_rate = 0;

    SDL_GPUVertexAttribute vertex_attribute = {};
    vertex_attribute.location = 0;
    vertex_attribute.buffer_slot = 0;
    vertex_attribute.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    vertex_attribute.offset = 0;

    SDL_GPUVertexInputState vertex_input_state = {};
    vertex_input_state.vertex_buffer_descriptions = &vertex_buffer_description;
    vertex_input_state.num_vertex_buffers = 1;
    vertex_input_state.vertex_attributes = &vertex_attribute;
    vertex_input_state.num_vertex_attributes = 1;

    SDL_GPURasterizerState rasterizer_state = {};
    rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;

    SDL_GPUMultisampleState multisample_state = {};
    multisample_state.sample_count = sample_count;

    SDL_GPUDepthStencilState depth_stencil_state = {};
    depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_ALWAYS;

    SDL_GPUColorTargetBlendState blend_state = {};
    blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    blend_state.enable_blend = true;

    SDL_GPUColorTargetDescription color_target_description = {};
    color_target_description.format = format;
    color_target_description.blend_state = blend_state;

    SDL_GPUGraphicsPipelineTargetInfo target_info = {};
    target_info.color_target_descriptions = &color_target_description;
    target_info.num_color_targets = 1;

    SDL_GPUGraphicsPipelineCreateInfo pipeline_info = {};
    pipeline_info.vertex_shader = vertex_shader;
    pipeline_info.fragment_shader = fragment_shader;
    pipeline_info.vertex_input_state = vertex_input_state;
    pipeline_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipeline_info.rasterizer_state = rasterizer_state;
    pipeline_info.multisample_state = multisample_state;
    pipeline_info.depth_stencil_state = depth_stencil_state;
    pipeline_info.target_info = target_info;
    return SDL_CreateGPUGraphicsPipeline(device, &pipeline_info);
}
