# Ball Territory War

四队（红/绿/蓝/黄）全自动对战模拟器：左半屏是「物理机械区」——物理球在重力、挡板与
×2/×4/×8 倍率带之间循环积累价值（value）；价值越过阈值后从底部 5 格武器栏转化成
画笔球、大球、狙击等武器，去右半屏 1000×1000 战场涂领土、拆基地。
基地被涂掉即淘汰，最后剩一队（或全灭）后倒计时 60 秒结束。

- 语言/依赖：C++17 + SDL3（GPU API），无游戏引擎、无第三方绘图库
- 全部图形都是自己往 `std::vector<Uint8>` RGBA 位图上逐像素光栅化，再整张上传 GPU
- 终端启动器（TUI）：实时战况面板 + 一键启动/构建 + 全部 61 个参数的可视化编辑与热更新，
  纯 C++17 无第三方依赖，另附 status/build/args/config/telemetry 等脚本化子命令

---

## 目录结构

```
.
├── CMakeLists.txt          # 唯一构建入口（产物 build/main）
├── assets/                 # black_wool.png（左侧机械区平铺背景）
├── scripts/                # build / run / run_launcher / open_output / gen_font_atlas.py
├── src/
│   ├── main.cpp            # 窗口、SDL GPU 设备与管线、主循环、录像回读
│   ├── core/               # 与玩法无关的基础设施
│   │   ├── constants.h     # 窗口/战场几何、闪光缓冲等常量
│   │   ├── math_utils.h    # RandFloat / ToFColor
│   │   ├── palette.{h,cpp} # 四角配色（纯色 + 新色）与配色反查
│   │   ├── config.{h,cpp}  # GameConfig + 手写 YAML 子集解析 + SETCONFIG 热更新
│   │   ├── entities.h      # BallObject / PhysicsBall 等玩法实体
│   │   ├── effects.h       # 粒子/拖尾/冲击波/气泡（纯数据，仅显示层）
│   │   ├── state.h         # GameState（实体 + 队伍状态 + 计时）
│   │   └── telemetry.{h,cpp}  # Unix socket 遥测服务线程
│   ├── game/               # 玩法
│   │   ├── scene.{h,cpp}   # 静态场景、基地/阻挡圆布局几何
│   │   ├── physics.{h,cpp} # 机械区循环、武器触发、机枪 AI、引力与碰撞结算
│   │   └── simulation.{h,cpp}  # InitializeGame / StepGame（一帧模拟）
│   ├── render/             # 显示层
│   │   ├── canvas.{h,cpp}  # 像素画布原语（圆/矩形/文字/混合/领土采样）
│   │   ├── font_atlas.{h,cpp}  # 画面全部文字的位图字形表（生成期用系统字体烘焙，运行时零依赖）
│   │   ├── particles.{h,cpp}   # 特效推进与绘制、领土闪光
│   │   ├── water.{h,cpp}   # 升力缺口的水面（涟漪 + 菲涅耳反射 + 折射背景，仅显示层）
│   │   ├── hud.{h,cpp}     # 右侧悬浮状态面板（队名/领土占比/护盾/弹药/复活中）
│   │   ├── render.{h,cpp}  # 显示画布合成顺序
│   │   └── gpu.{h,cpp}     # MSL 着色器与 SDL3 GPU 资源辅助
│   └── io/
│       └── output.{h,cpp}  # ffmpeg 录像管道 + 背景音乐播放列表
├── launcher/               # CLI 启动器（终端 UI + 子命令，产物 build/btw-launcher）
├── demo/                   # 重构前的原始 demo（只读参考，未删除）
└── build/                  # 构建产物（已 gitignore）
```

### 数据流

```
main 循环（60fps）
  ├─ StepGame()      推进一帧模拟，写入持久画布 canvas
  ├─ RenderGame()    canvas → display_canvas，叠加特效/UI
  ├─ 上传纹理 → fs_canvas（饱和/对比/辉光/暗角）+ fs_grid（战场网格）
  └─ 可选：GPU 回读 → ffmpeg（output.mp4，可混入 ~/.ball/music）
遥测线程：/tmp/btw_telemetry.sock 推送 JSON，接收 SETCONFIG 热更新
```

两张画布是理解本项目的关键：

