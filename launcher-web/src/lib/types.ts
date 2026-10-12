// 前端类型（与 server/types.ts、server/schema.ts 对应；schema 本身由 /api/config 获取，不在这里重复）

export interface TelemetryFrame {
  territory: number[]
  bigBalls: number[]
  totalBalls: number
  physicsValues: number[]
  physicsCount: number
  shields: number[]
  ammo: number[]
  alive: boolean[]
  elapsed: number
  fps: number
}

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

export type ItemType = 'float' | 'int' | 'array'

export interface ConfigItem {
  key: string
  label: string
  desc: string
  type: ItemType
  count: number
  min: number
  max: number
  defaults: number[]
  restartOnly: boolean
}

export interface ConfigGroup {
  group: string
  label: string
  items: ConfigItem[]
}

export interface ApiConfig {
  schema: ConfigGroup[]
  values: Record<string, string>
  configPath: string
  error: string
}

export interface ConfigUpdate {
  key: string
  value: string
}

export interface ConfigResult {
  errors: string[]
  written?: { count: number; appended: string[] }
  applied?: { count: number; failures: string[]; connected: boolean }
}
