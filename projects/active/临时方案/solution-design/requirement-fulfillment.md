---
title: 需求实现详情 — 十条需求逐条分析
type: output
tags: [存储方案, 需求分析, 验收标准]
created: 2026-06-14
updated: 2026-06-15
source: projects/active/windows高性能存储方案/requirements.md
---

# 需求实现详情

> 逐条分析本方案如何满足 [[requirements.md]] 中的全部10项需求。

---

## R1：可浏览的目录结构（P0）

**需求原文**：文件资源管理器必须能看到层级目录结构，支持复制/粘贴/拖拽，不能使用纯KV存储。

本方案提供两条路径满足 R1，可独立选择或并存。

### R1-A：方案A（Materializer 物化到 NTFS）

Materializer 后台线程将 Ring Buffer 中的 Block 物化为标准 NTFS 文件。物化后的目录树与需求中的层级结构完全一致：

```
<Drive>:\Data\ProjectRoot\
├── Vehicle_VIN_001\
│   ├── Station_Front\
│   │   ├── cam01_001.jpg
│   │   └── pointcloud.pcd
│   ├── Station_Rear\
│   │   └── metadata.json
│   └── vehicle_summary.json
└── Vehicle_VIN_002\...
```

**关键设计决策**：

1. **物化延迟 < 1秒**：Materializer 以100ms间隔轮询积压，车辆检测周期2-10分钟，1秒延迟对运维人员完全无感知。

2. **目录预创建**：收到车辆第一个文件时，Materializer 立即预创建完整目录骨架，后续文件只需在已有目录下创建文件。

3. **不实现虚拟文件系统**：不使用 ProjFS 或 Dokan。物化方案更简——Materializer 就是普通的"创建目录+写文件"逻辑。

4. **文件完整性**：物化时先用临时文件名（`.tmp`），写入完成后 `ReplaceFileW` 原子重命名，崩溃后不残留半写文件。

5. **单盘注意**：单盘部署时物化与 Ring Buffer 共享同一磁盘 I/O 带宽，但物化是异步的、可被限速，不会阻塞写入关键路径。

**验收对照**：

| 标准 | 实现 |
|------|------|
| Windows Explorer 完整浏览 | ✅ 物化后为标准NTFS目录 |
| 复制粘贴拖拽正常 | ✅ 标准文件操作 |
| 目录结构符合层级规范 | ✅ Materializer 严格按 file_path 创建 |

### R1-B：方案B（Viewer Tool 虚拟目录树）

Viewer Tool（`StorageViewer.exe`）是一个独立的 Windows GUI 应用程序，直接从 Ring Buffer 的 BlockMeta 读取元数据并展示虚拟目录树。不写入任何 NTFS 文件。

**Viewer Tool 工作流程**：

1. 以 Direct I/O 只读方式打开 `ring.dat`（共享读，不阻塞写入）
2. 读取 `RingHeader` + `BlockMeta[N]` 数组（约 16KB，一次顺序读）
3. 从所有 `BlockMeta.file_path` 字段（`flags != FREE`）构建内存中的目录树
4. 左侧 TreeView 控件展示层级目录结构；右侧列表展示：文件名、大小、时间戳、可靠性级别、序列号
5. 可配置轮询间隔（默认1秒），重新读取 Header 检测新 Block 到达
6. **"导出选中"** 按钮：从 Ring Buffer 数据区读取选中 Block，写入用户指定的目标目录
7. 导出的文件为标准文件，可在 Explorer 中正常操作

**关键设计决策**：

1. **不写文件系统**：Viewer Tool 只读 Ring Buffer，不做文件系统操作。打开 Viewer 没有额外的 I/O 开销（除了一次 16KB 元数据读取）。

2. **数据新鲜度零延迟**：BlockMeta 在每次 `commit_block` 后立即可见（内存 + 磁盘均已更新），Viewer 读取的就是最新状态。

