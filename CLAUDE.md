# CLAUDE.md

给接手本仓库的 Claude（或任何 AI 协作者）的项目说明。**动代码前先读完这一份**。
人类可读的玩法/配置说明在 `README.md`，终端启动器说明在 `launcher/README.md`，三者互补。

---

## 1. 这是什么

**Ball Territory War（球球领土战争）**：四队（红/绿/蓝/黄）全自动对战模拟器的实时可视化。
左半屏是「物理机械区」（1600×1000 窗口里宽 600 的竖条），右半屏是四角基地的像素领土争夺。
没有游戏引擎、没有场景文件、**每一帧的每个像素都是 C++ 手写光栅化出来的**：
全部绘制写进两个 `std::vector<Uint8>` RGBA 画布，最后作为一张纹理交给 SDL3 GPU 管线做后处理
（饱和度/对比度/辉光/暗角）后上屏，可选回读给 ffmpeg 录像。

- 语言/依赖：C++17 + SDL3（GPU API + MSL 着色器），**只在 macOS 上构建过**；
  刻意**不依赖 SDL3_ttf**（所有文字都是生成期烘焙的位图字形，见第 6 节）。
- 产物：`build/main`（游戏本体）、`build/btw-launcher`（纯 POSIX 终端启动器）。

---

## 2. 快速开始

```bash
scripts/build.sh                 # cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
scripts/run.sh [参数]            # 编译并启动（默认录到 output.mp4，可混入 ~/.ball/music 的背景乐）
scripts/run_launcher.sh          # 终端启动器（TUI：状态面板 / 配置编辑 / 启动游戏）
./build/main --help              # 看运行参数
python3 scripts/gen_font_atlas.py   # 重新烘焙位图字形（需要 Pillow）
```

常用运行参数：`--no-record`（不录像）、`--no-music`、`-d <秒>`（跑这么多久后退出）、
`--unlimited`（不限帧）、`--no-weapon-lift`（关掉倍率带上方的升力）。

**提交前必须做的两件事**（用户明确要求过）：
1. 干净目录全量编译，`-Wall -Wextra` 必须 **0 warning / 0 error**；
2. 端到端跑一次（例如 `./build/main --no-record --no-music -d 6`，确认稳定 ~59 FPS 且正常退出）。

---

## 3. 目录地图

```
src/main.cpp             窗口/GPU 设备/两条全屏管线(MSL)/主循环/GPU 回读→ffmpeg/清理
src/core/
  constants.h            1600×1000、战场 FRAME_X=600/FRAME_SIZE=1000、闪光缓冲、基地半径
  config.{h,cpp}         GameConfig + 手写 YAML 子集解析 + SETCONFIG 热更新 + SanitizeDivisors
  palette.cpp            PURE_COLORS（领土判定用纯色）/ NEW_COLORS（显示用柔和色）+ 颜色反查
  entities.h             BallObject（画笔球/大球/狙击/碎片）、PhysicsBall、PendingPaint
  state.h                GameState：实体容器 + 队伍状态（弹药/护盾/升力阈值/存活）+ 计时
  effects.h              纯显示层数据：Particle / TrailDot / Shockwave / Bubble
  telemetry.{h,cpp}      /tmp/btw_telemetry.sock 线程：JSON 行推送 + 解析 SETCONFIG
src/game/
  scene.{h,cpp}          静态场景绘制 + **机械区布局几何常量（唯一真相，见第 5 节）**
  physics.cpp            机械区一帧：水中浮力/重力/挡板/倍率带/五种武器格/机枪 AI/碰撞结算
  simulation.cpp         InitializeGame / StepGame（一帧模拟的总编排）
src/render/
  canvas.{h,cpp}         逐像素光栅化原语 + 位图字体绘制（定尺寸 / 等比缩放）+ 领土采样
  font_atlas.{h,cpp}     两套位图字形表（**由脚本生成，勿手改**）
  water.{h,cpp}          中央水池：涟漪 + 无色水体（折射/镜像反射）+ 表面高光（仅显示层）
  particles.cpp          粒子/拖尾/冲击波/气泡 + 领土闪光衰减
  hud.{h,cpp}            右侧悬浮面板（色点 + 队名 + 领土占比 + 护盾/弹药 / 已灭）
  render.cpp             RenderGame：每帧把 canvas 合成进 display_canvas 的唯一入口
  gpu.cpp                三段 MSL 与 GPU 资源辅助
src/io/output.cpp        输出名/音乐播放列表/ffmpeg 管道/像素格式转换
launcher/                 终端启动器（独立目标 btw-launcher，纯 POSIX）
scripts/                  build.sh / run.sh / run_launcher.sh / gen_font_atlas.py
demo/                     重构前的旧工程，独立 git 历史、已 gitignore，只读参考，别改
```

