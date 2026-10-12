// ==================== ~/.ball/config.yaml 读写（行级改写，保留注释） ====================
// 逐函数移植自 launcher/src/config.cpp，行为必须与旧启动器一致：
//   * 读取：扫描每行的 dotted key 与值区间
//   * 写入：值区间拼接（前缀 + 新值 + 后缀），保留缩进/行尾注释/顺序
//   * 缺失键补写进所属分组，分组不存在则文件末尾新建（标注「启动器补充」）

import fs from 'node:fs'
import path from 'node:path'

import { configSchema, type ConfigItem } from './schema'

// ---------- 路径 ----------

function homeDir(): string {
  return process.env.HOME ?? ''
}

export function configFilePath(): string {
  const home = homeDir()
  return home ? path.join(home, '.ball', 'config.yaml') : path.join('.ball', 'config.yaml')
}

export function launcherConfPath(): string {
  const home = homeDir()
  return home ? path.join(home, '.ball', 'launcher.conf') : path.join('.ball', 'launcher.conf')
}

// ---------- 基础工具 ----------

function isSpace(c: string): boolean {
  return c === ' ' || c === '\t' || c === '\n' || c === '\v' || c === '\f' || c === '\r'
}

export function trim(s: string): string {
  let b = 0
  let e = s.length
  while (b < e && isSpace(s[b])) b++
  while (e > b && isSpace(s[e - 1])) e--
  return s.slice(b, e)
}

function stripComment(s: string): string {
  let out = ''
  let quote = ''
  for (const c of s) {
    if (quote) {
      out += c
      if (c === quote) quote = ''
    } else if (c === "'" || c === '"') {
      quote = c
      out += c
    } else if (c === '#') {
      break
    } else {
      out += c
    }
  }
  return out
}

function indentOf(line: string): number {
  let n = 0
  for (const c of line) {
    if (c === ' ') n++
    else break
  }
  return n
}

function unquote(s: string): string {
  if (s.length >= 2 && s[0] === s[s.length - 1] && (s[0] === "'" || s[0] === '"')) {
    return s.slice(1, -1)
  }
  return s
}

function splitLines(text: string): string[] {
  const lines: string[] = []
  let cur = ''
  for (const ch of text) {
    if (ch === '\n') {
      lines.push(cur)
      cur = ''
    } else if (ch !== '\r') {
      cur += ch
    }
  }
  if (cur !== '') lines.push(cur)
  return lines
}

// 与 C++ FormatNumber 对齐：整数用十进制，否则 6 位有效数字（%g 风格）
export function formatNumber(value: number): string {
  if (Number.isInteger(value)) return String(value)
  return String(Number(value.toPrecision(6)))
}

function readFileText(file: string): string | null {
  try {
    return fs.readFileSync(file, 'utf8')
  } catch {
    return null
  }
}

// ---------- 扫描 ----------

interface LineInfo {
  hasKey: boolean
  key: string
  valueBegin: number
  valueEnd: number
  valuePresent: boolean
}

function valueSpan(raw: string, from: number): { begin: number; end: number; present: boolean } {
  let begin = from
  while (begin < raw.length && raw[begin] === ' ') begin++
  let end = raw.indexOf('#', begin)
  if (end === -1) end = raw.length
  while (end > begin && isSpace(raw[end - 1])) end--
  return { begin, end, present: end > begin }
}

function scanYaml(lines: string[]): LineInfo[] {
  const out: LineInfo[] = []
  let group = ''
  let arrayKey = ''
  let arrayIndex = 0

  for (const raw of lines) {
    const info: LineInfo = { hasKey: false, key: '', valueBegin: 0, valueEnd: 0, valuePresent: false }
    const content = trim(stripComment(raw))
    if (content === '') {
      out.push(info)
      continue
    }
    const indent = indentOf(raw)
    const colon = raw.indexOf(':')
    const dash = content.startsWith('- ') ? raw.indexOf('-') : -1

    if (indent === 0) {
      arrayKey = ''
      arrayIndex = 0
      if (content.endsWith(':')) {
        group = trim(content.slice(0, -1))
      } else if (colon !== -1) {
        info.hasKey = true
        info.key = trim(raw.slice(0, colon))
        group = ''
        const span = valueSpan(raw, colon + 1)
        info.valueBegin = span.begin
        info.valueEnd = span.end
        info.valuePresent = span.present
      }
    } else if (dash !== -1 && arrayKey !== '') {
      info.hasKey = true
      info.key = `${group}.${arrayKey}.${arrayIndex++}`
      const span = valueSpan(raw, dash + 1)
      info.valueBegin = span.begin
      info.valueEnd = span.end
      info.valuePresent = span.present
    } else if (content.endsWith(':')) {
      arrayKey = trim(content.slice(0, -1))
      arrayIndex = 0
    } else if (colon !== -1) {
      arrayKey = ''
      const key = trim(raw.slice(0, colon))
      info.hasKey = true
      info.key = group === '' ? key : `${group}.${key}`
      const span = valueSpan(raw, colon + 1)
      info.valueBegin = span.begin
      info.valueEnd = span.end
      info.valuePresent = span.present
    }
    out.push(info)
  }
  return out
}