3. **非 Explorer 原生**：Viewer Tool 不是 Windows Shell 扩展，不支持在 Explorer 中浏览。复制/粘贴/拖拽通过"导出选中"按钮实现。

4. **实现复杂度可控**：核心逻辑 ~300 行 C++ Win32 GUI，使用标准 TreeView/ListView 控件。

**验收对照**：

| 标准 | 方案B 实现 |
|------|-----------|
| 可浏览层级目录结构 | ✅ Viewer Tool TreeView 展示 |
| 复制/粘贴操作 | ✅ "导出选中" 按钮→用户指定目录 |
| 目录结构符合层级规范 | ✅ 严格按 BlockMeta.file_path 构建 |
| 实时性 | ✅ 即时（直接读 BlockMeta，无物化延迟） |

### R1 方案对比

| 能力 | 方案A（物化） | 方案B（Viewer Tool） |
|------|-------------|-------------------|
| 浏览方式 | Windows Explorer 原生 | StorageViewer.exe GUI |
| 复制/粘贴/拖拽 | 原生（标准 NTFS 文件操作） | 导出按钮 |
| 数据新鲜度 | < 1s 物化延迟 | 即时（读 BlockMeta 内存数组） |
| 额外 I/O | 每 Block 多 1 读+1 写 | 0 |
| 额外磁盘空间 | 2x | 1x |
| 实现复杂度 | ~200 行 C++ | ~300 行 C++/Win32 |

---

## R2：冷热数据分层管理（P0）

**需求原文**：热数据（5min, P99<10ms）、温数据（1-30天）、冷数据（30-365天）、过期数据（自动清理），冷热迁移不阻塞写入流。

本方案提供两条路径满足 R2，对应于两种消费方案。

### R2-A：方案A（基于 NTFS 的生命周期）

**前提**：已启用 Materializer（方案A），存在物化后的 NTFS 目录树。

**默认：单盘部署**

| 物理位置 | 逻辑温度 | 管理方式 |
|---------|---------|---------|
| `<Drive>:\RingBuffer\ring.dat` | 热 (0-5min) | Direct I/O, committed_seq 边界 |
| `<Drive>:\Data\ProjectRoot\` | 温 (1-30天) | 物化目录树, 标准 NTFS 文件 |
| `<Drive>:\Archive\ProjectRoot\` | 冷 (30-365天) | Robocopy /MOV 迁移, 保持目录结构 |
| (已删除) | 过期 (>365天) | 定期清理 |

**可选：双盘部署**（SSD + HDD）：将 `<Drive>:\Archive\` 放在 HDD 上以降低成本，Ring Buffer 和 Data 放在 SSD 上保证性能。

**分层实现细节**：

- **热→温**：Materializer 物化完成即转换完成（延迟<1秒）
- **温→冷**：Lifecycle A 线程每小时扫描，`Robocopy /MOV` 迁移超过30天的车辆目录。完全不触碰 Ring Buffer
- **过期清理**：Lifecycle A 线程删除超过365天的归档目录

**不阻塞写入流**：三条路径隔离——Materializer → Data 目录，Lifecycle A → Archive 目录，写入 → Ring Buffer。互不干扰。单盘部署时各路径共享同一磁盘但异步执行。

### R2-B：方案B（基于 BlockMeta 的生命周期）

**前提**：未启用 Materializer，数据仅存在于 Ring Buffer 中。

**核心思想**：不依赖文件系统操作，通过 Lifecycle B 线程扫描 BlockMeta 数组（128 条目，纯内存操作），按时间戳和可靠性级别更新 Block 的 flags，控制 Block 的保留/覆盖/过期行为。

**生命周期状态**：

| 状态 | 条件 | BlockMeta.flags | 存储行为 |
|------|------|----------------|---------|
| **热** (0-5min) | `now - timestamp < 5min` | `FLAG_WRITTEN` | Ring Buffer 中，受保护不可覆盖 |
| **温** (5min-30天) | `5min <= age < 30天` | `FLAG_WRITTEN` 或 `FLAG_INDEXED` | Ring Buffer 中；如 MinIO 启用可选上传温数据桶 |
| **冷** (30-365天) | `30天 <= age < 365天` | `FLAG_ARCHIVED_OVERWRITABLE` 或 `FLAG_MINIO_COLD` | 如 MinIO 启用：数据在冷数据桶，本地 Block 可覆盖。无 MinIO：保留在 Ring Buffer（空间允许） |
| **过期** (>365天) | `age >= 365天` | `FLAG_FREE` | Block 槽位释放，可被新写入覆盖 |

**Lifecycle B 线程工作流程**（每 60 秒扫描一次）：

```
1. 遍历 BlockMeta[0..N-1]（纯内存操作，N≤128）
2. 对每个 flags != FREE 的 Block：
   a. 计算 age = now - meta.timestamp
   b. 根据 reliability 确定温→冷阈值：
      CRITICAL: 60天  |  NORMAL: 30天  |  TEMP: 立即标记过期
   c. 如果 age >= 365天 → meta.flags = FLAG_FREE
   d. 如果 age >= 冷阈值：
      - 若 MinIO 启用且未上传 → 触发上传到冷数据桶 → 成功后 flags = FLAG_ARCHIVED_OVERWRITABLE
      - 若 MinIO 未启用 → 保留在 Ring Buffer（不做操作，靠环形覆盖自然淘汰）
   e. 如果 age >= 5min 且 MinIO 温桶启用且未上传 → 可选上传到温数据桶
