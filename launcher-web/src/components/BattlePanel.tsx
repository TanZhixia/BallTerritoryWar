import { Activity } from 'lucide-react'

import type { TelemetryFrame } from '../lib/types'
import { formatClock, formatPct, formatValue } from '../lib/format'
import { TEAM_COLORS, TEAM_NAMES } from '../lib/theme'
import { Panel } from './ui'

export function BattlePanel({ frame }: { frame: TelemetryFrame | null }) {
  return (
    <Panel
      title="实时战况"
      icon={<Activity size={14} />}
      right={
        <span className="font-mono text-xs text-[var(--color-mute)]">
          {frame ? `FPS ${frame.fps.toFixed(1)}` : '—'}
        </span>
      }
    >
      {frame === null ? (
        <p className="py-6 text-center text-sm text-[var(--color-mute)]">
          游戏未运行 · 启动后这里会实时刷新
        </p>
      ) : (
        <>
          <div className="space-y-3">
            {TEAM_NAMES.map((name, i) => (
              <TeamRow key={name} index={i} name={name} frame={frame} />
            ))}
          </div>
          <div className="mt-4 grid grid-cols-4 gap-2 border-t border-[var(--color-line)] pt-3 text-center">
            <Stat label="总球数" value={frame.totalBalls.toLocaleString()} />
            <Stat label="物理球" value={String(frame.physicsCount)} />
            <Stat label="时间" value={formatClock(frame.elapsed)} />
            <Stat label="FPS" value={frame.fps.toFixed(1)} />
          </div>
        </>
      )}
    </Panel>
  )
}

function TeamRow({ index, name, frame }: { index: number; name: string; frame: TelemetryFrame }) {
  const color = TEAM_COLORS[index]
  const alive = frame.alive[index]
  const pct = frame.territory[index] ?? 0
  return (
    <div className="grid grid-cols-[4.5rem_1fr_3.5rem] items-center gap-3">
      <div className="flex items-center gap-2">
        <span className="h-2.5 w-2.5 shrink-0 rounded-full" style={{ background: color }} />
        <span
          className="text-sm font-medium"
          style={{ color: alive ? undefined : 'var(--color-faint)' }}
        >
          {name}
        </span>
      </div>

      <div className="relative h-6 overflow-hidden rounded-md border border-[var(--color-line)] bg-black/40">
        <div
          className="h-full rounded-[5px] transition-[width] duration-500 ease-out"
          style={{
            width: `${Math.max(0, Math.min(1, pct)) * 100}%`,
            background: alive
              ? `linear-gradient(90deg, ${color}55, ${color})`
              : 'transparent',
          }}
        />
        <div className="absolute inset-y-0 left-2 flex items-center gap-3 text-[11px] text-[var(--color-mute)]">
          {alive ? (
            <>
              <span>大球 {frame.bigBalls[index] ?? 0}</span>
              <span>护盾 {formatValue(frame.shields[index] ?? 0)}</span>
              <span>弹药 {formatValue(frame.ammo[index] ?? 0)}</span>
            </>
          ) : (
            <span className="text-[var(--color-err)]">已灭</span>
          )}
        </div>
      </div>

      <div
        className="text-right font-mono text-sm"
        style={{ color: alive ? color : 'var(--color-faint)' }}
      >
        {alive ? formatPct(pct) : '—'}
      </div>
    </div>
  )
}

function Stat({ label, value }: { label: string; value: string }) {
  return (
    <div>
      <div className="text-[11px] uppercase tracking-wider text-[var(--color-faint)]">{label}</div>
      <div className="font-mono text-lg text-[var(--color-ink)]">{value}</div>
    </div>
  )
}
