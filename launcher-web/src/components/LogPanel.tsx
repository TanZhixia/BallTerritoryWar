import { Terminal } from 'lucide-react'
import * as Tabs from '@radix-ui/react-tabs'
import { useEffect, useRef, type ReactNode } from 'react'

import { Panel } from './ui'

export function LogPanel({
  gameLines,
  buildLog,
  running,
}: {
  gameLines: string[]
  buildLog: string
  running: boolean
}) {
  return (
    <Panel
      title="日志"
      icon={<Terminal size={14} />}
      right={running ? <span className="text-xs text-[var(--color-ok)]">游戏运行中</span> : null}
    >
      <Tabs.Root defaultValue="game">
        <Tabs.List className="mb-3 flex gap-1 rounded-lg border border-[var(--color-line)] bg-black/30 p-1">
          <Tab value="game">游戏日志</Tab>
          <Tab value="build">构建输出</Tab>
        </Tabs.List>

        <Tabs.Content value="game">
          <Console lines={gameLines} empty="（无输出）" />
        </Tabs.Content>
        <Tabs.Content value="build">
          <Console text={buildLog} empty="（尚未构建）" />
        </Tabs.Content>
      </Tabs.Root>
    </Panel>
  )
}

function Tab({ value, children }: { value: string; children: ReactNode }) {
  return (
    <Tabs.Trigger
      value={value}
      className="rounded-md px-3 py-1.5 text-xs font-medium text-[var(--color-mute)] transition-colors data-[state=active]:bg-white/10 data-[state=active]:text-[var(--color-ink)]"
    >
      {children}
    </Tabs.Trigger>
  )
}

function Console({
  lines,
  text,
  empty,
}: {
  lines?: string[]
  text?: string
  empty: string
}) {
  const ref = useRef<HTMLPreElement>(null)
  const content = text !== undefined ? text : (lines ?? []).join('\n')

  useEffect(() => {
    const el = ref.current
    if (el) el.scrollTop = el.scrollHeight
  }, [content])

  return (
    <pre
      ref={ref}
      className="h-44 overflow-auto rounded-lg border border-[var(--color-line)] bg-black/40 p-3 font-mono text-[11.5px] leading-relaxed text-[var(--color-mute)]"
    >
      {content.trim() === '' ? empty : content}
    </pre>
  )
}
