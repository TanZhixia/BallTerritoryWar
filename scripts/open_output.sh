#!/bin/bash
# 打开最近一次录像
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if [ -f "$ROOT/build/output.mp4" ]; then
    open "$ROOT/build/output.mp4"
else
    open "$ROOT/output.mp4"
fi