---

## 4. 每帧数据流（理解这个就理解一半代码）

```
main while 循环（默认限帧 60fps）
  StepGame(state, canvas, canvas_snapshot, telemetry)
    ├─ 引力（狙击/大球，整帧一次）→ 大球半径刷新
    ├─ 两个子步（每步 dt=1/60）：按 CountPixelsToPaint 计费扣 value → 球移动
    │    → old_paints(移动前旧色) / new_paints(移动后新色)；只把 old_paints 写进持久 canvas
    ├─ 大球拖尾/分裂、狙击自爆、球球碰撞与伤害结算
    ├─ 基地占领判定 → 该色物理球全部转大球射出 → 胜负倒计时
    ├─ 升力阈值 ×1.01/秒 → UpdatePhysicsBalls（水中浮力 + 武器格 + 机枪）
    └─ 每 30 帧 SampleTerritory + CollectTelemetry
  RenderGame(state, canvas, display_canvas)      // 只写 display_canvas
    └─ canvas → display_canvas，叠：新色涂画、拖尾、底栏、特效、HUD、物理球、水池、护盾、炮塔、数值
  上传纹理 → 画到 swapchain → 可选回读 → 写进 ffmpeg
```

**两个画布的分工是硬约束**：

| 画布 | 内容 | 谁写 |
|---|---|---|
| `canvas`（持久） | 静态场景 + **只有纯色**的领土涂画（游戏真相） | `BuildStaticScene` 一次；每帧 `old_paints` |
| `display_canvas`（每帧重建） | 上面全部 + 柔和新色、粒子、UI、水面等一切"效果" | `RenderGame` |

---

## 5. 不可破坏的不变量

1. **`canvas` 里只能出现 `PURE_COLORS` 的精确字节**。领土统计、基地占领判定都靠精确颜色比较；
   任何"柔和色/半透明/渐变"只能写 `display_canvas`。
2. **几何只有一个真相：`src/game/scene.h`**。挡板、倍率带、缺口、水池、武器格、外框的位置尺寸
   全在那里，并且是**派生关系**（`BAND_TOP_Y = LIFT_WALL_Y - LIFT_WALL_HALF` 等）。
   `scene.cpp`（绘制）、`physics.cpp`（碰撞）、`water.cpp`（水面）都从那里取；
   **不要再在别处写字面量**——重构前就是两边各写一份，改一处就会让画面和碰撞错位。
3. **升力只在水池里**（`InLiftWater()`，x 225~375 / y 390~430）。倍率带上方另有一道独立的
   列升力（`BAND_LIFT_TOP_Y`~`BAND_TOP_Y`，按 `lift.weaponInitial` 分档）。
4. **物理球半径是常量** `physics.radius`。曾经做过"半径随 value 缩放"，
   用户明确放弃并要求撤销（提交 `d14dd16`），**不要再加回来**。
5. **×n 区没有水**。用户明确说过"那不加水浮力了"，只保留中央那一柱水池。
6. **没有复活机制**。基地被占 = 该色永久出局，剩余物理球全部转成大球射出（提交 `48a0676`）。
7. **底部武器栏标签是浅色 `(0.9,0.9,0.9)`**——曾有人"为提高对比度"改成深色，被要求改回。
   改 UI 配色前先问，不要自作主张。
8. **平衡数值不要动**（速度/重力/阈值/价格等），除非用户明确要求调。

---

## 6. 约定

### 提交与推送
- 中文提交信息，标题一句话说清"改了什么 + 为什么"，正文用 `-` 列要点（仓库既有风格）。
- 推送到 `origin/main`（`github.com/TanZhixia/BallTerritoryWar`）。
- 一个主题一个提交；修 bug 与加功能不要混在一个提交里。
- **推送偶发失败是网络问题**：本机到 github.com 的 HTTP/2 会随机报
  `Error in the HTTP2 framing layer` 或连接超时，**重试 2~5 次即可**，不要因此改远程协议。

### 配置
- 配置项有**三处真相**必须同步：`src/core/config.h`（结构体默认值）、`src/core/config.cpp`
  （默认 YAML 模板 + YAML 解析 + `ApplyConfigKey` 热更新分支）、
  `launcher/src/config.cpp` 的 `BuildSchema()`。当前共 11 组 56 键。
- 充当除数/步长的配置项要加进 `SanitizeDivisors()`（防 `inf` 转 int 的 UB 与死循环）。
- `SETCONFIG` 热更新**不写回文件**；写回 YAML 是启动器的事。