```

**覆盖资格判定**（写入路径 `commit_block` 中检查）：

Block 可被环形覆盖的条件（满足任一即可）：
- `flags == FLAG_FREE`（空闲）
- `flags == FLAG_MATERIALIZED`（方案A 已物化）
- `flags == FLAG_ARCHIVED_OVERWRITABLE`（MinIO 冷数据已有副本）
- `reliability == RELIABILITY_TEMP` 且 `age > 5min`

**无 MinIO 的限制**：如果既不启用方案A（NTFS 物化），也不启用 MinIO，则 Ring Buffer 是**唯一**存储。数据保留受限于 Ring Buffer 容量（64GB ≈ 约 10 辆车的完整数据）。超过容量的数据会被环形覆盖自然淘汰——这是方案B 无外部存储时的固有限制，需在部署前评估。

### R2 方案对比

| 方面 | 方案A（NTFS 生命周期） | 方案B（BlockMeta 生命周期） |
|------|----------------------|--------------------------|
| 热→温转换 | Materializer 写 NTFS 文件 | BlockMeta 时间戳检查（无数据复制） |
| 温→冷转换 | Robocopy /MOV 到归档目录 | flag 更新 + 可选 MinIO 上传 |
| 过期清理 | 删除归档目录中的文件 | 标记 FLAG_FREE |
| 生命周期 I/O | 高（Robocopy 拷贝全量数据） | 极低（仅元数据扫描，除非 MinIO 上传） |
| 无 MinIO 最大保留 | 受归档磁盘空间限制 | 受 Ring Buffer 容量限制（64GB） |
| 迁移不阻塞写入 | ✅ 路径隔离 | ✅ 纯内存扫描 |

---

## R3：异常场景数据完整性保证（P0）

**需求原文**：断电/蓝屏/磁盘满/应用崩溃后，已确认落盘数据不丢失不损坏，不出现半写文件。100次断电测试100%完整性。

### 实现方案

核心机制：**写入顺序保证 + 序列号 + CRC32 + 启动扫描恢复**

### 写入三步骤崩溃分析

| 崩溃时刻 | 磁盘状态 | 恢复行为 | 数据结果 |
|---------|---------|---------|---------|
| 步骤4a前 | 无变化 | 无影响 | ✅ 无损失 |
| 步骤4a中 | Block数据损坏 | Meta仍为空→该Block空闲 | ✅ 丢弃，应用重传 |
| 步骤4a完成,4b前 | 数据完整，Meta空闲 | Meta.flags=FREE | ✅ 未"提交"，等价于未写入 |
| 步骤4b中 | Meta不完整 | CRC校验失败→标记FREE | ✅ 丢弃 |
| 步骤4b完成,4c前 | 数据+Meta完整，Header未更新 | committed_seq不包含 | ✅ 以Header为准，未提交 |
| 步骤4c中 | Header损坏 | magic/crc32失败→从Meta全量重建 | ✅ 按Meta最大seq重建 |
| 步骤4c完成 | 完整提交 | 正常恢复 | ✅ 数据完整 |

### 启动恢复流程

```
1. 打开ring.dat → 读取Header → 校验magic+crc32
   ├── 通过 → 以Header为准
   └── 失败 → 全量扫描BlockMeta, 重建Header
