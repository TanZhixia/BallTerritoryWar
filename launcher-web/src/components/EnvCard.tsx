import { CheckCircle2, FolderOpen, XCircle } from 'lucide-react'

import type { EnvStatus } from '../lib/types'
import { Panel } from './ui'

export function EnvCard({ env }: { env: EnvStatus | null }) {
  return (
    <Panel title="环境" icon={<FolderOpen size={14} />}>
      {env === null ? (
        <p className="text-sm text-[var(--color-mute)]">读取中…</p>
      ) : (
        <dl className="space-y-2 text-sm">
          <Row label="项目" value={env.root} mono />
          <Row
            label="游戏程序"
            value={env.gameExists ? '已构建' : '未构建，请先构建'}
            ok={env.gameExists}
            mono={!env.gameExists}
            title={env.gamePath}
          />
          <Row label="ffmpeg" value={env.ffmpeg ? '可用' : '不可用（录像跳过）'} ok={env.ffmpeg} />
          <Row label="背景音乐" value={`${env.musicCount} 首`} title={env.musicDir} />
          <Row
            label="游戏配置"
            value={env.configExists ? '已存在' : '未生成（首次运行创建）'}
            ok={env.configExists}
            title={env.configPath}
          />
        </dl>
      )}
    </Panel>
  )
}

function Row({
  label,
  value,
  ok,
  mono,
  title,
}: {
  label: string
  value: string
  ok?: boolean
  mono?: boolean
  title?: string
}) {
  return (
    <div className="flex items-baseline justify-between gap-3">
      <dt className="shrink-0 text-[var(--color-faint)]">{label}</dt>
      <dd
        className={`flex min-w-0 items-center gap-1.5 text-right ${mono ? 'font-mono text-xs' : ''}`}
        title={title}
      >
        {ok !== undefined &&
          (ok ? (
            <CheckCircle2 size={13} className="shrink-0 text-[var(--color-ok)]" />
          ) : (
            <XCircle size={13} className="shrink-0 text-[var(--color-err)]" />
          ))}
        <span className="truncate" style={{ color: ok === false ? 'var(--color-warn)' : undefined }}>
          {value}
        </span>
      </dd>
    </div>
  )
}
