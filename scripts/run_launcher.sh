#!/bin/bash
# 启动 Web 启动器（Vite + React 前端 + Node 桥接后端，入口 http://localhost:3070）
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT/launcher-web"
[ -d node_modules ] || npm install
exec npm run dev
