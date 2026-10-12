import * as SliderPrimitive from '@radix-ui/react-slider'
import * as Tabs from '@radix-ui/react-tabs'
import { RotateCcw, Save, Settings2, Upload, Undo2 } from 'lucide-react'
import { useEffect, useMemo, useState } from 'react'

import { postConfig, resetConfig } from '../lib/api'
import type { ApiConfig, ConfigGroup, ConfigItem } from '../lib/types'
import { cn } from '../lib/theme'
import { Button, ConfirmDialog, Panel } from './ui'

function keyOf(group: string, item: ConfigItem, index: number): string {
  return index >= 0 ? `${group}.${item.key}.${index}` : `${group}.${item.key}`
}

function defaultOf(item: ConfigItem, index: number): string {
  const i = index < 0 ? 0 : index
  return String(item.defaults[i])
}

export function ConfigEditor({
  config,
  reload,
  notify,
}: {
  config: ApiConfig | null
  reload: () => void
  notify: (msg: string, kind?: 'ok' | 'err' | 'info') => void
}) {
  const [draft, setDraft] = useState<Record<string, string>>({})
  const [busy, setBusy] = useState(false)
  const [resetOpen, setResetOpen] = useState(false)

  // 每次加载/重读配置时，用「文件值 ?? 默认值」初始化草稿
  useEffect(() => {
    if (!config) return
    const next: Record<string, string> = {}
    for (const group of config.schema) {
      for (const item of group.items) {
        if (item.type === 'array') {
          for (let i = 0; i < item.count; i++) {
            const k = keyOf(group.group, item, i)
            next[k] = config.values[k] ?? defaultOf(item, i)
          }
        } else {
          const k = keyOf(group.group, item, -1)
          next[k] = config.values[k] ?? defaultOf(item, -1)
        }
      }
    }
    setDraft(next)
  }, [config])

  const dirtyKeys = useMemo(() => {
    if (!config) return []
    const keys: string[] = []
    for (const group of config.schema) {
      for (const item of group.items) {
        const indices = item.type === 'array' ? Array.from({ length: item.count }, (_, i) => i) : [-1]
        for (const idx of indices) {
          const k = keyOf(group.group, item, idx)
          const original = config.values[k] ?? defaultOf(item, idx)
          if ((draft[k] ?? '') !== original) keys.push(k)
        }
      }
    }
    return keys
  }, [config, draft])

  const set = (key: string, value: string) => setDraft((d) => ({ ...d, [key]: value }))

  const send = async (mode: 'save' | 'apply' | 'both') => {
    if (dirtyKeys.length === 0) {
      notify('没有改动', 'info')
      return
    }
    setBusy(true)
    try {
      const updates = dirtyKeys.map((key) => ({ key, value: draft[key] ?? '' }))
      const result = await postConfig(updates, mode)
      if (result.errors.length) notify(result.errors.join('；'), 'err')
      const parts: string[] = []
      if (result.written) {
        parts.push(
          `已写入 ${result.written.count} 项${
            result.written.appended.length ? `（补写 ${result.written.appended.length} 项）` : ''
          }`,
        )
      }
      if (result.applied) {
        if (result.applied.failures.length) parts.push(`热更新失败 ${result.applied.failures.length} 项`)
        else if (result.applied.connected) parts.push(`已实时应用 ${result.applied.count} 项`)
        else parts.push('游戏未运行：改动将在下次启动生效')
      }
      if (parts.length) notify(parts.join('；'), result.errors.length ? 'err' : 'ok')
      reload()
    } catch (err) {
      notify((err as Error).message, 'err')
    } finally {
      setBusy(false)
    }
  }

  const doReset = async () => {
    setBusy(true)
    try {
      const r = await resetConfig()
      notify(`已恢复默认并写入 ${r.count} 项${r.failures.length ? `，热更新失败 ${r.failures.length} 项` : ''}`, 'ok')
      reload()
    } catch (err) {
      notify((err as Error).message, 'err')
    } finally {
      setBusy(false)
    }
  }

  if (!config) {
    return (
      <Panel title="配置" icon={<Settings2 size={14} />}>
        <p className="text-sm text-[var(--color-mute)]">读取中…</p>
      </Panel>
    )
  }

  return (
    <Panel
      title="配置"
      icon={<Settings2 size={14} />}
      right={
        <div className="flex items-center gap-2">
          <span
            className={cn(
              'text-xs',
              dirtyKeys.length ? 'text-[var(--color-warn)]' : 'text-[var(--color-faint)]',
            )}
          >
            {dirtyKeys.length ? `${dirtyKeys.length} 项未保存` : '无改动'}
          </span>
          <Button variant="ghost" onClick={reload} disabled={busy} title="从磁盘重读">
            <Undo2 size={15} /> 重读
          </Button>
          <Button variant="danger" onClick={() => setResetOpen(true)} disabled={busy}>
            <RotateCcw size={15} /> 恢复默认
          </Button>
        </div>
      }
    >
      <Tabs.Root defaultValue={config.schema[0]?.group}>
        <Tabs.List className="mb-4 flex flex-wrap gap-1 rounded-xl border border-[var(--color-line)] bg-black/30 p-1">
          {config.schema.map((g) => (
            <Tabs.Trigger
              key={g.group}
              value={g.group}
              className="rounded-lg px-3 py-1.5 text-xs font-medium text-[var(--color-mute)] transition-colors data-[state=active]:bg-white/10 data-[state=active]:text-[var(--color-ink)]"
            >
              {g.label}
            </Tabs.Trigger>
          ))}
        </Tabs.List>

        {config.schema.map((g) => (
          <Tabs.Content key={g.group} value={g.group}>
            <GroupEditor group={g} draft={draft} original={config.values} onChange={set} />
          </Tabs.Content>
        ))}
      </Tabs.Root>

      <div className="mt-4 flex flex-wrap items-center gap-2 border-t border-[var(--color-line)] pt-4">
        <Button variant="primary" onClick={() => send('both')} disabled={busy || dirtyKeys.length === 0}>
          <Save size={15} /> 保存并热更新
        </Button>
        <Button variant="outline" onClick={() => send('apply')} disabled={busy || dirtyKeys.length === 0}>
          <Upload size={15} /> 仅热更新
        </Button>
        <span className="ml-auto font-mono text-[11px] text-[var(--color-faint)]">{config.configPath}</span>
      </div>

      <ConfirmDialog
        open={resetOpen}
        onOpenChange={setResetOpen}
        title="恢复全部默认值？"
        description="会把 56 项配置全部写回默认值（覆盖当前文件，游戏在跑则同时热更新）。"
        confirmLabel="恢复默认"
        variant="danger"
        onConfirm={doReset}
      />
    </Panel>
  )
}

