import * as Dialog from '@radix-ui/react-dialog'
import * as SwitchPrimitive from '@radix-ui/react-switch'
import * as TooltipPrimitive from '@radix-ui/react-tooltip'
import { X } from 'lucide-react'
import type { ButtonHTMLAttributes, ReactNode } from 'react'

import { cn } from '../lib/theme'

type Variant = 'primary' | 'outline' | 'ghost' | 'danger'

const VARIANTS: Record<Variant, string> = {
  primary:
    'bg-[var(--color-accent)] text-[#06111f] hover:bg-[#8bbcff] border border-transparent font-semibold',
  outline:
    'border border-[var(--color-line-2)] text-[var(--color-ink)] hover:bg-white/5 bg-white/[0.02]',
  ghost: 'border border-transparent text-[var(--color-mute)] hover:text-[var(--color-ink)] hover:bg-white/5',
  danger:
    'border border-[#5a2630] text-[var(--color-err)] hover:bg-[#ff6b6b]/10 bg-[#ff6b6b]/[0.06]',
}

export function Button({
  variant = 'outline',
  className,
  ...props
}: ButtonHTMLAttributes<HTMLButtonElement> & { variant?: Variant }) {
  return (
    <button
      className={cn(
        'inline-flex items-center justify-center gap-2 rounded-lg px-3.5 py-2 text-sm transition-colors',
        'disabled:cursor-not-allowed disabled:opacity-45',
        VARIANTS[variant],
        className,
      )}
      {...props}
    />
  )
}

export function Panel({
  icon,
  title,
  right,
  children,
  className,
  bodyClassName,
}: {
  icon?: ReactNode
  title?: ReactNode
  right?: ReactNode
  children: ReactNode
  className?: string
  bodyClassName?: string
}) {
  return (
    <section className={cn('panel p-4', className)}>
      {(title || right) && (
        <div className="mb-3 flex items-center justify-between gap-3">
          <div className="panel-title">
            {icon}
            {title}
          </div>
          {right}
        </div>
      )}
      <div className={bodyClassName}>{children}</div>
    </section>
  )
}

export function Switch({
  checked,
  onCheckedChange,
}: {
  checked: boolean
  onCheckedChange: (v: boolean) => void
}) {
  return (
    <SwitchPrimitive.Root
      checked={checked}
      onCheckedChange={onCheckedChange}
      className={cn(
        'relative h-5 w-9 shrink-0 rounded-full border transition-colors',
        checked ? 'border-transparent bg-[var(--color-accent)]' : 'border-[var(--color-line-2)] bg-white/5',
      )}
    >
      <SwitchPrimitive.Thumb
        className={cn(
          'block h-3.5 w-3.5 translate-x-1 rounded-full bg-white transition-transform',
          checked && 'translate-x-[18px]',
        )}
      />
    </SwitchPrimitive.Root>
  )
}

export function Tip({ label, children }: { label: string; children: ReactNode }) {
  return (
    <TooltipPrimitive.Provider delayDuration={200}>
      <TooltipPrimitive.Root>
        <TooltipPrimitive.Trigger asChild>{children}</TooltipPrimitive.Trigger>
        <TooltipPrimitive.Portal>
          <TooltipPrimitive.Content
            sideOffset={6}
            className="z-50 max-w-xs rounded-lg border border-[var(--color-line-2)] bg-[var(--color-surface-3)] px-2.5 py-1.5 text-xs text-[var(--color-ink)] shadow-xl"
          >
            {label}
            <TooltipPrimitive.Arrow className="fill-[var(--color-surface-3)]" />
          </TooltipPrimitive.Content>
        </TooltipPrimitive.Portal>
      </TooltipPrimitive.Root>
    </TooltipPrimitive.Provider>
  )
}

export function ConfirmDialog({
  open,
  onOpenChange,
  title,
  description,
  confirmLabel = '确认',
  variant = 'primary',
  onConfirm,
}: {
  open: boolean
  onOpenChange: (v: boolean) => void
  title: string
  description: ReactNode
  confirmLabel?: string
  variant?: Variant
  onConfirm: () => void
}) {
  return (
    <Dialog.Root open={open} onOpenChange={onOpenChange}>
      <Dialog.Portal>
        <Dialog.Overlay className="fixed inset-0 z-40 bg-black/60 backdrop-blur-sm" />
        <Dialog.Content className="fixed left-1/2 top-1/2 z-50 w-[min(92vw,420px)] -translate-x-1/2 -translate-y-1/2 rounded-2xl border border-[var(--color-line)] bg-[var(--color-surface-2)] p-5 shadow-2xl">
          <div className="mb-1 flex items-center justify-between">
            <Dialog.Title className="text-base font-semibold">{title}</Dialog.Title>
            <Dialog.Close asChild>
              <button className="rounded-md p-1 text-[var(--color-mute)] hover:bg-white/5">
                <X size={16} />
              </button>
            </Dialog.Close>
          </div>
          <Dialog.Description asChild>
            <div className="mb-5 text-sm leading-relaxed text-[var(--color-mute)]">{description}</div>
          </Dialog.Description>
          <div className="flex justify-end gap-2">
            <Dialog.Close asChild>
              <Button variant="ghost">取消</Button>
            </Dialog.Close>
            <Button
              variant={variant}
              onClick={() => {
                onOpenChange(false)
                onConfirm()
              }}
            >
              {confirmLabel}
            </Button>
          </div>
        </Dialog.Content>
      </Dialog.Portal>
    </Dialog.Root>
  )
}
