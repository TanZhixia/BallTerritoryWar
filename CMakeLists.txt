cmake_minimum_required(VERSION 3.16)
project(BallTerritoryWar LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(SDL3 REQUIRED)

# 目录结构：
#   src/core    配置 / 常量 / 配色 / 实体 / 状态 / 遥测
#   src/game    场景、物理与战斗、一帧模拟
#   src/render  像素画布、界面中文位图字形、粒子特效、HUD、GPU
#   src/io      录像输出
add_executable(main
    src/main.cpp
    src/core/config.cpp
    src/core/palette.cpp
    src/core/telemetry.cpp
    src/game/physics.cpp
    src/game/scene.cpp
    src/game/simulation.cpp
    src/io/output.cpp
    src/render/canvas.cpp
    src/render/font_atlas.cpp
    src/render/gpu.cpp
    src/render/hud.cpp
    src/render/particles.cpp
    src/render/render.cpp
    src/render/water.cpp
)

target_include_directories(main PRIVATE src)
# 资源目录（black_wool.png）在源码树里的绝对路径，供 assets 定位兜底
target_compile_definitions(main PRIVATE BTW_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(main PRIVATE SDL3::SDL3)

# CLI 启动器（终端 UI + 子命令）。纯 POSIX，不依赖 SDL3。
add_executable(btw-launcher
    launcher/src/main.cpp
    launcher/src/config.cpp
    launcher/src/json.cpp
    launcher/src/project.cpp
    launcher/src/telemetry.cpp
    launcher/src/term.cpp
    launcher/src/tui.cpp
)
target_include_directories(btw-launcher PRIVATE launcher/src)