// ---------- 读取 ----------

export interface LoadResult {
  values: Map<string, string>
  error: string
}

export function loadConfigFile(): LoadResult {
  const values = new Map<string, string>()
  const file = configFilePath()
  const text = readFileText(file)
  if (text === null) {
    return { values, error: `配置文件不存在：${file}` }
  }
  const lines = splitLines(text)
  const infos = scanYaml(lines)
  for (let i = 0; i < lines.length; i++) {
    const info = infos[i]
    if (!info.hasKey || !info.valuePresent) continue
    values.set(info.key, unquote(trim(lines[i].slice(info.valueBegin, info.valueEnd))))
  }
  return { values, error: '' }
}

// ---------- 默认 YAML ----------

function itemComment(group: string, key: string, index: number): string {
  for (const g of configSchema()) {
    if (g.group !== group) continue
    for (const item of g.items) {
      if (item.key !== key) continue
      if (index >= 0) {
        const labels = ['*8', '*4', '*2']
        return labels[index < 3 ? index : 0]
      }
      return item.desc === '' ? item.label : `${item.label}：${item.desc}`
    }
  }
  return '新增配置项'
}

export function defaultValueString(item: ConfigItem, index: number): string {
  const i = index < 0 ? 0 : index
  return formatNumber(item.defaults[i])
}

export function generateDefaultYaml(): string {
  let out = ''
  out += '# Ball Territory War 配置文件（由 CLI 启动器生成）\n'
  out += '# 修改后重启游戏生效；删除本文件可恢复默认配置。\n'
  out += '# 语法：key: value、缩进嵌套、- 列表、# 注释；不要用制表符缩进。\n\n'
  for (const group of configSchema()) {
    out += `${group.group}:  # ${group.label}\n`
    for (const item of group.items) {
      const comment = item.desc === '' ? item.label : `${item.label}：${item.desc}`
      if (item.type === 'array') {
        out += `  ${item.key}:  # ${comment}\n`
        const labels = ['*8', '*4', '*2']
        for (let i = 0; i < item.count; i++) {
          out += `    - ${defaultValueString(item, i)}  # ${labels[i]}\n`
        }
      } else {
        out += `  ${item.key}: ${defaultValueString(item, -1)}  # ${comment}${
          item.restartOnly ? '（下次启动生效）' : ''
        }\n`
      }
    }
    out += '\n'
  }
  return out
}

// ---------- 写入（行级改写） ----------

function findGroupRange(
  lines: string[],
  group: string,
): { begin: number; end: number } | null {
  let begin = -1
  for (let i = 0; i < lines.length; i++) {
    const content = trim(stripComment(lines[i]))
    if (content === '' || indentOf(lines[i]) !== 0) continue
    if (begin >= 0) return { begin, end: i }
    if (content.endsWith(':') && trim(content.slice(0, -1)) === group) {
      begin = i
    }
  }
  if (begin >= 0) return { begin, end: lines.length }
  return null
}

function appendMissingKeys(lines: string[], pending: Map<string, string>): void {
  const keys = [...pending.keys()].sort()
  for (const dotted of keys) {
    const firstDot = dotted.indexOf('.')
    if (firstDot === -1) continue
    const group = dotted.slice(0, firstDot)
    let rest = dotted.slice(firstDot + 1)
    let index = -1
    const secondDot = rest.lastIndexOf('.')
    if (secondDot !== -1) {
      const parsed = Number.parseInt(rest.slice(secondDot + 1), 10)
      index = Number.isNaN(parsed) ? 0 : parsed
      rest = rest.slice(0, secondDot)
    }

    const range = findGroupRange(lines, group)
    if (range === null) continue // 分组不存在，留到后面新建

    let insertAt = range.end
    while (insertAt > range.begin + 1 && trim(lines[insertAt - 1]) === '') insertAt--

    const value = pending.get(dotted) as string
    if (index < 0) {
      lines.splice(insertAt, 0, `  ${rest}: ${value}  # ${itemComment(group, rest, -1)}`)
    } else {
      let arrayLine = lines.length
      for (let i = range.begin + 1; i < range.end && i < lines.length; i++) {
        if (trim(stripComment(lines[i])) === `${rest}:`) {
          arrayLine = i
          break
        }
      }
      const element = `    - ${value}  # ${itemComment(group, rest, index)}`
      if (arrayLine === lines.length) {
        lines.splice(insertAt, 0, `  ${rest}:`)
        lines.splice(insertAt + 1, 0, element)
      } else {
        let lastItem = arrayLine
        for (let i = arrayLine + 1; i < range.end && i < lines.length; i++) {
          const content = trim(stripComment(lines[i]))
          if (content === '') continue
          if (content === '-' || content.startsWith('- ')) lastItem = i
          else break
        }
        lines.splice(lastItem + 1, 0, element)
      }
    }
    pending.delete(dotted)
  }

  if (pending.size === 0) return

  // 剩余的键所属分组不存在：在文件末尾新建分组
  const order: string[] = []
  const blocks = new Map<string, string[]>()
  const arrayHeader = new Map<string, Set<string>>()
  for (const dotted of [...pending.keys()].sort()) {
    const firstDot = dotted.indexOf('.')
    const group = firstDot === -1 ? dotted : dotted.slice(0, firstDot)
    let rest = firstDot === -1 ? dotted : dotted.slice(firstDot + 1)
    let index = -1
    const secondDot = rest.lastIndexOf('.')
    if (secondDot !== -1) {
      const parsed = Number.parseInt(rest.slice(secondDot + 1), 10)
      index = Number.isNaN(parsed) ? 0 : parsed
      rest = rest.slice(0, secondDot)
    }
    if (!blocks.has(group)) {
      order.push(group)
      blocks.set(group, [])
      arrayHeader.set(group, new Set())
    }
    const block = blocks.get(group) as string[]
    const value = pending.get(dotted) as string
    if (index < 0) {
      block.push(`  ${rest}: ${value}  # ${itemComment(group, rest, -1)}`)
    } else {
      const headers = arrayHeader.get(group) as Set<string>
      if (!headers.has(rest)) {
        block.push(`  ${rest}:`)
        headers.add(rest)
      }
      block.push(`    - ${value}  # ${itemComment(group, rest, index)}`)
    }
  }
  for (const group of order) {
    const label = configSchema().find((g) => g.group === group)?.label ?? group
    lines.push('')
    lines.push(`${group}:  # ${label}（启动器补充）`)
    for (const line of blocks.get(group) as string[]) lines.push(line)
  }
  pending.clear()
}

