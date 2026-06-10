---
title: 磨损均衡（Wear Leveling）
type: concept
tags: [SSD, NAND, 可靠性, Windows, TRIM, UNMAP]
created: 2026-06-10
updated: 2026-06-10
---

# 磨损均衡（Wear Leveling）

## 核心思想

磨损均衡是 SSD 控制器中的一项关键技术，用于解决 NAND Flash 的编程/擦除（P/E）周期限制。每个 NAND 单元只能承受有限次数的擦除操作（SLC ∼100K 次，MLC ∼10K 次，TLC ∼3K 次，QLC ∼1K 次）。工作负载往往对某些逻辑地址产生集中写入（如文件系统元数据区域），若不干预，这些 "热点" 块会提前耗尽寿命。

磨损均衡算法的核心是**动态重映射**：通过 FTL（Flash Translation Layer）维护逻辑块地址（LBA）到物理块地址（PBA）的映射表，将写入分散到不同的物理块，使得所有块的擦除次数尽可能均匀分布。

## 两种策略

- **动态磨损均衡（Dynamic）**：只对空闲块池中的块做均衡选择。当热点频繁更新同一 LBA 时，每次分配不同的空闲 PBA。策略简单，但无法处理冷数据长期占用的块
- **静态磨损均衡（Static）**：主动将冷数据（很少被修改的块）搬迁到擦除次数较高的块上，释放低擦除次数的块供热数据使用。代价是增加了额外的数据搬迁开销

例如，企业 SSD 在日常混合负载下，通过静态磨损均衡可将 P/E 周期差异从 10 倍降低到 1.5 倍以内。

## Windows 与 SSD 磨损均衡

### TRIM/UNMAP 的作用

在 Windows 上，TRIM（ATA 术语）和 UNMAP（SCSI 术语）是操作系统告知 SSD "这些 LBA 不再包含有效数据" 的机制。这对磨损均衡至关重要：

- **没有 TRIM**：SSD FTL 不知道哪些 LBA 已被文件系统释放，GC 搬迁时会保留已删除文件的数据块，造成不必要的 P/E 消耗
- **有 TRIM**：SSD FTL 可将 TRIM 过的块直接标记为无效，GC 时无需搬迁，降低 WAF → 间接减少磨损 → 延长 SSD 寿命

### Windows TRIM 路径

```
文件系统 (NTFS/ReFS)
    文件删除 → 标记 LBA 范围为空闲
        │
        ▼
磁盘类驱动 (disk.sys)
    构建 SCSI UNMAP 命令
        │
        ▼
Storport (Storport.sys)
    将 UNMAP 传递给 Miniport
        │
        ▼
Miniport (stornvme.sys)
    将 SCSI UNMAP → NVMe Dataset Management (Deallocate)
        │
        ▼
NVMe SSD
    FTL 标记物理页无效 → 提升 GC 效率 → 降低磨损
```

```powershell
# 查看 SSD TRIM 支持状态
Get-PhysicalDisk | Select FriendlyName, MediaType, `
    @{N='TRIM';E={(Get-StorageAdvancedProperty -PhysicalDisk $_).IsTrimEnabled}}

# 手动触发批量 TRIM（优化驱动器）
Optimize-Volume -DriveLetter C -ReTrim -Verbose

# 传统方式
defrag.exe C: /L

# 查看文件系统 TRIM 行为
fsutil behavior query DisableDeleteNotify
# DisableDeleteNotify = 0 → TRIM 启用 (默认)
# DisableDeleteNotify = 1 → TRIM 禁用
```

### Windows 特有的 TRIM 行为

| 场景 | TRIM 行为 |
|------|----------|
| 删除文件 | 立即发出 (内联 TRIM，Windows 8+) |
| 格式化分区 | 完全 TRIM (Windows 7+) |
| 优化驱动器计划 | 默认每周自动批量 TRIM |
| Storage Spaces | 支持 TRIM 透传到物理磁盘 (Windows Server 2012 R2+) |
| Hyper-V VHDX | 动态 VHDX 支持 TRIM 透传缩小文件 |
| ReFS | 使用 CoW 而非原地覆盖，删除后 TRIM 旧块 |

### 监控 SSD 寿命

```powershell
# PowerShell 查看 SSD 健康信息
Get-PhysicalDisk | Get-StorageReliabilityCounter | `
    Select-Object DeviceId, Wear, ReadErrorsTotal, WriteErrorsTotal, `
        Temperature, PowerOnHours

# 使用 WMI 查询 NVMe 健康信息
Get-WmiObject -Namespace "Root\Microsoft\Windows\Storage" `
    -Class MSFT_PhysicalDisk | `
    Where-Object MediaType -eq 4 | `  # 4 = SSD
    Select-Object FriendlyName, HealthStatus, Usage
```

## 与其他概念的关系

- [[write-amplification|写放大]] @contrasts — 磨损均衡通过搬迁数据会引入额外写入，加剧写放大；而 TRIM 通过降低 WAF 间接减轻磨损
- [[copy-on-write|Copy-on-Write]] @contrasts — FTL 的重映射机制与 CoW 的间接寻址在思想上相似
- [[../protocols/nvme|NVMe]] @implements — NVMe Dataset Management 命令是 TRIM/UNMAP 在 NVMe 协议层的载体
- [[../storage-stack/port-drivers|Storport]] @based_on — Windows 的 TRIM 命令通过 Storport 栈递到 SSD