| 画布 | 说明 |
|---|---|
| `canvas`（持久/玩法层） | 领土判定的唯一真相。只写入 `PURE_COLORS`（纯色）与 `NEW_COLORS`（柔和显示色）；领土统计**只认纯色** |
| `display_canvas`（每帧重建） | 拷贝 canvas 后叠加新色涂画、拖尾、粒子、冲击波、闪光、护盾、炮塔、HUD、数值 |

---

## 构建与运行

```bash
# 只编译（不启动）
scripts/build.sh

# 编译并启动（会开窗口，默认录制 output.mp4）
scripts/run.sh                       # 额外参数原样传给游戏
scripts/run.sh --no-record --max-frames 1200
scripts/run.sh -d 30                 # 跑 30 秒后正常收尾退出

# 手动构建
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j

# 启动器（终端 UI，无第三方依赖）
scripts/run_launcher.sh
```

依赖：SDL3（`brew install sdl3`）、CMake ≥ 3.16、C++17 编译器；
ffmpeg 可选（缺失则录像静默失效，游戏本身照常运行）。两个可执行文件（`build/main`
与 `build/btw-launcher`）由同一个 CMake 工程构建。

### 命令行参数

| 参数 | 说明 |
|---|---|
| `-u` / `--unlimited` | 不限帧（IMMEDIATE 呈现；录制时关闭 MSAA） |
| `-nr` / `--no-record` | 不录制视频 |
| `-nm` / `--no-music` | 不混入 `~/.ball/music` 的背景音乐 |
| `-o` / `--output <文件>` | 录像输出路径（默认 output.mp4；**缺扩展名会自动补 .mp4**，否则 ffmpeg 无法判断封装格式而放弃录制） |
| `-d` / `--duration <秒>` | 运行指定秒数后正常收尾退出 |
| `--max-frames <N>` | 运行 N 帧后退出（0 = 不限） |
| `--no-weapon-lift` | 关闭 ×8/×4/×2 与霰弹/狙击列的升力（中间升力保留） |
| `--write-config` | 只生成默认配置文件后退出 |

---

## CLI 启动器（终端 UI）

`scripts/run_launcher.sh`（或 `./build/btw-launcher`）打开终端界面：

```
 Ball Territory War  CLI 启动器                    ● LIVE   游戏运行中 · pid 8123
╭ 环境 ──────────────────────────────────────────────────────────────────────╮
│ 项目  /path/to/BallTerritoryWar                                            │
│ 游戏  ● build/main 已构建   录像  ● ffmpeg 可用   音乐  ● 3 首              │
╰────────────────────────────────────────────────────────────────────────────╯
╭ 实时战况 · 遥测 /tmp/btw_telemetry.sock ───────────────────────────────────╮
│ 队伍  领土占比                        百分比  大球  护盾       弹药        │
│ 红队  ███████░░░░░░░░░░░░░░░░░░░░░░░  23.4%   2     9.80M      246.9k      │
│ 绿队  █████░░░░░░░░░░░░░░░░░░░░░░░░░  18.0%   1     10.00M     250.0k      │
│ 蓝队  ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░  已灭    3     -          -           │
│ 总球数 453    物理球 16    时间 2:36    FPS 60.2                           │
╰────────────────────────────────────────────────────────────────────────────╯
╭ 启动选项 ──────────────────────────────────────────────────────────────────╮
│ > 输出文件            output.mp4                                           │
│ 录制视频 / 背景音乐 / 限制 60fps / 武器升力：恒定开启                      │
╰────────────────────────────────────────────────────────────────────────────╯
  Enter / o 编辑输出文件名   l 启动   b 构建   c 配置   r 刷新环境   q 退出
```

终端支持真彩（`COLORTERM=truecolor`）时自动启用 24bit 主题：深色面板底色、队伍配色与
游戏显示层一致、领土进度条、状态徽标；不支持的终端自动退回 16 色，也可用
`--truecolor` / `--16color` / `--no-color`（或 `NO_COLOR`）强制。超宽终端下内容限宽
120 列居中。

- 主面板：`Enter`/`o` 编辑录像输出文件名、`l` 启动（二次确认）、`b` 构建、`c` 配置、
  `q` 退出；录制视频 / 背景音乐 / 限制 60fps / 武器升力恒定开启（不传任何关闭参数）
