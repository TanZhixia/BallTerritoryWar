import { Hammer, Rocket } from 'lucide-react'
import { useState } from 'react'

import type { EnvStatus, GameStatus } from '../lib/types'
import { Button, ConfirmDialog, Panel } from './ui'

export function LaunchPanel({
  env,
  game,
  output,
  onOutputChange,
  onLaunch,
  onBuild,
  launching,
  building,
}: {
  env: EnvStatus | null
  game: GameStatus | null
  output: string
  onOutputChange: (v: string) => void
  onLaunch: () => void
  onBuild: () => void
  launching: boolean
  building: boolean
}) {
  const [confirmOpen, setConfirmOpen] = useState(false)
  const canLaunch = env?.gameExists ?? false

  return (
    <Panel title="启动" icon={<Rocket size={14} />}>
      <label className="mb-1.5 block text-xs text-[var(--color-faint)]">录像输出文件名</label>
      <input
        value={output}
        onChange={(e) => onOutputChange(e.target.value)}
        placeholder="output.mp4"
        spellCheck={false}
        className="w-full rounded-lg border border-[var(--color-line-2)] bg-black/40 px-3 py-2 font-mono text-sm text-[var(--color-ink)] outline-none focus:border-[var(--color-accent)]"
      />
      <p className="mt-1.5 text-[11px] text-[var(--color-faint)]">
        缺扩展名会自动补 .mp4；录制 / 音乐 / 60fps / 武器升力恒定开启。
      </p>

      <div className="mt-3 rounded-lg border border-[var(--color-line)] bg-black/30 px-3 py-2 font-mono text-[11px] text-[var(--color-mute)]">
        build/main --output {output.trim() || 'output.mp4'}
      </div>

      <div className="mt-4 flex gap-2">
        <Button
          variant="primary"
          className="flex-1"
          disabled={!canLaunch || launching}
          onClick={() => setConfirmOpen(true)}
        >
          <Rocket size={15} /> 启动游戏
        </Button>
        <Button variant="outline" disabled={building} onClick={onBuild}>
          <Hammer size={15} /> {building ? '构建中…' : '构建'}
        </Button>
      </div>
      {!canLaunch && env && (
        <p className="mt-2 text-[11px] text-[var(--color-warn)]">
          尚未发现 build/main，请先点「构建」。
        </p>
      )}

      <ConfirmDialog
        open={confirmOpen}
        onOpenChange={setConfirmOpen}
        title="启动游戏？"
        description={
          <span>
            将以工作目录 <code className="font-mono text-xs">{env?.root}</code> 启动，输出重定向到{' '}
            <code className="font-mono text-xs">build/game.log</code>
            {game?.running ? '（当前已有游戏在运行）' : ''}。
          </span>
        }
        confirmLabel="启动"
        onConfirm={onLaunch}
      />
    </Panel>
  )
}
