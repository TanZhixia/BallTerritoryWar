// ==================== 后端入口：REST + SSE + 静态资源 ====================
// 浏览器无法直连 Unix socket / 读写 ~/.ball，所有能力都经这里桥接。
// 只监听 127.0.0.1（本机工具，且会执行 cmake / 启动进程）。

import express, { type Request, type Response } from 'express'
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

import { configEntries, configSchema, findItem, fullKey, type ConfigItem } from './schema'
import {
  configFilePath,
  defaultValueString,
  loadConfigFile,
  loadLaunchOptions,
  normalizeValue,
  normalizeOutputName,
  saveLaunchOptions,
  writeConfigFile,
} from './yaml'
import { GameRunner, buildGame, findProjectRoot, queryEnv, gameLogPath } from './project'
import { TelemetryHub } from './telemetry'
import type { ConfigUpdate } from './types'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const distDir = path.resolve(__dirname, '..', 'dist')
const root = findProjectRoot()
const PORT = Number.parseInt(process.env.PORT ?? '3070', 10)
const HOST = '127.0.0.1'

const hub = new TelemetryHub()
hub.start()
const game = new GameRunner()

const app = express()
app.use(express.json({ limit: '1mb' }))

// ---------- SSE 工具 ----------
function openSse(res: Response): void {
  res.set({
    'Content-Type': 'text/event-stream',
    'Cache-Control': 'no-cache, no-transform',
    Connection: 'keep-alive',
    'X-Accel-Buffering': 'no',
  })
  res.flushHeaders?.()
}
function sseSend(res: Response, event: string, data: unknown): void {
  res.write(`event: ${event}\ndata: ${JSON.stringify(data)}\n\n`)
}

// ---------- 环境 ----------
app.get('/api/env', (_req, res) => {
  res.json(queryEnv(root))
})

// ---------- 配置 ----------
app.get('/api/config', (_req, res) => {
  const { values, error } = loadConfigFile()
  res.json({
    schema: configSchema(),
    values: Object.fromEntries(values),
    configPath: configFilePath(),
    error,
  })
})

interface ResolvedKey {
  item: ConfigItem
  index: number
  group: string
}

function resolveKey(dotted: string): ResolvedKey | null {
  const first = dotted.indexOf('.')
  if (first === -1) return null
  const group = dotted.slice(0, first)
  const rest = dotted.slice(first + 1)
  const second = rest.lastIndexOf('.')
  if (second !== -1) {
    const base = rest.slice(0, second)
    const idx = Number.parseInt(rest.slice(second + 1), 10)
    const item = findItem(group, base)
    if (item && item.type === 'array' && Number.isInteger(idx) && idx >= 0 && idx < item.count) {
      return { item, index: idx, group }
    }
  }
  const item = findItem(group, rest)
  if (!item) return null
  return { item, index: item.type === 'array' ? -1 : 0, group }
}

function buildUpdates(updates: ConfigUpdate[]): { map: Map<string, string>; errors: string[] } {
  const map = new Map<string, string>()
  const errors: string[] = []
  for (const { key, value } of updates) {
    const resolved = resolveKey(key)
    if (!resolved) {
      errors.push(`未知配置键：${key}`)
      continue
    }
    const res = normalizeValue(resolved.item, value)
    if (res.error !== '') {
      errors.push(`${key}：${res.error}`)
      continue
    }
    map.set(fullKey(resolved.group, resolved.item, resolved.index), res.value)
  }
  return { map, errors }
}

app.post('/api/config', async (req, res) => {
  const body = req.body as { updates?: ConfigUpdate[]; mode?: string }
  const updates = Array.isArray(body.updates) ? body.updates : []
  const mode = body.mode === 'apply' || body.mode === 'both' ? body.mode : 'save'
  const { map, errors } = buildUpdates(updates)

  const result: {
    errors: string[]
    written?: { count: number; appended: string[] }
    applied?: { count: number; failures: string[]; connected: boolean }
  } = { errors }

  if (map.size > 0 && (mode === 'save' || mode === 'both')) {
    const w = writeConfigFile(map)
    if (w.error !== '') errors.push(w.error)
    else result.written = { count: map.size, appended: w.appended }
  }
  if (map.size > 0 && (mode === 'apply' || mode === 'both')) {
    const failures = await hub.applyConfig(map)
    result.applied = { count: map.size, failures, connected: hub.connected }
  }
  res.json(result)
})