### 文字（重要）
- 画面里**所有**文字都走位图字形表（`render/font_atlas.{h,cpp}`），没有 SDL3_ttf。
- 字形是 `scripts/gen_font_atlas.py` 用 Pillow + 系统字体（Hiragino Sans GB W6）在
  **生成期**烘焙出来的：小号表 37 字形（界面文案 + 数字符号）、大号表 14 字形（缩放用的数字）。
- **加/改任何文案都要先把字符加进脚本的 `CJK`/`ASCII` 常量并重跑脚本**；
  没收录的字符会按 `FONT_UNKNOWN_ADVANCE`(8px) 空过（表现为漏字）。
- 需要缩放的文字用 `DrawTextFontScaled(..., scale)`（scale 是相对主字号的倍数，
  >1 会自动改用大号表缩小采样，两表按基线对齐）。
- `FontLineHeight()` / `ScaledFontLineHeight()` 是**函数**不是 constexpr，别写进 `constexpr` 表达式。

### 遥测与启动器
- 游戏每 0.5s 往 `/tmp/btw_telemetry.sock` 推一行 JSON；启动器连上去解析。
- 改 JSON 字段 = 同时改 `src/core/telemetry.cpp` 与 `launcher/src/tui.cpp`，两边都要编译验证。

---

## 7. 怎么验证（不开窗口也能验）

这套离线手法在本项目里反复救命，强烈建议沿用：

1. **真实对局帧导出**：写一个小 `main`，链接除 `src/main.cpp` 外的全部源码，
   跑 `InitializeGame` → N 次 `StepGame` → `RenderGame`，把 `display_canvas` 导成 PPM，
   用 Pillow 放大看。想观察中间量就在里面加 `printf`（例如打印物理球数量/value/阈值）。
2. **像素级比对**：改绘制/几何时，重构前后把同一函数的输出导成 PPM 做 `cmp`——
   "零像素变化"比肉眼可靠得多（本次布局重构就是这么验的：静态场景与阻挡圆列表逐字节一致）。
3. **数值校验**：把物理规则单独拎出来跑（例如放几个不同 value 的球进水池，打印 y/vy 轨迹），
   能一眼看出"该浮的沉了"这类问题。**别只验几何，颜色/数值也要验**。
4. **长局巡检**：跑到几万帧（十几分钟游戏时间），看物理球数量、value 曲线、是否卡球/崩溃。
5. **微基准**：新特效单独链接计时（例如水面 0.036 ms/帧）。**不要**用整体帧率波动判断开销，
   帧率会被环境噪声干扰（见过同一份代码 55~59 FPS 跳动）。

TUI 可以用伪终端驱动脚本化验证（写按键字节 → 读屏幕内容）。

---

## 8. 已知的坑与"看着像 bug 但不是"的现象

- **步进不对称**：画笔球/大球/狙击每帧跑 2 个子步（等效 2 倍配置速度），
  物理球与引力每帧 1 次（1 倍）。这是历史设计，不是 bug。
- **涂画滞后一个子步**：每个子步按"移动前位置"落笔（约 2px），显示层由 `new_paints` 覆盖。
- **基地占领判定里的 `NEW_COLORS` 检查恒为 false**，是冗余防御（因为 canvas 只有纯色）。
- **`demo/` 里的旧工程**：独立 git 历史、已 gitignore、只读参考，不要动也不要删。
- **录像依赖外部 ffmpeg**，缺失时静默不产文件，不是崩溃。
- **领土采样隔 4 像素**（约 6.25 万样本）；领土闪光每帧遍历 100 万像素缓冲（仅显示层）。
- 曾经的严重坑（别再犯）：给运动学叠加力时**重力已经加过一次**，附加力只能补"那一项"——
  浮力第一版把含重力的净加速度又加了一遍，等效重力算两次，表现为"接近阈值的球加速下沉"。
- 曾经的严重坑：位图字形缩放的覆盖率是 **0-255** 量纲，混用 0-1 会让 `Uint8` 溢出成随机色
  （大球数字像电视雪花）。所有像素/颜色类改动都要做逐像素颜色校验。

---

## 9. 当前状态速览（2026-10-07 末）

- 最近几轮改动：中文化界面 → 统一位图字体 → 大球数字随球径等比缩放 →
  中央缺口做成水（无色，只有折射/镜像反射 + 表面高光）→ 水池延伸到底部挡板 →
  升力改成"质量 ∝ value、浮力恒定"的浮力模型（阈值每秒 +1%，浮力随时间变大）→
  删除复活机制 → 布局几何收敛到 `scene.h`。
- 被用户否决、**不要重做**的：物理球动态半径（`d14dd16` 撤销）、×n 区加水浮力。
- `README.md` 的「玩法速览」是当前数值与规则的最新描述；改规则时同步更新它。
