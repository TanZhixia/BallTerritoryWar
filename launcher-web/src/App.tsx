import { useCallback, useEffect, useRef, useState } from 'react'

import { BattlePanel } from './components/BattlePanel'
import { ConfigEditor } from './components/ConfigEditor'
import { EnvCard } from './components/EnvCard'
import { Header } from './components/Header'
import { LaunchPanel } from './components/LaunchPanel'
import { LogPanel } from './components/LogPanel'
import { TerritoryChart, type TerritorySample } from './components/TerritoryChart'
import {
  getConfig,
  getEnv,
  getGameStatus,
  getLaunchOptions,
  launchGame,
  putLaunchOptions,
  runBuild,
  subscribeLogs,
  subscribeTelemetry,
} from './lib/api'
import type { ApiConfig, EnvStatus, GameStatus, TelemetryFrame } from './lib/types'
import { cn } from './lib/theme'

interface Toast {
  msg: string
  kind: 'ok' | 'err' | 'info'
}

export default function App() {
  const [env, setEnv] = useState<EnvStatus | null>(null)
  const [config, setConfig] = useState<ApiConfig | null>(null)
  const [frame, setFrame] = useState<TelemetryFrame | null>(null)
  const [connected, setConnected] = useState(false)
  const [game, setGame] = useState<GameStatus | null>(null)
  const [output, setOutput] = useState('output.mp4')
  const [history, setHistory] = useState<TerritorySample[]>([])
  const [gameLines, setGameLines] = useState<string[]>([])
  const [buildLog, setBuildLog] = useState('')
  const [building, setBuilding] = useState(false)
  const [launching, setLaunching] = useState(false)
  const [toast, setToast] = useState<Toast | null>(null)

  const pushTimer = useRef(0)
  const lastElapsed = useRef(0)
  const outputSaveTimer = useRef<ReturnType<typeof setTimeout> | null>(null)
  const toastTimer = useRef<ReturnType<typeof setTimeout> | null>(null)

  const notify = useCallback((msg: string, kind: Toast['kind'] = 'info') => {
    setToast({ msg, kind })
    if (toastTimer.current) clearTimeout(toastTimer.current)
    toastTimer.current = setTimeout(() => setToast(null), 4200)
  }, [])

  const reloadEnv = useCallback(() => {
    getEnv().then(setEnv).catch(() => {})
  }, [])
  const reloadConfig = useCallback(() => {
    getConfig().then(setConfig).catch((e) => notify((e as Error).message, 'err'))
  }, [notify])

  useEffect(() => {
    reloadEnv()
    reloadConfig()
    getLaunchOptions().then((o) => setOutput(o.output)).catch(() => {})
    getGameStatus().then(setGame).catch(() => {})
  }, [reloadEnv, reloadConfig])

  // 遥测：实时帧 + 连接状态
  useEffect(() => {
    return subscribeTelemetry({
      onStatus: setConnected,
      onFrame: (f) => {
        setFrame(f)
        const now = Date.now()
        if (now - pushTimer.current < 400) return
        pushTimer.current = now
        setHistory((h) => {
          const point: TerritorySample = { t: f.elapsed, v: f.territory.map((x) => x * 100) }
          if (f.elapsed < lastElapsed.current) {
            lastElapsed.current = f.elapsed
            return [point] // 新一局：曲线重来
          }
          lastElapsed.current = f.elapsed
          return [...h, point] // 全局采样：保留本局全部采样点，不设数量上限
        })
      },
    })
  }, [])

  // 游戏日志
  useEffect(() => subscribeLogs(setGameLines), [])

  // 周期刷新游戏运行状态
  useEffect(() => {
    const id = setInterval(() => {
      getGameStatus()
        .then((s) => {
          setGame((prev) => {
            if (prev?.running && !s.running && s.exitCode !== null && s.exitCode !== 0) {
              notify(`游戏已退出（退出码 ${s.exitCode}）`, 'err')
            }
            return s
          })
        })
        .catch(() => {})
    }, 2000)
    return () => clearInterval(id)
  }, [notify])

  const handleOutput = (v: string) => {
    setOutput(v)
    if (outputSaveTimer.current) clearTimeout(outputSaveTimer.current)
    outputSaveTimer.current = setTimeout(() => {
      putLaunchOptions(v).catch(() => {})
    }, 500)
  }

  const handleLaunch = async () => {
    setLaunching(true)
    try {
      const r = await launchGame(output)
      notify(`游戏已启动（pid ${r.pid ?? '-'}）`, 'ok')
      getGameStatus().then(setGame).catch(() => {})
    } catch (err) {
      notify((err as Error).message, 'err')
    } finally {
      setLaunching(false)
    }
  }

  const handleBuild = async () => {
    setBuilding(true)
    setBuildLog('')
    try {
      await runBuild((chunk) => setBuildLog((prev) => prev + chunk))
      notify('构建完成', 'ok')
      reloadEnv()
    } catch (err) {
      notify((err as Error).message, 'err')
    } finally {
      setBuilding(false)
    }
  }

  return (
    <div className="mx-auto max-w-[1440px] space-y-4 p-4 lg:p-6">
      <Header connected={connected} game={game} />

      <div className="grid gap-4 lg:grid-cols-[1.5fr_1fr]">
        <div className="space-y-4">
          <BattlePanel frame={frame} />
          <TerritoryChart history={history} />
        </div>
        <div className="space-y-4">
          <EnvCard env={env} />
          <LaunchPanel
            env={env}
            game={game}
            output={output}
            onOutputChange={handleOutput}
            onLaunch={handleLaunch}
            onBuild={handleBuild}
            launching={launching}
            building={building}
          />
        </div>
      </div>

      <ConfigEditor config={config} reload={reloadConfig} notify={notify} />

      <LogPanel gameLines={gameLines} buildLog={buildLog} running={game?.running ?? false} />

      {toast && (
        <div
          className={cn(
            'fixed bottom-5 right-5 z-50 max-w-sm rounded-xl border px-4 py-3 text-sm shadow-2xl',
            toast.kind === 'ok' && 'border-[var(--color-ok)]/50 bg-[var(--color-ok)]/10 text-[var(--color-ok)]',
            toast.kind === 'err' && 'border-[var(--color-err)]/50 bg-[var(--color-err)]/10 text-[var(--color-err)]',
            toast.kind === 'info' &&
              'border-[var(--color-line-2)] bg-[var(--color-surface-3)] text-[var(--color-ink)]',
          )}
        >
          {toast.msg}
        </div>
      )}
    </div>
  )
}
