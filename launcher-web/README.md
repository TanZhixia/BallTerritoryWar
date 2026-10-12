# Ball Territory War · Web 启动器

现代化的 Web 控制台：实时战况、环境状态、**11 组 56 键配置编辑与热更新**、一键构建/启动、
游戏日志。取代了原先的 C++ 终端 TUI（`launcher/` 已删除），行为与之一致。

## 为什么有后端

浏览器不能直连 Unix 域套接字（`/tmp/btw_telemetry.sock`），也不能直接读写 `~/.ball/`。
所以启动器分两层：**Vite + React 前端** + **Node 桥接后端**。后端只监听 `127.0.0.1`。

```
launcher-web/
├── server/                 # Node 后端（TypeScript，tsx 运行）
│   ├── index.ts            # express：REST + SSE + 生产静态资源
│   ├── schema.ts           # 配置 schema（唯一真相，11 组 56 键）
│   ├── yaml.ts             # ~/.ball/config.yaml 行级读写（保留注释）+ launcher.conf
│   ├── telemetry.ts        # 遥测 socket 客户端 + SETCONFIG
│   ├── project.ts          # 项目根定位 / 环境探测 / 构建 / 启动 / 日志
│   └── types.ts
└── src/                    # React 前端（Vite + TS + Tailwind v4 + Radix + recharts）
    ├── App.tsx
    ├── lib/{api,types,theme,format}.ts
    └── components/{Header,BattlePanel,TerritoryChart,EnvCard,LaunchPanel,LogPanel,ConfigEditor,ui}.tsx
```

## 端口与运行

| 命令 | 说明 |
|---|---|
| `npm run dev` | 开发：Vite **3070**（HMR）+ 后端 **3071**，`/api` 由 Vite 代理 |
| `npm run build` | 生产：前端产出 `dist/` |
| `npm start` | 生产：后端在 **3070** 同端口服务 `dist/` + API |
| `npm run typecheck` | 前后端 TS 类型检查 |

入口统一是 **http://localhost:3070**。仓库根的 `scripts/run_launcher.sh` 会 `npm install`
后跑 `npm run dev`。前提：Node ≥ 18。

## 与游戏/文件的交互

- **遥测**：连 `/tmp/btw_telemetry.sock`，读取每行一个 JSON 的战况帧（只接受含 `territory`
  的帧），断开后 0.8s 退避重连；通过 SSE `/api/telemetry/stream` 广播给浏览器。
- **热更新**：`SETCONFIG <点分键> <值...>` → `OK` / `ERR <原因>`（独立短连接逐条下发）。
- **配置**：`~/.ball/config.yaml` 采用**行级替换**写入，保留缩进/顺序/注释；文件中缺失的键
  自动补写到所属分组（分组不存在则文件末尾新建并标注「启动器补充」）。`~/.ball/launcher.conf`
  只存录像输出文件名 `output=`。
- **构建**：在 `<项目根>/build` 执行 `cmake .. && make -j<cores>`，输出流式返回。
- **启动**：`spawn(build/main, ['--output', <名>], { cwd: 项目根 })`，`stdout/stderr` 重定向到
  `build/game.log`（每次启动覆盖）。

## HTTP 接口

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/api/env` | 环境状态 |
| GET | `/api/config` | `{ schema, values, configPath }` |
| POST | `/api/config` | `{ updates, mode:'save'\|'apply'\|'both' }` |
| POST | `/api/config/reset` | 全部恢复默认并写入 + 热更新 |
| GET/PUT | `/api/launch-options` | 读/写录像输出文件名 |
| GET | `/api/telemetry/stream` | SSE 战况帧 |
| POST | `/api/build` | 流式构建输出（分块文本） |
| POST | `/api/launch` | 启动游戏 |
| GET | `/api/game` | 运行状态 / pid / 退出码 |
| GET | `/api/logs/stream` | SSE 游戏日志尾部 |

## 配置 schema 同步

`server/schema.ts` 与游戏 `src/core/config.h`、`src/core/config.cpp` 是**配置的三处真相**。
改游戏配置键时三处都要同步；前端不重复 schema，而是经 `/api/config` 获取。