function GroupEditor({
  group,
  draft,
  original,
  onChange,
}: {
  group: ConfigGroup
  draft: Record<string, string>
  original: Record<string, string>
  onChange: (key: string, value: string) => void
}) {
  return (
    <div className="divide-y divide-[var(--color-line)]/70">
      {group.items.map((item) => (
        <div key={item.key} className="flex flex-col gap-2 py-3 sm:flex-row sm:gap-4">
          <div className="sm:w-56 sm:shrink-0">
            <div className="flex items-center gap-2 text-sm">
              <span>{item.label}</span>
              {item.restartOnly && (
                <span className="rounded border border-[var(--color-warn)]/40 px-1.5 text-[10px] text-[var(--color-warn)]">
                  重启
                </span>
              )}
            </div>
            <div className="mt-0.5 text-[11px] leading-snug text-[var(--color-faint)]">{item.desc}</div>
          </div>

          <div className="flex-1">
            {item.type === 'array' ? (
              <div className="grid grid-cols-3 gap-2">
                {['*8', '*4', '*2'].map((label, i) => (
                  <Field
                    key={i}
                    label={label}
                    item={item}
                    index={i}
                    value={draft[keyOf(group.group, item, i)] ?? ''}
                    original={original}
                    group={group.group}
                    onChange={onChange}
                  />
                ))}
              </div>
            ) : (
              <Field
                label={null}
                item={item}
                index={-1}
                value={draft[keyOf(group.group, item, -1)] ?? ''}
                original={original}
                group={group.group}
                onChange={onChange}
              />
            )}
          </div>
        </div>
      ))}
    </div>
  )
}

function Field({
  label,
  item,
  index,
  value,
  original,
  group,
  onChange,
}: {
  label: string | null
  item: ConfigItem
  index: number
  value: string
  original: Record<string, string>
  group: string
  onChange: (key: string, value: string) => void
}) {
  const key = keyOf(group, item, index)
  const def = defaultOf(item, index)
  const dirty = value !== (original[key] ?? def)
  const num = Number.parseFloat(value)
  const showSlider = item.max - item.min <= 2000

  return (
    <div>
      <div className="flex items-center gap-2">
        {label && <span className="w-6 shrink-0 text-[11px] text-[var(--color-faint)]">{label}</span>}
        <input
          value={value}
          onChange={(e) => onChange(key, e.target.value)}
          inputMode="decimal"
          spellCheck={false}
          className={cn(
            'w-32 rounded-lg border bg-black/40 px-2.5 py-1.5 text-right font-mono text-sm outline-none focus:border-[var(--color-accent)]',
            dirty ? 'border-[var(--color-warn)]/60' : 'border-[var(--color-line-2)]',
          )}
        />
        <button
          onClick={() => onChange(key, def)}
          title={`恢复默认（${def}）`}
          className="rounded-md p-1 text-[var(--color-faint)] hover:bg-white/5 hover:text-[var(--color-ink)]"
        >
          <Undo2 size={13} />
        </button>
        <span className="text-[11px] text-[var(--color-faint)]">
          {item.min} – {item.max}
        </span>
      </div>

      {showSlider && (
        <SliderPrimitive.Root
          className="relative mt-2 flex h-4 w-full max-w-72 touch-none items-center"
          min={item.min}
          max={item.max}
          step={item.type === 'int' ? 1 : (item.max - item.min) / 1000}
          value={[Number.isFinite(num) ? Math.min(item.max, Math.max(item.min, num)) : item.min]}
          onValueChange={([v]) => onChange(key, item.type === 'int' ? String(Math.round(v)) : String(v))}
        >
          <SliderPrimitive.Track className="relative h-1 w-full grow rounded-full bg-[var(--color-line-2)]">
            <SliderPrimitive.Range className="absolute h-full rounded-full bg-[var(--color-accent)]" />
          </SliderPrimitive.Track>
          <SliderPrimitive.Thumb className="block h-3.5 w-3.5 rounded-full border-2 border-[var(--color-accent)] bg-white outline-none focus:ring-2 focus:ring-[var(--color-accent)]/40" />
        </SliderPrimitive.Root>
      )}
    </div>
  )
}
