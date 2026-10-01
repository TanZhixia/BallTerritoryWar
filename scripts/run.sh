#!/bin/bash
# 编译并启动游戏（会打开窗口，默认录制 output.mp4）
# 额外参数会原样传给游戏，例如：
#   scripts/run.sh --no-record --max-frames 1200
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
"$ROOT/scripts/build.sh"
cd "$ROOT"
exec ./build/main "$@"
