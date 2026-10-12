import { clsx, type ClassValue } from 'clsx'
import { twMerge } from 'tailwind-merge'

export function cn(...inputs: ClassValue[]): string {
  return twMerge(clsx(inputs))
}

// 与游戏显示层 NEW_FRAME_PALETTE 一致的四队配色
export const TEAM_COLORS = ['#ff9696', '#64ff64', '#b4d2ff', '#fff08c'] as const
export const TEAM_NAMES = ['红队', '绿队', '蓝队', '黄队'] as const
