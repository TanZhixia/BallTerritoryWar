// 与游戏 FormatValue 一致：1000 以上带 k / M / B 单位
export function formatValue(value: number): string {
  if (value >= 1e9) return `${Math.round(value / 1e9)}B`
  if (value >= 1e6) return `${Math.round(value / 1e6)}M`
  if (value >= 1e3) return `${Math.round(value / 1e3)}k`
  return String(Math.trunc(value))
}

export function formatClock(seconds: number): string {
  const total = Math.max(0, Math.floor(seconds))
  const m = Math.floor(total / 60)
  const s = total % 60
  return `${m}:${String(s).padStart(2, '0')}`
}

export function formatPct(fraction: number): string {
  return `${(fraction * 100).toFixed(1)}%`
}