- 配置面板：12 组 61 键，`Enter` 编辑（越界自动夹取）、`w` 写入 YAML 并热更新、
  `a` 仅热更新、`d` 恢复默认、`r` 重读、`Esc` 返回
- 启动游戏时其 stdout/stderr 重定向到 `build/game.log`，界面底部实时显示末尾几行
- 非交互子命令（脚本/CI 可用，行为与界面一致）：

```bash
./build/btw-launcher status                    # 环境状态
./build/btw-launcher build                     # cmake + make
./build/btw-launcher args                      # 打印将传给游戏的命令行（不启动）
./build/btw-launcher launch --dry-run          # 去掉 --dry-run 才真的启动
./build/btw-launcher config list|get|set|apply|reset
./build/btw-launcher telemetry -s 3 [--raw]    # 打印遥测帧
```

细节见 `launcher/README.md`。

---

## 玩法速览

**价值经济（左半屏）**：物理球在机械区弹跳，落到 y=390~410 的 ×8/×4/×2 倍率带上即
`value ×= 倍率` 并被重新抛回顶部。中间缺口（x 225~375）是一柱**水**（y=390~430，
一直铺到下方挡板的高度），球的"质量"与 `value` 成正比而**浮力对所有球相同**，
于是净加速度 `a = g × (1 − 阈值 / value)`：低于阈值上浮（越轻越快）、等于阈值悬浮、
高于阈值下沉（浮力仍托着，比空气中慢）；水里还有竖向流体阻尼，轻球会稳稳浮在水面小幅起伏。
倍率带上方另有一道升力，只托起超过对应阈值的球。阈值（`lift.initialThreshold` 起）
每秒 +1% **复利增长且没有上限**（原 maxThreshold / weaponMax 已移除）→ **浮力随时间变大**，
原本沉下去的球过一阵会重新浮起来。前 10 分钟额外限制：`value > 2e6` 的球在霰弹列与
狙击列会被顶回。

水本身**无色**（`render/water.cpp`，纯显示层）：看到的颜色全部来自背景（黑色羊毛）——
折射按斜率与深度把背景位移后双线性重采样，菲涅耳反射则是**关于水面镜像采样**上方那块
背景（水平偏移随斜率变化），唯一的"自带"亮色是表面那条白色高光（由掠射强度驱动）。
水面几何与升力区共用同一组常量（`game/scene.h` 的 `LIFT_*` 与 `InLiftWater()`），
所以"只有有水的地方才有升力"。

**底部 5 格武器栏**（y=980，每格 120px）：霰弹(0-120) / 机枪(120-240) / 护盾(240-360) /
大球(360-480) / 狙击(480-600)。触发后球的 value 归 1 并重置。

**机枪 AI**：锁定 300px 内的敌方大球，按子弹飞行时间逐步模拟大球未来轨迹（含四壁反弹）
求拦截点；威胁高时 10 倍火力甚至梭哈（弹药 < 大球价值时一次性打完）。

**战斗规则**：`combat.absorbRatio`(20) 倍价值差直接吞并；否则双方各扣「较大者 ×
`damageRatio`(0.1)」，较小者被扣死则由大者吃掉其价值。大球半径随 value 实时变化
（`(log(value)+5)×2`，**球内数值文字也随球径等比缩放**），每帧抛出一颗价值
`bigBall.trailValue` 的尾流小球；
**引力一律按大球 value 线性计算**（`a = G × value / r²`，同色不吸、速度上限兜底）：
敌对大球互相吸引（`bigBall.gravityStrength`），**狙击爆炸碎片也被大球吸引、
但不会被己方（同色）大球吸引**（`bigBall.fragmentGravityStrength`）；
**大球每 `bigBall.splitIntervalSeconds` 秒分裂一次，变成两个 value/2 的大球**
（子球重新计时、沿垂直方向分离，0 = 关闭）；
狙击按自身 value 吸引半径内一切实体；**进入狙击引力场的小球每秒 +1 value**
（`sniper.gravityValueGain`，0 = 关闭；大球与狙击自身不计，与引力开关相互独立）。

**复活**（`revive` 组）：基地沦陷后，若该颜色还剩 **≥ `revive.minPhysicsBalls`(2) 个物理球**，
则**保留物理球不转化**（颜色不会就此消失——它要复活），随后分三个阶段：

