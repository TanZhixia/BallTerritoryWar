// ==================== 遥测：Unix 域套接字客户端 + SETCONFIG ====================
// 移植自 launcher/src/telemetry.cpp：
//   * 一条持久连接读取游戏推送的 JSON 行（只接受含 territory 字段的战况帧），
//     断开后 0.8s 退避重连
//   * SETCONFIG 用独立短连接逐条下发并读取 OK/ERR

import net from 'node:net'
import { EventEmitter } from 'node:events'

import type { TelemetryFrame } from './types'

export const SOCKET_PATH = '/tmp/btw_telemetry.sock'
const RECONNECT_MS = 800
const RECV_TIMEOUT_MS = 300

export class TelemetryHub extends EventEmitter {
  latest: TelemetryFrame | null = null
  connected = false

  private sock: net.Socket | null = null
  private buf = ''
  private reconnectTimer: NodeJS.Timeout | null = null

  start(): void {
    this.connect()
  }

  stop(): void {
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer)
      this.reconnectTimer = null
    }
    this.sock?.destroy()
    this.sock = null
  }

  private connect(): void {
    if (this.sock) return
    const sock = net.connect({ path: SOCKET_PATH })
    this.sock = sock
    sock.setNoDelay(true)

    sock.on('connect', () => {
      this.connected = true
      this.buf = ''
      this.emit('status')
    })
    sock.on('data', (data) => this.onData(data))
    // 连接错误与断开都走 close，统一重连
    sock.on('error', () => {
      /* 由 close 处理 */
    })
    sock.on('close', () => {
      this.sock = null
      if (this.connected) {
        this.connected = false
        this.emit('status')
      }
      this.scheduleReconnect()
    })
  }

  private scheduleReconnect(): void {
    if (this.reconnectTimer) return
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null
      this.connect()
    }, RECONNECT_MS)
  }

  private onData(data: Buffer): void {
    this.buf += data.toString('utf8')
    let idx = this.buf.indexOf('\n')
    while (idx >= 0) {
      const line = this.buf.slice(0, idx)
      this.buf = this.buf.slice(idx + 1)
      idx = this.buf.indexOf('\n')
      if (line.trim() === '') continue
      let parsed: unknown
      try {
        parsed = JSON.parse(line)
      } catch {
        continue
      }
      if (parsed == null || typeof parsed !== 'object' || !('territory' in parsed)) {
        continue // 只接受战况帧
      }
      this.latest = parsed as TelemetryFrame
      this.emit('frame', this.latest)
    }
  }

  // 返回失败项描述；空数组表示全部成功
  applyConfig(updates: Map<string, string>): Promise<string[]> {
    const failures: string[] = []
    if (updates.size === 0) return Promise.resolve(failures)
    // 与 C++ std::map 一致：按键字典序下发
    const entries = [...updates.entries()].sort((a, b) => (a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0))

    return new Promise((resolve) => {
      const sock = net.connect({ path: SOCKET_PATH })
      let settled = false
      const finish = () => {
        if (settled) return
        settled = true
        sock.destroy()
        resolve(failures)
      }
      sock.setNoDelay(true)
      sock.on('error', () => {
        if (!settled) {
          failures.push('游戏未运行（无法连接遥测 socket）')
          finish()
        }
      })
      sock.on('connect', async () => {
        try {
          for (const [key, value] of entries) {
            sock.write(`SETCONFIG ${key} ${value}\n`)
            const reply = await readLine(sock, RECV_TIMEOUT_MS)
            if (!reply.startsWith('OK')) {
              failures.push(`${key}：${reply === '' ? '无响应' : reply}`)
            }
          }
        } finally {
          finish()
        }
      })
    })
  }
}

function readLine(sock: net.Socket, timeoutMs: number): Promise<string> {
  return new Promise((resolve) => {
    let buf = ''
    const onData = (data: Buffer) => {
      buf += data.toString('utf8')
      const idx = buf.indexOf('\n')
      if (idx >= 0) {
        cleanup()
        resolve(buf.slice(0, idx))
      }
    }
    const onClose = () => {
      cleanup()
      resolve('')
    }
    const timer = setTimeout(() => {
      cleanup()
      resolve('') // 超时视为无响应
    }, timeoutMs)
    function cleanup() {
      clearTimeout(timer)
      sock.off('data', onData)
      sock.off('close', onClose)
    }
    sock.on('data', onData)
    sock.on('close', onClose)
  })
}
