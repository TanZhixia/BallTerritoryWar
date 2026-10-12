// ==================== 配置 schema（唯一真相） ====================
// 移植自 launcher/src/config.cpp 的 BuildSchema()：11 组 56 项
// （lift.weaponInitial 为数组，展开后共 58 条），含每项 min/max/默认值/类型/重启标记。
// 改游戏配置键时必须同步这里（以及 src/core/config.h / config.cpp）。

export type ItemType = 'float' | 'int' | 'array'

export interface ConfigItem {
  key: string
  label: string
  desc: string
  type: ItemType
  count: number // 数组元素个数
  min: number
  max: number
  defaults: number[] // 标量取 [0]；数组取 [0..count)
  restartOnly: boolean
}

export interface ConfigGroup {
  group: string
  label: string
  items: ConfigItem[]
}

function floatItem(
  key: string,
  label: string,
  desc: string,
  min: number,
  max: number,
  def: number,
  restartOnly = false,
): ConfigItem {
  return { key, label, desc, type: 'float', count: 1, min, max, defaults: [def], restartOnly }
}

function intItem(
  key: string,
  label: string,
  desc: string,
  min: number,
  max: number,
  def: number,
  restartOnly = false,
): ConfigItem {
  return { key, label, desc, type: 'int', count: 1, min, max, defaults: [def], restartOnly }
}

function arrayItem(
  key: string,
  label: string,
  desc: string,
  min: number,
  max: number,
  a: number,
  b: number,
  c: number,
): ConfigItem {
  return { key, label, desc, type: 'array', count: 3, min, max, defaults: [a, b, c], restartOnly: false }
}

