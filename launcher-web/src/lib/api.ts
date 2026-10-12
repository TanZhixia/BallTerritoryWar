import type {
  ApiConfig,
  ConfigResult,
  ConfigUpdate,
  EnvStatus,
  GameStatus,
  TelemetryFrame,
} from './types'

async function json<T>(res: Response): Promise<T> {
  if (!res.ok) {
    const body = (await res.json().catch(() => ({}))) as { error?: string }
    throw new Error(body.error ?? `请求失败（${res.status}）`)
  }
  return (await res.json()) as T
}

export function getEnv(): Promise<EnvStatus> {
  return fetch('/api/env').then(json<EnvStatus>)
}

export function getConfig(): Promise<ApiConfig> {
  return fetch('/api/config').then(json<ApiConfig>)
}

export function postConfig(updates: ConfigUpdate[], mode: 'save' | 'apply' | 'both'): Promise<ConfigResult> {
  return fetch('/api/config', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ updates, mode }),
  }).then(json<ConfigResult>)
}

export function resetConfig(): Promise<{ count: number; appended: string[]; failures: string[] }> {
  return fetch('/api/config/reset', { method: 'POST' }).then(
    json<{ count: number; appended: string[]; failures: string[] }>,
  )
}

export function getLaunchOptions(): Promise<{ output: string }> {
  return fetch('/api/launch-options').then(json<{ output: string }>)
}

export function putLaunchOptions(output: string): Promise<{ output: string }> {
  return fetch('/api/launch-options', {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ output }),
  }).then(json<{ output: string }>)
}

export function getGameStatus(): Promise<GameStatus> {
  return fetch('/api/game').then(json<GameStatus>)
}

export function launchGame(output: string): Promise<{ pid: number | null; logPath: string }> {
  return fetch('/api/launch', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ output }),
  }).then(json<{ pid: number | null; logPath: string }>)
}

// 构建：POST 返回分块文本流，逐块回调
export async function runBuild(onChunk: (chunk: string) => void): Promise<void> {
  const res = await fetch('/api/build', { method: 'POST' })
  if (!res.body) return
  const reader = res.body.getReader()
  const decoder = new TextDecoder()
  for (;;) {
    const { done, value } = await reader.read()
    if (done) break
    onChunk(decoder.decode(value, { stream: true }))
  }
}

export interface TelemetryHandlers {
  onFrame: (frame: TelemetryFrame) => void
  onStatus: (connected: boolean) => void
}

export function subscribeTelemetry(handlers: TelemetryHandlers): () => void {
  const es = new EventSource('/api/telemetry/stream')
  es.addEventListener('frame', (e) => handlers.onFrame(JSON.parse((e as MessageEvent).data)))
  es.addEventListener('status', (e) =>
    handlers.onStatus(JSON.parse((e as MessageEvent).data).connected as boolean),
  )
  return () => es.close()
}

export function subscribeLogs(onLines: (lines: string[]) => void): () => void {
  const es = new EventSource('/api/logs/stream')
  es.addEventListener('log', (e) => onLines(JSON.parse((e as MessageEvent).data).lines as string[]))
  return () => es.close()
}
