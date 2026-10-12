import { Activity, Boxes } from 'lucide-react'

import type { GameStatus } from '../lib/types'
import { cn } from '../lib/theme'

export function Header({
  connected,
  game,
}: {
  connected: boolean
  game: GameStatus | null
}) {
  return (
    <header className="panel flex items-center gap-4 px-4 py-3">
      <div className="grid h-11 w-11 shrink-0 grid-cols-3 grid-rows-3 gap-[3px] rounded-lg border border-[var(--color-line-2)] bg-black/40 p-[5px]">
        {['#ff9696', '#64ff64', '#b4d2ff', '#fff08c', '#e9edf7', '#b4d2ff', '#64ff64', '#ff9696', '#fff08c'].map(
          (c, i) => (
            <span key={i} className="rounded-[2px]" style={{ background: c }} />
          ),
        )}
      </div>

      <div className="min-w-0">
        <h1 className="truncate text-[15px] font-semibold tracking-wide">Ball Territory War · 控制台</h1>
        <p className="truncate text-xs text-[var(--color-mute)]">全自动四队对战 · 启动 / 监控 / 调参</p>
      </div>

      <div className="ml-auto flex items-center gap-2">
        <span
          className={cn(
            'chip',
            connected
              ? 'border-[var(--color-ok)]/50 text-[var(--color-ok)]'
              : 'border-[var(--color-line-2)] text-[var(--color-mute)]',
          )}
        >
          <Activity size={12} className={connected ? 'live-dot' : ''} />
          {connected ? 'LIVE' : 'OFFLINE'}
        </span>
        <GameBadge game={game} />
      </div>
    </header>
  )
}

function GameBadge({ game }: { game: GameStatus | null }) {
  if (!game) {
    return (
      <span className="chip">
        <Boxes size={12} /> 状态未知
      </span>
    )
  }
  if (game.running) {
    return (
      <span className="chip border-[var(--color-ok)]/50 text-[var(--color-ok)]">
        <Boxes size={12} className="live-dot" /> 运行中 · pid {game.pid ?? '-'}
      </span>
    )
  }
  return (
    <span className="chip">
      <Boxes size={12} /> 未运行
      {game.exitCode !== null && game.exitCode !== 0 ? `（上次退出码 ${game.exitCode}）` : ''}
    </span>
  )
}
