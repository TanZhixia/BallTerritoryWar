#!/bin/bash
# 配置并编译（不会启动游戏）
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$ROOT/build" -j"$( (command -v sysctl >/dev/null && sysctl -n hw.ncpu) || nproc || echo 4)"
echo "构建完成：$ROOT/build/main"