function buildSchema(): ConfigGroup[] {
  return [
    {
      group: 'paintBalls',
      label: '画笔球',
      items: [
        floatItem('speed', '速度', '画笔球飞行速度', 10, 400, 120),
        floatItem('initialValue', '初始价值', '球的初始价值', 0, 100, 15),
        floatItem('deleteValue', '删除阈值', '价值 ≤ 此值时删除', 0, 10, 0),
        floatItem('radius', '半径', '普通球半径', 1, 10, 2),
        floatItem('pixelCostBase', '涂画基础消耗', '涂画每像素基础消耗', 0, 10, 1),
        floatItem('pixelCostPerMinute', '消耗增长间隔', '每 N 分钟消耗 +1', 0.5, 60, 3),
        floatItem('pixelCostMax', '消耗上限', '涂画消耗上限', 1, 100, 16),
      ],
    },
    {
      group: 'physics',
      label: '物理球',
      items: [
        intItem('countPerColor', '每队数量', '每队物理球数量', 1, 10, 4, true),
        floatItem('initialValue', '初始价值', '物理球初始价值', 100, 1000000, 1000, true),
        floatItem('radius', '半径', '物理球半径', 2, 30, 10),
        floatItem('gravity', '重力', '机械区重力', 0, 2000, 200),
        floatItem('launchSpeed', '发射速度', '重置/发射速度', 20, 500, 120),
        floatItem('restitution', '反弹系数', '1 = 无损耗', 0, 1, 1),
      ],
    },
    {
      group: 'lift',
      label: '升力系统',
      items: [
        floatItem('initialThreshold', '缺口初始阈值', '缺口升力初始阈值（无上限，按增长率一直增长）', 1000, 100000000, 100000),
        floatItem('growthPerSecond', '每秒增长率', '0.01 = 1%', 0, 0.1, 0.01),
        floatItem('acceleration', '升力加速度', '升力加速度', 0, 5000, 1200),
        arrayItem('weaponInitial', '武器区初始阈值', '*8 / *4 / *2 升力初始阈值（无上限）', 1000, 100000000, 50000, 80000, 100000),
        floatItem('highValueLimit', '前期高价值限制', '10 分钟内超过此值的球在霰弹/狙击列被顶回', 10000, 100000000, 2000000),
      ],
    },
    {
      group: 'machineGun',
      label: '机枪',
      items: [
        floatItem('initialAmmo', '初始弹药', '每队初始弹药', 0, 10000000, 250000, true),
        floatItem('drainDivisor', '消耗除数', '弹药消耗 = ammo / 此值（每帧）', 100, 100000, 12000),
        floatItem('minBallValue', '单发最小价值', '单发子弹最小价值', 1, 1000, 20),
        floatItem('maxBallsPerFrame', '每帧最多发射', '每色每帧发射上限', 1, 100, 10),
        floatItem('speed', '子弹速度', '机枪子弹速度', 10, 500, 120),
        floatItem('spreadDeg', '散射角度', '总散射角度（度）', 0, 30, 4),
        floatItem('rotateDegPerFrame', '炮管转速', '无目标时每帧旋转（度）', 0, 10, 0.5),
        floatItem('lockDistance', '锁定距离', '锁定敌方大球的距离', 50, 1000, 300),
      ],
    },
    {
      group: 'shotgun',
      label: '霰弹',
      items: [
        floatItem('fragmentValue', '每份价值', 'value / 此值 = 碎片数', 1, 1000, 50),
        intItem('maxFragments', '碎片上限', '碎片数上限', 10, 10000, 1000),
        floatItem('spreadDeg', '散射角度', '总散射角度（度）', 0, 30, 4),
        floatItem('speed', '碎片速度', '碎片速度', 10, 500, 120),
      ],
    },
    {
      group: 'sniper',
      label: '狙击',
      items: [
        floatItem('explosionValue', '爆炸每份价值', '爆炸碎片每份价值', 1, 1000, 50),
        intItem('maxFragments', '爆炸碎片上限', '爆炸碎片数上限', 10, 10000, 1000),
        floatItem('speed', '速度', '狙击/碎片速度', 10, 500, 120),
        floatItem('jitter', '飞行抖动', '抖动幅度（像素），仅显示层', 0, 20, 3),
        intItem('particlesPerFrame', '每帧粒子数', '飞行时生成的尾迹粒子', 0, 20, 3),
        floatItem('selfDestructChance', '每秒自爆概率', '狙击不稳定，0 = 关闭', 0, 1, 0.01),
        floatItem('dotTrailLife', '光点拖尾时长', '秒，0 = 关闭', 0, 1, 0.25),
        floatItem('gravityRadius', '引力半径', '吸引此范围内所有球（含大球）', 0, 400, 150),
        floatItem('gravityStrength', '引力常数 G', 'a = G × 质量 / r²', 0, 100, 4),
        floatItem('gravityMaxSpeed', '吸引速度上限', '被吸引球的速度上限（防失控）', 60, 2000, 600),
        floatItem('gravityValueGain', '引力场增值', '引力场内的小球每秒 +value（0 = 关闭）', 0, 1000, 1),
      ],
    },
    {
      group: 'bigBall',
      label: '大球',
      items: [
        floatItem('radiusLogOffset', '半径对数偏移', '半径 = (log(value) + 此值) × 2', 0, 20, 5),
        floatItem('radiusMin', '半径下限', '大球半径下限', 1, 50, 2),
        floatItem('speedFactor', '速度系数', '大球速度 = 画笔球速度 × 此值', 0.1, 2, 0.5),
        floatItem('trailValue', '拖尾消耗', '每帧扣大球价值', 0, 500, 20),
        floatItem('gravityRadius', '引力半径', '大球引力半径（吸引大球与狙击碎片）', 0, 1000, 260),
        floatItem('gravityStrength', '大球引力常数', '敌对大球之间互吸（a = G × value / r²，同色不吸）', 0, 100, 0.5),
        floatItem('gravityMaxSpeed', '吸引速度上限', '被吸引球的速度上限（防失控）', 60, 2000, 480),
        floatItem('fragmentGravityStrength', '碎片引力常数', '狙击爆炸碎片被大球吸引（a = G × value / r²，0 = 关闭）', 0, 100, 0.5),
        floatItem('splitIntervalSeconds', '分裂间隔', '大球每 N 秒分裂成两个 value/2（0 = 关闭）', 0, 600, 60),
      ],
    },
    {
      group: 'shield',
      label: '护盾',
      items: [
        floatItem('initialValue', '初始护盾', '每队初始护盾', 0, 1000000000, 10000000, true),
        floatItem('radius', '半径', '护盾圆半径', 20, 300, 80),
      ],
    },
    {
      group: 'combat',
      label: '战斗',
      items: [
        floatItem('absorbRatio', '吞并倍数', '价值差 ≥ 此倍数直接吞并', 1, 100, 20),
        floatItem('damageRatio', '伤害比例', '互扣大者价值的此比例', 0, 1, 0.1),
      ],
    },
    {
      group: 'gameOver',
      label: '结束条件',
      items: [intItem('countdownFrames', '倒计时帧数', '60fps 下 3600 = 60 秒', 60, 36000, 3600)],
    },
    {
      group: 'startup',
      label: '开局',
      items: [
        floatItem('openingShotgunValue', '开局霰弹总价值', '每队开局向中心发射的霰弹总价值', 0, 100000000, 2500000, true),
      ],
    },
  ]
}

let cached: ConfigGroup[] | null = null
export function configSchema(): ConfigGroup[] {
  if (cached === null) {
    cached = buildSchema()
  }
  return cached
}

export interface ConfigEntry {
  group: string
  item: ConfigItem
  index: number // -1 标量；>=0 数组下标
}

export function configEntries(): ConfigEntry[] {
  const entries: ConfigEntry[] = []
  for (const group of configSchema()) {
    for (const item of group.items) {
      if (item.type === 'array') {
        for (let i = 0; i < item.count; i++) {
          entries.push({ group: group.group, item, index: i })
        }
      } else {
        entries.push({ group: group.group, item, index: -1 })
      }
    }
  }
  return entries
}

export function findItem(group: string, key: string): ConfigItem | undefined {
  const g = configSchema().find((x) => x.group === group)
  return g?.items.find((it) => it.key === key)
}

export function findGroup(group: string): ConfigGroup | undefined {
  return configSchema().find((x) => x.group === group)
}

export function fullKey(group: string, item: ConfigItem, index: number): string {
  let key = `${group}.${item.key}`
  if (index >= 0) {
    key += `.${index}`
  }
  return key
}