app.post('/api/config/reset', async (_req, res) => {
  const map = new Map<string, string>()
  for (const entry of configEntries()) {
    map.set(fullKey(entry.group, entry.item, entry.index), defaultValueString(entry.item, entry.index))
  }
  const w = writeConfigFile(map)
  const failures = await hub.applyConfig(map)
  res.json({ count: map.size, appended: w.appended, failures })
})

// ---------- 启动选项 ----------
app.get('/api/launch-options', (_req, res) => {
  res.json(loadLaunchOptions())
})

app.put('/api/launch-options', (req, res) => {
  const output = String((req.body as { output?: string }).output ?? '').trim() || 'output.mp4'
  saveLaunchOptions({ output })
  res.json({ output })
})

// ---------- 遥测 ----------
app.get('/api/telemetry/latest', (_req, res) => {
  res.json({ connected: hub.connected, frame: hub.latest })
})

app.get('/api/telemetry/stream', (req: Request, res: Response) => {
  openSse(res)
  sseSend(res, 'status', { connected: hub.connected })
  if (hub.latest) sseSend(res, 'frame', hub.latest)

  const onFrame = (frame: unknown) => sseSend(res, 'frame', frame)
  const onStatus = () => sseSend(res, 'status', { connected: hub.connected })
  hub.on('frame', onFrame)
  hub.on('status', onStatus)

  const keepAlive = setInterval(() => res.write(': ping\n\n'), 20000)
  req.on('close', () => {
    clearInterval(keepAlive)
    hub.off('frame', onFrame)
    hub.off('status', onStatus)
  })
})

// ---------- 构建（流式文本） ----------
app.post('/api/build', async (_req, res) => {
  res.set({ 'Content-Type': 'text/plain; charset=utf-8', 'Cache-Control': 'no-cache' })
  res.flushHeaders?.()
  res.write(`构建中：cmake + make（${root}/build）\n`)
  const { code } = await buildGame(root, (chunk) => res.write(chunk))
  if (code !== 0) {
    res.write(`\n构建失败（退出码 ${code}）\n`)
  } else {
    res.write(`\n构建完成：${root}/build/main\n`)
  }
  res.end()
})

// ---------- 启动 / 状态 / 日志 ----------
app.post('/api/launch', (req, res) => {
  const output = normalizeOutputName(String((req.body as { output?: string }).output ?? '').trim())
  saveLaunchOptions({ output })
  const result = game.launch(root, output)
  if (result.error) {
    res.status(400).json({ error: result.error })
    return
  }
  res.json({ pid: result.pid ?? null, logPath: gameLogPath(root) })
})

app.get('/api/game', (_req, res) => {
  res.json(game.status())
})

app.get('/api/logs/stream', (req: Request, res: Response) => {
  openSse(res)
  let last = ''
  const tick = setInterval(() => {
    const lines = game.tail(root, 200)
    const text = lines.join('\n')
    if (text !== last) {
      last = text
      sseSend(res, 'log', { lines })
    }
  }, 500)
  req.on('close', () => clearInterval(tick))
})

// ---------- 静态资源（生产：dist/；开发由 Vite 提供） ----------
if (fs.existsSync(distDir)) {
  app.use(express.static(distDir))
  app.get(/^(?!\/api).*/, (_req, res) => {
    res.sendFile(path.join(distDir, 'index.html'))
  })
}

app.listen(PORT, HOST, () => {
  // eslint-disable-next-line no-console
  console.log(`btw-launcher-web 后端已启动：http://${HOST}:${PORT}`)
  console.log(`项目根：${root}`)
  if (!fs.existsSync(distDir)) {
    console.log('（未发现 dist/：开发模式请用 Vite 访问 3070；生产请先 npm run build）')
  }
})
