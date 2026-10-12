import { LineChart } from 'lucide-react'
import {
  Area,
  AreaChart,
  CartesianGrid,
  ResponsiveContainer,
  Tooltip as RTooltip,
  XAxis,
  YAxis,
} from 'recharts'

import { formatClock } from '../lib/format'
import { TEAM_COLORS, TEAM_NAMES } from '../lib/theme'
import { Panel } from './ui'

export interface TerritorySample {
  t: number
  v: number[] // 四队占比 0..100
}

export function TerritoryChart({ history }: { history: TerritorySample[] }) {
  return (
    <Panel
      title="领土曲线"
      icon={<LineChart size={14} />}
      right={<span className="text-xs text-[var(--color-faint)]">本局 {history.length} 采样</span>}
    >
      <div className="h-52 w-full">
        {history.length < 2 ? (
          <div className="flex h-full items-center justify-center text-sm text-[var(--color-mute)]">
            数据积累中…
          </div>
        ) : (
          <ResponsiveContainer width="100%" height="100%">
            <AreaChart data={history} margin={{ top: 6, right: 8, bottom: 0, left: -18 }}>
              <defs>
                {TEAM_NAMES.map((_, i) => (
                  <linearGradient key={i} id={`grad-${i}`} x1="0" y1="0" x2="0" y2="1">
                    <stop offset="0%" stopColor={TEAM_COLORS[i]} stopOpacity={0.5} />
                    <stop offset="100%" stopColor={TEAM_COLORS[i]} stopOpacity={0.02} />
                  </linearGradient>
                ))}
              </defs>
              <CartesianGrid stroke="var(--color-line)" strokeDasharray="3 3" vertical={false} />
              <XAxis
                dataKey="t"
                tickFormatter={(t: number) => formatClock(t)}
                stroke="var(--color-faint)"
                fontSize={11}
                minTickGap={40}
              />
              <YAxis
                domain={[0, 100]}
                tickFormatter={(v: number) => `${v}%`}
                stroke="var(--color-faint)"
                fontSize={11}
                width={44}
              />
              <RTooltip
                contentStyle={{
                  background: 'var(--color-surface-3)',
                  border: '1px solid var(--color-line-2)',
                  borderRadius: 10,
                  fontSize: 12,
                }}
                labelFormatter={(t) => formatClock(Number(t))}
                formatter={(value: number, name: string) => [`${value.toFixed(1)}%`, name]}
              />
              {TEAM_NAMES.map((name, i) => (
                <Area
                  key={name}
                  type="monotone"
                  dataKey={(d: TerritorySample) => d.v[i] ?? 0}
                  name={name}
                  stroke={TEAM_COLORS[i]}
                  strokeWidth={1.8}
                  fill={`url(#grad-${i})`}
                  isAnimationActive={false}
                  dot={false}
                />
              ))}
            </AreaChart>
          </ResponsiveContainer>
        )}
      </div>
    </Panel>
  )
}
