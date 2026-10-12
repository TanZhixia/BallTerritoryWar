// ==================== 项目定位 / 环境探测 / 构建 / 启动 / 日志 ====================
// 移植自 launcher/src/project.cpp

import { spawn, type ChildProcess } from 'node:child_process'
import { EventEmitter } from 'node:events'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'

import { configFilePath, launcherConfPath, buildGameArgs } from './yaml'
import type { EnvStatus, GameStatus } from './types'

const MUSIC_EXT = ['.mp3', '.m4a', '.wav', '.flac', '.aac', '.ogg']

function isProjectRoot(dir: string): boolean {
  if (dir === '') return false
  return fs.existsSync(path.join(dir, 'CMakeLists.txt')) && fs.existsSync(path.join(dir, 'src', 'main.cpp'))
}

function ascendToRoot(start: string): string | null {
  let cur = start
  for (let i = 0; i < 12 && cur !== ''; i++) {
    if (isProjectRoot(cur)) return cur
    const parent = path.dirname(cur)
    if (parent === cur) break
    cur = parent
  }
  return null
}

export function findProjectRoot(): string {
  const env = process.env.BTW_PROJECT_DIR
  if (env && env !== '' && isProjectRoot(env)) return env
  const found = ascendToRoot(process.cwd())
  if (found) return found
  return process.cwd()
}

function commandExists(name: string): boolean {
  const dirs = (process.env.PATH ?? '').split(path.delimiter)
  return dirs.some((dir) => {
    try {
      fs.accessSync(path.join(dir, name), fs.constants.X_OK)
      return true
    } catch {
      return false
    }
  })
}

function countMusicFiles(dir: string): number {
  let count = 0
  try {
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
      if (!entry.isFile()) continue
      const ext = path.extname(entry.name).toLowerCase()
      if (MUSIC_EXT.includes(ext)) count++
    }
  } catch {
    return 0
  }
  return count
}

export function queryEnv(root: string): EnvStatus {
  const gamePath = path.join(root, 'build', 'main')
  const musicDir = path.join(os.homedir(), '.ball', 'music')
  const cfgPath = configFilePath()
  return {
    root,
    gamePath,
    gameExists: fs.existsSync(gamePath),
    ffmpeg: commandExists('ffmpeg'),
    musicDir,
    musicCount: countMusicFiles(musicDir),
    configPath: cfgPath,
    configExists: fs.existsSync(cfgPath),
    launcherConfigPath: launcherConfPath(),
  }
}

export function gameLogPath(root: string): string {
  return path.join(root, 'build', 'game.log')
}

// 在 <root>/build 执行 cmake .. && make；输出通过 onData 流式回调
export function buildGame(root: string, onData: (chunk: string) => void): Promise<{ code: number }> {
  const buildDir = path.join(root, 'build')
  fs.mkdirSync(buildDir, { recursive: true })
  const cores = Math.max(1, os.cpus().length)
  const cmd = `cmake .. && make -j${cores} 2>&1`
  return new Promise((resolve) => {
    const child = spawn('sh', ['-c', cmd], { cwd: buildDir })
    child.stdout.on('data', (d: Buffer) => onData(d.toString('utf8')))
    child.stderr.on('data', (d: Buffer) => onData(d.toString('utf8')))
    child.on('error', (err) => {
      onData(`\n无法执行构建命令：${err.message}\n`)
      resolve({ code: 1 })
    })
    child.on('close', (code) => resolve({ code: code ?? 1 }))
  })
}

// fork/exec 启动游戏：工作目录 = 项目根，stdout/stderr 重定向到 build/game.log
export class GameRunner extends EventEmitter {
  private child: ChildProcess | null = null
  private _pid: number | null = null
  private _exitCode: number | null = null

  status(): GameStatus {
    return { running: this.child !== null, pid: this._pid, exitCode: this._exitCode }
  }

  launch(root: string, output: string): { pid?: number; error?: string } {
    const game = path.join(root, 'build', 'main')
    if (!fs.existsSync(game)) {
      return { error: `未找到游戏程序：${game}（请先构建）` }
    }
    const args = buildGameArgs({ output })
    let fd: number
    try {
      fd = fs.openSync(gameLogPath(root), 'w')
    } catch (err) {
      return { error: `无法写入日志：${(err as Error).message}` }
    }
    let child: ChildProcess
    try {
      child = spawn(game, args, { cwd: root, stdio: ['ignore', fd, fd] })
    } catch (err) {
      fs.closeSync(fd)
      return { error: `启动失败：${(err as Error).message}` }
    }
    fs.closeSync(fd)
    this.child = child
    this._pid = child.pid ?? null
    this._exitCode = null
    child.on('exit', (code) => {
      this.child = null
      this._exitCode = code ?? -1
      this.emit('exit', this._exitCode)
    })
    child.on('error', (err) => {
      this.child = null
      this._exitCode = -1
      this.emit('exit', -1, err.message)
    })
    return { pid: this._pid ?? undefined }
  }

  // 读取游戏日志尾部若干行
  tail(root: string, maxLines: number): string[] {
    try {
      const text = fs.readFileSync(gameLogPath(root), 'utf8')
      const lines = text.split('\n')
      if (lines.length > 0 && lines[lines.length - 1] === '') lines.pop()
      return lines.slice(-Math.max(1, maxLines))
    } catch {
      return []
    }
  }
}