2. 遍历BlockMeta:
   ├── data_crc32不匹配 → FREE
   ├── sequence > committed_seq → FREE
   └── 否则 → 有效
3. write_cursor = 第一个FREE索引
4. 对未物化但已提交的Block → 重新触发物化
```

### 磁盘满场景
- Ring Buffer 预分配在启动时完成，运行时不分配
- 物化前检查磁盘空间，< 5% 暂停物化并告警（写入不受影响）
- 环形覆盖前检查 materialized_seq（保证被覆盖数据已物化）

### 验收对照

| 标准 | 实现 |
|------|------|
| 100次断电100%完整性 | CRC32 + 写入顺序保证 |
| 无半写文件 | commit点前写入不可见 |
| 异常恢复后可校验 | 每个Block有data_crc32 |

---

## R4：异构文件大小高效处理（P0）

**需求原文**：1KB JSON ~ 500MB 点云，小文件P99<5ms，大文件≥200MB/s，混合负载退化<20%。

### 实现方案：两路分流

```
if (req.size < 64KB):
    追加到 Write Buffer → 满64KB或100ms超时 → 聚合为1个Block → 1次WriteFile
else:
    扇区对齐 → 直接 DMA 直传
```

### 分流阈值分析

64KB 是 Direct I/O 系统调用开销与数据传输时间的分界线：
- 低于64KB：系统调用开销（~5-10μs）> 数据传输时间（~20μs），应聚合
- 高于64KB：数据传输时间主导，系统调用开销可忽略

### 小文件性能（1KB JSON）
- 50个JSON → 1个64KB Block → 1次WriteFile
- 摊销延迟 ≈ 0.5μs/文件
- P99 < 5ms ✅

### 大文件性能（500MB点云）
- DMA直传，SSD 3GB/s → ~167ms
- 吞吐 ≈ 3GB/s ≥ 200MB/s ✅

### 混合负载
- 小文件Buffer独立于大文件DMA通道
- 共享Ring Buffer写入锁，等待时间 < Block写入时间

### 验收对照

| 标准 | 实现 |
|------|------|
| 1KB P99<5ms | Write Buffer批量聚合 |
| 500MB ≥200MB/s | DMA直传 |
| 混合退化<20% | 等待时间 << 数据传输时间 |

---

## R5：目录结构查询效率（P1）

**需求原文**：百万级文件下，单层枚举<100ms，三层遍历<500ms。

### R5-A：方案A（NTFS B+树目录索引）

目录查询在物化目录树上进行，即标准 NTFS B+树目录索引。

**物化时的优化策略**：

1. **按VIN分目录**：每个车辆独立目录。极端情况下（>10000文件）自动创建子目录分片。
2. **关闭8.3短文件名**：`fsutil behavior set disable8dot3 1`，消除额外MFT条目。
3. **关闭最后访问时间**：`fsutil behavior set disablelastaccess 1`，消除读取时的MFT写回。

**验收对照**：

| 标准 | 实现 |
|------|------|
| 单层10000条目<100ms | NTFS B+树 O(log N) |
| 百万文件三层<500ms | 每层B+树查找+目录条目读取 |
| 性能不随总量线性增长 | B+树对数复杂度 |

### R5-B：方案B（Viewer Tool 内存索引）

Viewer Tool 从 BlockMeta 数组构建内存中的目录树索引。由于 BlockMeta 条目数有限（N ≤ 128），所有查询为纯内存操作。

**查询性能**：

| 操作 | 算法 | 复杂度 | 典型耗时（N=128） |
|------|------|--------|-----------------|
| 构建目录树 | 遍历BlockMeta + 路径前缀分组 | O(N × avg_path_depth) | < 1ms |
| 单层枚举 | 预分组 children 遍历 | O(children_of_node) | < 0.1ms |
| 三层遍历 | 根→层1→层2 逐层展开 | O(N) 最坏 | < 0.5ms |
| 按路径查找 | FNV-1a hash → BlockMeta 数组匹配 | O(1) | < 0.01ms |

**说明**：由于 N 很小（128 个 Block），所有操作远低于需求指标（100ms/500ms）。方案 B 在查询效率上实际上**优于**方案 A（无文件系统调用开销）。但当 N 极大（数千 Block）时，方案 A 的 NTFS B+树会更优。

**验收对照**：

| 标准 | 方案B 实现 |
|------|-----------|
| 单层枚举<100ms | ✅ 纯内存 O(children) << 1ms |
| 三层遍历<500ms | ✅ 纯内存 O(N) < 0.5ms |
| 按路径查找 | ✅ FNV-1a hash O(1) |

---

## R6：海量元数据碎片分析与管理（P1）

**需求原文**：可观测性仪表盘——文件数、容量、碎片率、异常检测。

### 实现方案

**EngineStats** 结构体持续更新，Stats采集线程每10秒刷新。

对外接口极简：
- CLI：`storage_engine.exe --stats`
- API：`struct EngineStats engine.GetStats();`

输出示例：
```
Ring Buffer:  12.3 GB / 64 GB (19.2%)
Warm Storage: 1,234 files, 23.5 GB
Cold Storage: 8,901 files, 189.2 GB
Performance:  P50=0.12ms  P99=2.3ms  P999=15.8ms
Backpressure: OFF
Disk: SSD 234.5 GB free  |  HDD 1.2 TB free
```

### 验收对照

| 标准 | 实现 |
|------|------|
| 元数据统计面板 | CLI --stats + API |
| 碎片分析 | 大目录计数 + 文件分布 |
| 阈值告警 | backpressure_active标志 + large_dir_count |

---

## R7：数据积压优雅处理（P1）

**需求原文**：3倍突发写入5分钟不崩溃，积压30分钟内消化，已确认数据0丢失。

### 实现方案

**Ring Buffer 作为天然缓冲池**：64GB可缓冲约10辆车的完整数据。3倍突发5分钟积压≈19.5GB，远小于64GB容量。

### 三级背压机制

| 使用率 | 行为 | 应用层感知 |
|--------|------|-----------|
| < 80% | 正常写入+正常物化 | 无感知 |
| 80-95% | 正常写入+加速物化 | STATUS_OK |
| 95-100% | 拒绝临时, 普通延迟, 关键正常 | REJECTED/DELAYED |
| 100% | 覆盖最旧已物化Block | 无感知 |

### 消化机制
物化速率受NTFS写入限制（~500MB/s），64GB最慢~128秒消化。

### 验收对照

| 标准 | 实现 |
|------|------|
| 3倍突发5分钟不崩溃 | 64GB >> 19.5GB |
| 30分钟内消化 | ~2分钟 |
| 已确认数据0丢失 | committed_seq之前永不丢失 |

---

## R8：避免分页溢出（P1）

**需求原文**：防止Windows Memory Manager写节流，72h后可用内存>20%。

### 实现方案

**FILE_FLAG_NO_BUFFERING 完全绕过 Page Cache**：
- 数据直接从用户缓冲区DMA到磁盘
- 不产生脏页 → 不触发Dirty Page Threshold
- 不触发Windows写节流
- 不污染Page Cache

### 应用层缓冲区固定
- Write Buffer：固定64KB
- 大文件临时缓冲：写入即释放
- 总占用 < 600MB（32GB系统仅约2%）

### 验收对照

| 标准 | 实现 |
|------|------|
| 72h内存>20% | <600MB固定占用 |
| 无系统级写节流 | NO_BUFFERING不产生脏页 |

---

## R9：数据可靠性分级（P1）

**需求原文**：关键数据不可丢失，普通数据可容忍偶发丢失，临时数据可丢弃。

### 实现方案

| 步骤 | 关键(CRITICAL) | 普通(NORMAL) | 临时(TEMP) |
|------|:---:|:---:|:---:|
| WriteFile(Block数据) | 同步等待 | 同步等待 | 可异步 |
| WriteFile(BlockMeta) | 同步等待 | 同步等待 | 可省略 |
| WriteFile(Header) | 同步等待 | 同步等待 | 可省略 |
| 触发Materializer | 立即通知 | 通知 | 不通知 |
| Write Buffer聚合 | 不参与 | 可参与 | 优先聚合 |
| 背压时 | 永不拒绝 | 95%时DELAYED | 95%时拒绝 |
| 崩溃恢复 | 全部恢复 | 全部恢复 | 不恢复 |

### 临时数据简化

临时数据直接走普通 `WriteFile` 写入 `D:\Temp\`，不走 Ring Buffer。崩溃后自然清理，不占 Ring Buffer 空间。

### 验收对照

| 标准 | 实现 |
|------|------|
| 关键数据fsync等价 | Direct I/O同步WriteFile |
| 普通数据relaxed | 同步但背压可降级 |
| 临时数据自动清理 | 直接写TEMP目录 |

---

## R10：稳定且高效的写入性能（P0综合）

**需求原文**：SSD ≥300MB/s, P99<10ms, P999<50ms, 最大卡顿<200ms, 7×24无≥1s停顿。

### 实现方案

R1-R9的综合效果。

### 延迟分解

| 延迟来源 | 处理 | 典型值 |
|---------|------|--------|
| DMA传输 | DMA直传，扇区对齐 | 20MB→~7ms |
| 系统调用 | Write Buffer批量聚合 | 摊销<1μs/文件 |
| 元数据写入 | 预分配消除 | 0 |
| Page Cache污染 | NO_BUFFERING | 0 |
| 排队等待 | 单Ring+独占锁 | <1μs |
| NTFS $LogFile | 预分配后仅inode时间戳 | 极低 |
| SSD GC | 顺序写入模式 | 偶发<200ms |

### 7×24无≥1s停顿

- 无Page Cache → 无脏页刷盘停顿
- 预分配 → 无元数据分配停顿
- 单Ring文件 → 无多文件竞争
- Materializer异步 → 无物化阻塞
- 背压机制 → 无磁盘满停顿

### 验收对照

| 标准 | 实现 |
|------|------|
| ≥300MB/s | DMA直传接近SSD物理带宽 |
| P99<10ms | 传输7ms+开销<1ms |
| P999<50ms | 大文件低频不影响P999 |
| 卡顿<200ms | 所有运行时元数据操作已消除 |
| 7×24无≥1s停顿 | 系统性消除所有停顿源 |

---

## 参考

- [[design-overview]] — 方案总览
- [[core-data-structures.h]] — 核心数据结构
- [[write-path]] — 写入路径与崩溃分析
- [[crash-recovery]] — 启动恢复
- [[simplicity-analysis]] — 最简分析
