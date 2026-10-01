#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "core/config.h"
#include "core/constants.h"
#include "core/telemetry.h"
#include "game/physics.h"
#include "game/scene.h"
#include "game/simulation.h"
#include "io/output.h"
#include "render/gpu.h"
#include "render/render.h"


int main(int argc, char *argv[])
{
    bool limit_fps = true;
    bool record_video = true;
    bool music_enabled = true;
    bool write_config_only = false;  // --write-config：只生成配置文件后退出
    int max_frames = 0;               // --max-frames N：单局帧数上限（0 = 不限）
    float run_duration_seconds = 0.0f;  // --duration <秒>：运行指定秒数后自动退出（0 = 不限）
    std::string output_name = "output.mp4";
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--unlimited") == 0 ||
            std::strcmp(argv[i], "-u") == 0) {
            limit_fps = false;
        }
        if (std::strcmp(argv[i], "--write-config") == 0) {
            write_config_only = true;
        }
        if (std::strcmp(argv[i], "--max-frames") == 0 && i + 1 < argc) {
            max_frames = std::atoi(argv[++i]);
        }
        if ((std::strcmp(argv[i], "--duration") == 0 || std::strcmp(argv[i], "-d") == 0) &&
            i + 1 < argc) {
            run_duration_seconds = std::atof(argv[++i]);
        }
        if (std::strcmp(argv[i], "--no-weapon-lift") == 0) {
            g_weapon_lift_enabled = false;  // 关闭 *8/*4/*2 与霰弹/狙击列升力（中间升力保留）
        }
        if ((std::strcmp(argv[i], "--output") == 0 || std::strcmp(argv[i], "-o") == 0) &&
            i + 1 < argc) {
            output_name = argv[++i];
        }
        if (std::strcmp(argv[i], "--no-record") == 0 ||
            std::strcmp(argv[i], "-nr") == 0) {
            record_video = false;
        }
        if (std::strcmp(argv[i], "--no-music") == 0 ||
            std::strcmp(argv[i], "-nm") == 0) {
            music_enabled = false;
        }
    }
    SDL_Log("startup: unlimited=%d record=%d music=%d max_frames=%d duration=%.1fs",
            !limit_fps, record_video, music_enabled, max_frames, run_duration_seconds);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    std::signal(SIGPIPE, SIG_IGN);  // 遥测 socket 写入对端关闭时不终止进程
    LoadConfig();

    if (write_config_only) {
        SDL_Log("config: 配置已就绪，退出");
        SDL_Quit();
        return 0;
    }

    SDL_Window *window = SDL_CreateWindow("Ball Territory War", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    if (!window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GPUDevice *device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL, true, nullptr);
    if (!device) {
        SDL_Log("SDL_CreateGPUDevice failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    if (!SDL_ClaimWindowForGPUDevice(device, window)) {
        SDL_Log("SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
        SDL_DestroyGPUDevice(device);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    if (!limit_fps) {
        if (!SDL_SetGPUSwapchainParameters(
                device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                SDL_GPU_PRESENTMODE_IMMEDIATE)) {
            SDL_Log("Failed to disable vsync: %s", SDL_GetError());
        }
    }

    SDL_GPUTextureFormat swapchain_format = SDL_GetGPUSwapchainTextureFormat(device, window);

    SDL_GPUShader *vs_quad = CreateShader(device, VS_QUAD_MSL, "vs_quad",
                                          SDL_GPU_SHADERSTAGE_VERTEX);
    SDL_GPUShader *fs_grid = CreateShader(device, FS_GRID_MSL, "fs_grid",
                                          SDL_GPU_SHADERSTAGE_FRAGMENT, 1);
    SDL_GPUShader *fs_canvas = CreateShader(device, FS_CANVAS_MSL, "fs_canvas",
                                            SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 1);
    if (!vs_quad || !fs_grid || !fs_canvas) {
        SDL_Log("SDL_CreateGPUShader failed: %s", SDL_GetError());
        return 1;
    }

    SDL_GPUGraphicsPipeline *canvas_pipeline =
        CreateFullscreenPipeline(device, vs_quad, fs_canvas, swapchain_format);
    SDL_GPUGraphicsPipeline *grid_pipeline =
        CreateFullscreenPipeline(device, vs_quad, fs_grid, swapchain_format);
    if (!canvas_pipeline || !grid_pipeline) {
        SDL_Log("SDL_CreateGPUGraphicsPipeline failed: %s", SDL_GetError());
        return 1;
    }

    SDL_GPUSampleCount msaa_sample_count = SDL_GPU_SAMPLECOUNT_4;
    if (!SDL_GPUTextureSupportsSampleCount(device, swapchain_format, msaa_sample_count)) {
        msaa_sample_count = SDL_GPU_SAMPLECOUNT_2;
        if (!SDL_GPUTextureSupportsSampleCount(device, swapchain_format, msaa_sample_count)) {
            msaa_sample_count = SDL_GPU_SAMPLECOUNT_1;
        }
    }
    if (!limit_fps && record_video) {
        msaa_sample_count = SDL_GPU_SAMPLECOUNT_1;  // 录制加速模式：关闭抗锯齿
    }
    SDL_GPUGraphicsPipeline *canvas_pipeline_msaa = canvas_pipeline;
    SDL_GPUGraphicsPipeline *grid_pipeline_msaa = grid_pipeline;
    if (msaa_sample_count > SDL_GPU_SAMPLECOUNT_1) {
        canvas_pipeline_msaa = CreateFullscreenPipeline(
            device, vs_quad, fs_canvas, swapchain_format, msaa_sample_count);
        grid_pipeline_msaa = CreateFullscreenPipeline(
            device, vs_quad, fs_grid, swapchain_format, msaa_sample_count);
        if (!canvas_pipeline_msaa || !grid_pipeline_msaa) {
            SDL_Log("SDL_CreateGPUGraphicsPipeline (MSAA) failed: %s", SDL_GetError());
            return 1;
        }
    }

    const float quad_vertices[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f,  1.0f,
    };

    SDL_GPUBuffer *quad_buffer = CreateGPUBuffer(device, SDL_GPU_BUFFERUSAGE_VERTEX,
                                                 sizeof(quad_vertices));
    if (!quad_buffer || !UploadBufferData(device, quad_buffer, quad_vertices,
                                          sizeof(quad_vertices))) {
        SDL_Log("Failed to create/upload quad buffer: %s", SDL_GetError());
        return 1;
    }

    int output_width = WINDOW_WIDTH;
    int output_height = WINDOW_HEIGHT;
    SDL_GetWindowSizeInPixels(window, &output_width, &output_height);

    SDL_GPUTextureCreateInfo readback_info = {};
    readback_info.type = SDL_GPU_TEXTURETYPE_2D;
    readback_info.format = swapchain_format;
    readback_info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    readback_info.width = output_width;
    readback_info.height = output_height;
    readback_info.layer_count_or_depth = 1;
    readback_info.num_levels = 1;
    readback_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture *readback_texture = SDL_CreateGPUTexture(device, &readback_info);
    if (!readback_texture) {
        SDL_Log("SDL_CreateGPUTexture (readback) failed: %s", SDL_GetError());
        return 1;
    }

    const Uint32 readback_size =
        static_cast<Uint32>(output_width) * static_cast<Uint32>(output_height) * 4;
    SDL_GPUTransferBuffer *readback_transfer =
        CreateTransferBuffer(device, readback_size, SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD);
    if (!readback_transfer) {
        SDL_Log("SDL_CreateGPUTransferBuffer (readback) failed: %s", SDL_GetError());
        return 1;
    }

    SDL_GPUTexture *msaa_texture = nullptr;
    if (msaa_sample_count > SDL_GPU_SAMPLECOUNT_1) {
        SDL_GPUTextureCreateInfo msaa_info = {};
        msaa_info.type = SDL_GPU_TEXTURETYPE_2D;
        msaa_info.format = swapchain_format;
        msaa_info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        msaa_info.width = output_width;
        msaa_info.height = output_height;
        msaa_info.layer_count_or_depth = 1;
        msaa_info.num_levels = 1;
        msaa_info.sample_count = msaa_sample_count;
        msaa_texture = SDL_CreateGPUTexture(device, &msaa_info);
        if (!msaa_texture) {
            SDL_Log("SDL_CreateGPUTexture (MSAA) failed: %s", SDL_GetError());
            return 1;
        }
    }

    SDL_GPUTextureCreateInfo canvas_info = {};
    canvas_info.type = SDL_GPU_TEXTURETYPE_2D;
    canvas_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    canvas_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    canvas_info.width = WINDOW_WIDTH;
    canvas_info.height = WINDOW_HEIGHT;
    canvas_info.layer_count_or_depth = 1;
    canvas_info.num_levels = 1;
    canvas_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture *canvas_texture = SDL_CreateGPUTexture(device, &canvas_info);
    if (!canvas_texture) {
        SDL_Log("SDL_CreateGPUTexture (canvas) failed: %s", SDL_GetError());
        return 1;
    }

    SDL_GPUSamplerCreateInfo sampler_info = {};
    sampler_info.min_filter = SDL_GPU_FILTER_LINEAR;
    sampler_info.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    SDL_GPUSampler *canvas_sampler = SDL_CreateGPUSampler(device, &sampler_info);
    if (!canvas_sampler) {
        SDL_Log("SDL_CreateGPUSampler failed: %s", SDL_GetError());
        return 1;
    }

    const Uint32 canvas_size =
        static_cast<Uint32>(WINDOW_WIDTH) * static_cast<Uint32>(WINDOW_HEIGHT) * 4;
    SDL_GPUTransferBuffer *canvas_transfer =
        CreateTransferBuffer(device, canvas_size, SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD);
    if (!canvas_transfer) {
        SDL_Log("SDL_CreateGPUTransferBuffer (canvas) failed: %s", SDL_GetError());
        return 1;
    }
    GameState state;
    std::vector<Uint8> canvas;
    BuildStaticScene(canvas, state.blocking_circles);

    if (output_name.empty()) {
        output_name = "output.mp4";
    }
    FILE *recorder = record_video
        ? StartRecorder(output_width, output_height, music_enabled, output_name.c_str())
        : nullptr;
    if (!recorder) {
        if (record_video) {
            SDL_Log("Failed to start ffmpeg, recording disabled");
        } else {
            SDL_Log("Recording disabled by --no-record");
        }
    } else {
        SDL_Log("Recording to %s (press close button to stop)", output_name.c_str());
    }

    InitializeGame(state);
    state.max_frames = max_frames;
    state.run_duration_seconds = run_duration_seconds;

    std::vector<Uint8> canvas_snapshot(canvas_size);
    std::vector<Uint8> display_canvas(canvas_size);

    // 全屏 shader 的统一变量（逻辑分辨率 + 战场区域 + 输出分辨率）
    UIUniform uniform = {};
    uniform.logical_width = WINDOW_WIDTH;
    uniform.logical_height = WINDOW_HEIGHT;
    uniform.frame_x = FRAME_X;
    uniform.frame_y = FRAME_Y;
    uniform.frame_size = FRAME_SIZE;
    uniform.output_width = output_width;
    uniform.output_height = output_height;
    uniform.padding = 0.0f;

    bool running = true;
    Uint64 fps_frame_count = 0;
    Uint64 fps_last_time = SDL_GetTicks();
    Uint64 acquire_ms_total = 0;
    Uint64 submit_ms_total = 0;

    // 遥测 IPC：后台线程监听 /tmp/btw_telemetry.sock
    TelemetryState telemetry;
    std::thread telemetry_thread(TelemetryThreadMain, std::ref(telemetry));

    while (running) {
        ++fps_frame_count;
        const Uint64 frame_start = SDL_GetTicks();

        const Uint64 now_ms = SDL_GetTicks();
        if (now_ms - fps_last_time >= 1000) {
            const double fps = fps_frame_count * 1000.0 /
                static_cast<double>(now_ms - fps_last_time);
            SDL_Log("FPS: %.1f (acquire %.2fms, submit %.2fms)",
                    fps,
                    static_cast<double>(acquire_ms_total) / fps_frame_count,
                    static_cast<double>(submit_ms_total) / fps_frame_count);
            fps_frame_count = 0;
            fps_last_time = now_ms;
            acquire_ms_total = 0;
            submit_ms_total = 0;
        }

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
        }

        if (!running) {
            break;
        }

        if (!StepGame(state, canvas, canvas_snapshot, telemetry)) {
            break;
        }

        RenderGame(state, canvas, display_canvas);

        void *canvas_mapped = SDL_MapGPUTransferBuffer(device, canvas_transfer, true);
        if (!canvas_mapped) {
            SDL_Log("SDL_MapGPUTransferBuffer (canvas) failed: %s", SDL_GetError());
            break;
        }
        SDL_memcpy(canvas_mapped, display_canvas.data(), canvas_size);
        SDL_UnmapGPUTransferBuffer(device, canvas_transfer);

        SDL_GPUCommandBuffer *command_buffer = SDL_AcquireGPUCommandBuffer(device);
        if (!command_buffer) {
            SDL_Log("SDL_AcquireGPUCommandBuffer failed: %s", SDL_GetError());
            break;
        }

        SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(command_buffer);
        if (!copy_pass) {
            SDL_Log("SDL_BeginGPUCopyPass failed: %s", SDL_GetError());
            SDL_CancelGPUCommandBuffer(command_buffer);
            break;
        }

        SDL_GPUTextureTransferInfo canvas_source = {};
        canvas_source.transfer_buffer = canvas_transfer;
        canvas_source.offset = 0;
        canvas_source.pixels_per_row = WINDOW_WIDTH;
        canvas_source.rows_per_layer = WINDOW_HEIGHT;

        SDL_GPUTextureRegion canvas_destination = {};
        canvas_destination.texture = canvas_texture;
        canvas_destination.mip_level = 0;
        canvas_destination.layer = 0;
        canvas_destination.x = 0;
        canvas_destination.y = 0;
        canvas_destination.z = 0;
        canvas_destination.w = WINDOW_WIDTH;
        canvas_destination.h = WINDOW_HEIGHT;
        canvas_destination.d = 1;
        SDL_UploadToGPUTexture(copy_pass, &canvas_source, &canvas_destination, false);

        SDL_EndGPUCopyPass(copy_pass);

        SDL_GPUTexture *swapchain_texture = nullptr;
        Uint32 swapchain_width = 0;
        Uint32 swapchain_height = 0;
        const Uint64 acquire_start = SDL_GetTicks();
        bool acquired = false;
        if (limit_fps) {
            acquired = SDL_WaitAndAcquireGPUSwapchainTexture(
                command_buffer, window, &swapchain_texture,
                &swapchain_width, &swapchain_height);
        } else {
            acquired = SDL_AcquireGPUSwapchainTexture(
                command_buffer, window, &swapchain_texture,
                &swapchain_width, &swapchain_height);
        }
        if (!acquired) {
            SDL_Log("SDL_WaitAndAcquireGPUSwapchainTexture failed: %s", SDL_GetError());
            SDL_SubmitGPUCommandBuffer(command_buffer);
            break;
        }
        acquire_ms_total += SDL_GetTicks() - acquire_start;
        if (!swapchain_texture) {
            // 非阻塞模式下帧在途已满：先提交当前命令缓冲，下一帧再继续
            const Uint64 submit_start = SDL_GetTicks();
            SDL_SubmitGPUCommandBuffer(command_buffer);
            submit_ms_total += SDL_GetTicks() - submit_start;
            continue;
        }

        SDL_PushGPUFragmentUniformData(command_buffer, 0, &uniform, sizeof(uniform));

        if (swapchain_texture) {
            SDL_GPUColorTargetInfo swapchain_target = {};
            swapchain_target.clear_color = SDL_FColor{0.06f, 0.06f, 0.1f, 1.0f};
            swapchain_target.load_op = SDL_GPU_LOADOP_CLEAR;
            if (msaa_sample_count > SDL_GPU_SAMPLECOUNT_1) {
                swapchain_target.texture = msaa_texture;
                swapchain_target.resolve_texture = swapchain_texture;
                swapchain_target.resolve_mip_level = 0;
                swapchain_target.resolve_layer = 0;
                swapchain_target.store_op = SDL_GPU_STOREOP_RESOLVE;
            } else {
                swapchain_target.texture = swapchain_texture;
                swapchain_target.store_op = SDL_GPU_STOREOP_STORE;
            }

            SDL_GPURenderPass *swapchain_pass =
                SDL_BeginGPURenderPass(command_buffer, &swapchain_target, 1, nullptr);
            if (swapchain_pass) {
                SDL_GPUViewport viewport = {};
                viewport.x = 0.0f;
                viewport.y = 0.0f;
                viewport.w = static_cast<float>(swapchain_width);
                viewport.h = static_cast<float>(swapchain_height);
                viewport.min_depth = 0.0f;
                viewport.max_depth = 1.0f;
                SDL_SetGPUViewport(swapchain_pass, &viewport);

                SDL_BindGPUGraphicsPipeline(swapchain_pass, canvas_pipeline_msaa);
                SDL_GPUBufferBinding quad_binding = {};
                quad_binding.buffer = quad_buffer;
                quad_binding.offset = 0;
                SDL_BindGPUVertexBuffers(swapchain_pass, 0, &quad_binding, 1);
                SDL_GPUTextureSamplerBinding canvas_binding = {};
                canvas_binding.texture = canvas_texture;
                canvas_binding.sampler = canvas_sampler;
                SDL_BindGPUFragmentSamplers(swapchain_pass, 0, &canvas_binding, 1);
                SDL_DrawGPUPrimitives(swapchain_pass, 6, 1, 0, 0);

                SDL_BindGPUGraphicsPipeline(swapchain_pass, grid_pipeline_msaa);
                SDL_BindGPUVertexBuffers(swapchain_pass, 0, &quad_binding, 1);
                SDL_DrawGPUPrimitives(swapchain_pass, 6, 1, 0, 0);

                SDL_EndGPURenderPass(swapchain_pass);
            }
        }

        if (recorder) {
            SDL_PushGPUFragmentUniformData(command_buffer, 0, &uniform, sizeof(uniform));

            SDL_GPUColorTargetInfo readback_target = {};
            readback_target.texture = readback_texture;
            readback_target.clear_color = SDL_FColor{0.06f, 0.06f, 0.1f, 1.0f};
            readback_target.load_op = SDL_GPU_LOADOP_CLEAR;
            readback_target.store_op = SDL_GPU_STOREOP_STORE;

            SDL_GPURenderPass *readback_pass =
                SDL_BeginGPURenderPass(command_buffer, &readback_target, 1, nullptr);
            if (readback_pass) {
                SDL_GPUViewport viewport = {};
                viewport.x = 0.0f;
                viewport.y = 0.0f;
                viewport.w = static_cast<float>(output_width);
                viewport.h = static_cast<float>(output_height);
                viewport.min_depth = 0.0f;
                viewport.max_depth = 1.0f;
                SDL_SetGPUViewport(readback_pass, &viewport);

                SDL_BindGPUGraphicsPipeline(readback_pass, canvas_pipeline);
                SDL_GPUBufferBinding quad_binding = {};
                quad_binding.buffer = quad_buffer;
                quad_binding.offset = 0;
                SDL_BindGPUVertexBuffers(readback_pass, 0, &quad_binding, 1);
                SDL_GPUTextureSamplerBinding canvas_binding = {};
                canvas_binding.texture = canvas_texture;
                canvas_binding.sampler = canvas_sampler;
                SDL_BindGPUFragmentSamplers(readback_pass, 0, &canvas_binding, 1);
                SDL_DrawGPUPrimitives(readback_pass, 6, 1, 0, 0);

                SDL_BindGPUGraphicsPipeline(readback_pass, grid_pipeline);
                SDL_BindGPUVertexBuffers(readback_pass, 0, &quad_binding, 1);
                SDL_DrawGPUPrimitives(readback_pass, 6, 1, 0, 0);

                SDL_EndGPURenderPass(readback_pass);
            }

        SDL_GPUCopyPass *readback_copy = SDL_BeginGPUCopyPass(command_buffer);
        if (readback_copy) {
            SDL_GPUTextureRegion source = {};
            source.texture = readback_texture;
            source.mip_level = 0;
            source.layer = 0;
            source.x = 0;
            source.y = 0;
            source.z = 0;
            source.w = output_width;
            source.h = output_height;
            source.d = 1;

            SDL_GPUTextureTransferInfo destination = {};
            destination.transfer_buffer = readback_transfer;
            destination.offset = 0;
            destination.pixels_per_row = 0;
            destination.rows_per_layer = 0;

            SDL_DownloadFromGPUTexture(readback_copy, &source, &destination);
            SDL_EndGPUCopyPass(readback_copy);
        }

        SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(command_buffer);
        if (!fence) {
            SDL_Log("SDL_SubmitGPUCommandBufferAndAcquireFence failed: %s", SDL_GetError());
            break;
        }

        if (!SDL_WaitForGPUFences(device, true, &fence, 1)) {
            SDL_Log("SDL_WaitForGPUFences failed: %s", SDL_GetError());
            SDL_ReleaseGPUFence(device, fence);
            break;
        }

        void *readback_data = SDL_MapGPUTransferBuffer(device, readback_transfer, false);
        if (readback_data) {
            WriteReadbackToRecorder(recorder, readback_data, output_width, output_height,
                                    swapchain_format);
            SDL_UnmapGPUTransferBuffer(device, readback_transfer);
        }

            SDL_ReleaseGPUFence(device, fence);
        } else {
            const Uint64 submit_start = SDL_GetTicks();
            SDL_SubmitGPUCommandBuffer(command_buffer);
            submit_ms_total += SDL_GetTicks() - submit_start;
        }

        const Uint64 elapsed = SDL_GetTicks() - frame_start;
        if (limit_fps && elapsed < FRAME_MS) {
            SDL_Delay(FRAME_MS - elapsed);
        }
    }

    if (recorder) {
        pclose(recorder);  // 等待 ffmpeg 子进程完成录像收尾（fclose 不等待，可能截断文件）
        SDL_Log("Recording finished: %s", output_name.c_str());
    }
    if (!g_music_playlist_path.empty()) {
        std::remove(g_music_playlist_path.c_str());
        g_music_playlist_path.clear();
    }

    telemetry.running = false;
    telemetry_thread.join();

    if (device) {
        if (readback_transfer) {
            SDL_ReleaseGPUTransferBuffer(device, readback_transfer);
        }
        if (readback_texture) {
            SDL_ReleaseGPUTexture(device, readback_texture);
        }
        if (msaa_texture) {
            SDL_ReleaseGPUTexture(device, msaa_texture);
        }
        if (canvas_transfer) {
            SDL_ReleaseGPUTransferBuffer(device, canvas_transfer);
        }
        if (canvas_sampler) {
            SDL_ReleaseGPUSampler(device, canvas_sampler);
        }
        if (canvas_texture) {
            SDL_ReleaseGPUTexture(device, canvas_texture);
        }
        if (quad_buffer) {
            SDL_ReleaseGPUBuffer(device, quad_buffer);
        }
        if (grid_pipeline_msaa && grid_pipeline_msaa != grid_pipeline) {
            SDL_ReleaseGPUGraphicsPipeline(device, grid_pipeline_msaa);
        }
        if (canvas_pipeline_msaa && canvas_pipeline_msaa != canvas_pipeline) {
            SDL_ReleaseGPUGraphicsPipeline(device, canvas_pipeline_msaa);
        }
        if (canvas_pipeline) {
            SDL_ReleaseGPUGraphicsPipeline(device, canvas_pipeline);
        }
        if (grid_pipeline) {
            SDL_ReleaseGPUGraphicsPipeline(device, grid_pipeline);
        }
        if (fs_canvas) {
            SDL_ReleaseGPUShader(device, fs_canvas);
        }
        if (fs_grid) {
            SDL_ReleaseGPUShader(device, fs_grid);
        }
        if (vs_quad) {
            SDL_ReleaseGPUShader(device, vs_quad);
        }
        SDL_ReleaseWindowFromGPUDevice(device, window);
        SDL_DestroyGPUDevice(device);
    }
    if (window) {
        SDL_DestroyWindow(window);
    }
    SDL_Quit();
    return 0;
}