**① 等待期 `delaySeconds`(60s)**：该颜色的物理球在这段时间里**落入任何武器格都不发射武器**，
而是把「价值 × `shieldValueScale`(1.0)」**累加成护盾**（球重置为 value=1 回顶部继续跑，可反复喂）；
倍率带 `×8/×4/×2` 照常生效，所以先吃倍率再喂武器格收益更高。

**② 起飞**：等待结束，**value 最大的那个物理球原地停住 `stopSeconds`(0.5s)**，
再以 `flightSpeed`(400px/s) **飞向自己的炮塔**（画在所有 UI 之上）。

**③ 到达炮塔**：1) 颜色重新存活（`color_alive = true`）；2) 该球价值 × `shieldValueScale`
**累加到护盾**（等待期攒的护盾不会被覆盖丢失）；3) **护盾圈内所有像素刷回该队纯色**
（领土恢复，带闪光反馈）；该物理球消耗掉。

只剩 1 个物理球（或 `minPhysicsBalls = 0` 关闭）时不复活，维持原逻辑：物理球全部
转化为大球从基地发射。等待期与飞行中的颜色**都不算被消灭**，因此结束倒计时不会在复活流程中触发。

复活流程的状态显示：游戏内 HUD 该队第一行显示 **`复活中`**（等待期与飞行期都显示，
色点仍用队伍色），护盾/弹药列在复活流程中也照常显示，因此等待期武器格攒的护盾在游戏内
直接看得见；终端启动器该队「百分比」列显示 **`复活 47s`**（等待期倒计时）或
**`复活中`**（已起飞），彻底出局才是「已灭」。
遥测提供 `"reviving":[b,b,b,b]` 与 `"reviveIn":[s,s,s,s]`（等待剩余秒数）两个数组。

**胜负**：基地 = 四角**半径 100 的纯色圆**（圆心距两条边各 100px，即原来的护盾圈大小；
盾牌圈默认半径 80，落在圆内），中心 20×20 区域出现任何非本队色像素即沦陷
（弹药清零；有足够物理球时走上面的复活流程，否则物理球转为大球）。
存活 ≤1 队且场上无该队敌人（亡队大球）时开始 3600 帧倒计时后结束。

---

## 配置（~/.ball/config.yaml）

首次运行自动生成带注释的默认配置，12 组 61 键（paintBalls / physics / lift / machineGun /
shotgun / sniper / bigBall / shield / combat / gameOver / startup / revive）。解析器是手写 YAML 子集：
支持 `key: value`、缩进嵌套、`- ` 列表、`#` 注释；不支持 Tab 缩进、锚点、流式集合。
出错或字段缺失时该项回退内置默认值并打印日志，删除文件即可恢复默认。

游戏运行时可通过遥测 socket 热更新：`SETCONFIG <点分键> <值...>` → `OK` / `ERR <原因>`。
启动器「配置」窗口即走这条链路（同时把改动写回 YAML）。少数启动期参数（物理球数量、
初始弹药/护盾等）需重启生效。

## 遥测协议

游戏后台线程监听 `/tmp/btw_telemetry.sock`，无客户端时零开销。每行一个 JSON（数据每 30 帧
即 0.5s 采样一次，发送循环 50ms 重复推送）：

```json
{"territory":[0.25,0.3,0.2,0.1],"bigBalls":[2,3,1,0],"totalBalls":453,
 "physicsValues":[5000,6000,8000,5000],"physicsCount":16,
 "shields":[10000000,10000000,10000000,10000000],
 "ammo":[246894,246894,246894,246894],"alive":[true,true,true,true],
 "elapsed":2.6,"fps":60.2}
```

---

## 实现要点 / 已知限制

- **每帧 2 个子步**：`StepGame` 内部跑 2 次 `dt = 1/60` 的模拟，因此画笔球/大球/狙击的
  实际速度是配置值的 2 倍；而物理球（`UpdatePhysicsBalls`）每帧只跑 1 次，是 1 倍。
  调平衡参数时必须记住这个不对称。
