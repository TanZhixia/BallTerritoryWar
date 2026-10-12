// ==================== 共享类型 ====================

// 游戏每 0.5s 推的一行遥测 JSON（见 src/core/telemetry.cpp）
export interface TelemetryFrame {
  territory: number[] // 4 队领土占比 0..1
  bigBalls: number[] // 4 队大球数
  totalBalls: number
  physicsValues: number[] // 4 队物理球 value 总和
  physicsCount: number
  shields: number[] // 4 队护盾
  ammo: number[] // 4 队机枪弹药
  alive: boolean[] // 4 队是否存活
  elapsed: number // 本局已运行秒数
  fps: number
}

// 环境探测结果（对齐 launcher/src/project.h 的 EnvStatus）
export interface EnvStatus {
  root: string
  gamePath: string
  gameExists: boolean
  ffmpeg: boolean
  musicDir: string
  musicCount: number
  configPath: string
  configExists: boolean
  launcherConfigPath: string
}

export interface GameStatus {
  running: boolean
  pid: number | null
  exitCode: number | null
}

export interface ConfigUpdate {
  key: string
  value: string
}
