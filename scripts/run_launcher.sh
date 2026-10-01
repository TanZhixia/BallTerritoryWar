#!/bin/bash
# 构建并启动 CLI 启动器（终端界面）
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$ROOT/build" --target btw-launcher \
    -j"$( (command -v sysctl >/dev/null && sysctl -n hw.ncpu) || nproc || echo 4)"
cd "$ROOT"
exec ./build/btw-launcher "$@"