- **画面里的全部文字走同一套位图字形**（`render/font_atlas.{h,cpp}`）：队名、武器名、
  领土占比、护盾/弹药、球价值、倍率带 `×8/×4/×2` 都是 16px 中文/数字位图——由
  `scripts/gen_font_atlas.py` 在生成期用系统字体（Hiragino Sans GB W6）烘焙成
  抗锯齿覆盖率位图，**运行时不加载字体文件、不引入第三方依赖**（窗口截图/录像与
  字体渲染效果一致）。项目里已不再有 5×7 点阵字体；改文案、加字或换字体后重跑该脚本
  （需 Pillow）即可，未收录的字符会按 8px 空过而不显示。
- **大球内数字随球径等比缩放**：字表有两套（16px 主表 + 32px 大号数值表），
  `DrawTextFontScaled()` 按「相对主字号的缩放」绘制——放大时改用大号表缩小采样
  （区域平均抗锯齿），两套表按基线对齐；比例写在 `render.cpp` 的
  `text_scale = max(0.75, 半径 / 37.6)`，改这里即可调文字与球的大小关系。
- 领土采样为战场区域隔 4 像素（1/16 样本），约 6.25 万样本。
- 领土闪光每帧遍历 100 万像素缓冲（仅显示层）。
- 录像依赖外部 ffmpeg；未安装时不报错但不会产生文件。

## 本次重构说明（相对 demo/）

行为完全等价，只做结构调整（已通过全新目录 `cmake` 构建验证：0 error / 0 warning，
未运行程序）：

| 原路径 | 新路径 |
|---|---|
| `main.cpp` | `src/main.cpp` |
| `src/game.h`（609 行“万能头”） | 拆分为 `core/{constants,math_utils,palette,config,entities,effects,state,telemetry}.h` + `game/{scene,physics,simulation}.h` + `render/{canvas,particles,hud,render,gpu}.h` + `io/output.h` |
| `src/config.cpp` / `src/telemetry.cpp` | `src/core/` |
| `src/physics.cpp` / `scene.cpp` / `simulation.cpp` | `src/game/` |
| `src/canvas.cpp` / `particles.cpp` / `hud.cpp` / `render.cpp` / `gpu.cpp` | `src/render/` |
| `src/output.cpp` | `src/io/` |
| `black_wool.png` | `assets/black_wool.png`（加载顺序：根/assets → build 的 ../assets → 编译期源码目录 → 旧路径兜底） |
| `run.sh` / `run_launcher.sh` / `open_output.sh` | `scripts/`（改为相对脚本自身定位项目根） |

同时完成的清理：

- 删除死代码 `DrawTextBlend`（全项目零调用）
- 把错位的定义归位：`CanvasPixelMatches`（physics.cpp → canvas.cpp）、
  `GetColorBlockCenter`/`GetBlockingCircles`（physics.cpp → scene.cpp）、
  `PURE_COLORS`/`NEW_COLORS`/`Find*ColorIndex`（physics.cpp → core/palette.cpp）
- 各 .cpp 只包含自己需要的头文件，不再依赖“万能头”的间接包含
- `build/` 不再随源码分发（原 demo 里的 CMakeCache 记录了别的机器的绝对路径，会导致
  就地 `cmake ..` 失败）
- 启动器：项目根识别增加 `CMakeLists.txt` 标记（`main.cpp` 已移入 `src/`），
  构建目录缺失时自动创建
- **启动器重写**：原 Tauri（Rust + 两个 HTML）启动器替换为 C++17 终端 UI
  （`launcher/src/`，与游戏同一个 CMake 工程，零第三方依赖）。功能对齐：环境状态、
  实时战况、61 键配置编辑与热更新、一键构建；新增：启动二次确认、
  游戏日志重定向与界面内查看、非交互子命令（便于脚本化与自动化测试）。
  启动选项只保留录像输出文件名，录制/音乐/限帧/武器升力改为恒定开启
- 升力阈值上限（`lift.maxThreshold` / `lift.weaponMax`）已删除：阈值按 `growthPerSecond`
  无限增长。旧配置文件里残留的这两项会被游戏忽略（解析器不认识的多余键不影响加载）

未纳入重构的内容：`demo/BallTerritoryWar/launcher/redesign-backup/`（未跟踪的旧设计稿，
含指向不存在字体的 `@font-face` 与已废弃的诊断信标）保留在 demo 中，未复制。