export interface WriteResult {
  appended: string[]
  error: string
}

export function writeConfigFile(updates: Map<string, string>): WriteResult {
  const file = configFilePath()
  let text = readFileText(file)
  if (text === null) {
    text = generateDefaultYaml()
    fs.mkdirSync(path.dirname(file), { recursive: true })
  }

  const lines = splitLines(text)
  const infos = scanYaml(lines)

  const pending = new Map(updates)
  for (let i = 0; i < lines.length; i++) {
    const info = infos[i]
    if (!info.hasKey || !info.valuePresent) continue
    const next = pending.get(info.key)
    if (next === undefined) continue
    lines[i] = lines[i].slice(0, info.valueBegin) + next + lines[i].slice(info.valueEnd)
    pending.delete(info.key)
  }

  const appended: string[] = []
  if (pending.size > 0) {
    for (const key of [...pending.keys()].sort()) appended.push(key)
    appendMissingKeys(lines, pending)
  }

  const joined = lines.map((l) => l + '\n').join('')
  fs.mkdirSync(path.dirname(file), { recursive: true })
  fs.writeFileSync(file, joined, { encoding: 'utf8', flag: 'w' })
  return { appended, error: '' }
}

// ---------- 数值规整 ----------

export interface NormalizeResult {
  value: string
  error: string
}

export function normalizeValue(item: ConfigItem, raw: string): NormalizeResult {
  const trimmed = trim(raw)
  if (trimmed === '') return { value: '', error: '值为空' }
  const n = Number(trimmed)
  if (!Number.isFinite(n)) return { value: '', error: `不是合法数字：${trimmed}` }
  let clamped = n
  if (clamped < item.min) clamped = item.min
  if (clamped > item.max) clamped = item.max
  if (item.type === 'int') clamped = Math.trunc(clamped)
  return { value: formatNumber(clamped), error: '' }
}

// ---------- 启动选项（~/.ball/launcher.conf） ----------

export interface LaunchOptions {
  output: string
}

export function loadLaunchOptions(): LaunchOptions {
  const options: LaunchOptions = { output: 'output.mp4' }
  const text = readFileText(launcherConfPath())
  if (text === null) return options
  for (const line of text.split('\n')) {
    const eq = line.indexOf('=')
    if (eq === -1) continue
    const key = trim(line.slice(0, eq))
    const value = trim(line.slice(eq + 1))
    if (key === 'output' && value !== '') options.output = value
  }
  return options
}

export function saveLaunchOptions(options: LaunchOptions): void {
  const file = launcherConfPath()
  fs.mkdirSync(path.dirname(file), { recursive: true })
  let out = ''
  out += '# Ball Territory War CLI 启动器配置\n'
  out += '# 录制视频 / 背景音乐 / 限制 60fps / 武器升力 恒定开启，不在此配置\n'
  out += `output=${options.output}\n`
  fs.writeFileSync(file, out, { encoding: 'utf8', flag: 'w' })
}

const KNOWN_EXT = ['.mp4', '.m4v', '.mkv', '.mov', '.avi', '.webm', '.ts', '.flv']

export function normalizeOutputName(name: string): string {
  if (name === '') return 'output.mp4'
  const lower = name.toLowerCase()
  for (const ext of KNOWN_EXT) {
    if (lower.length > ext.length && lower.endsWith(ext)) return name
  }
  return name + '.mp4'
}

export function buildGameArgs(options: LaunchOptions): string[] {
  return ['--output', normalizeOutputName(options.output)]
}

export function joinArgs(args: string[]): string {
  return args
    .map((a) => (a.includes(' ') ? `'${a}'` : a))
    .join(' ')
}
