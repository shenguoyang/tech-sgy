---
title: 写时复制（Copy-on-Write）
type: concept
tags: [文件系统, 快照, 数据完整性, Windows, ReFS, VSS]
created: 2026-06-10
updated: 2026-06-10
---

# 写时复制（Copy-on-Write, CoW）

## 核心思想

写时复制是一种数据更新策略：当需要修改某块数据时，不直接覆盖原有数据，而是将修改后的数据写入一个新位置，然后原子性地更新指针指向新位置。原有数据保持不变，直到没有任何指针引用它时才被回收。

这种策略带来了两个关键优势：**快照**几乎是免费的（只需保留旧指针），以及**数据完整性**——如果写操作中途崩溃，旧指针仍然完整，不会出现数据部分写入的情况。代价是需要额外的空间和后台垃圾回收来清理孤立块。

## Windows 上的典型实现

### ReFS (Resilient File System)

ReFS 是 Windows 上的 CoW 文件系统，与 NTFS 互补而非替代。ReFS 的设计目标是为虚拟化、数据归档和大规模存储提供更强的数据完整性保障。

ReFS 的 CoW 机制：

- **元数据 CoW**：所有元数据更新使用 CoW（默认开启），确保文件系统结构即使在中途崩溃后也能保持一致性
- **数据 CoW**（可选）：通过 `FILE_FLAG_WRITE_THROUGH` 或设置文件完整性流（Integrity Stream）来对特定文件启用数据 CoW
- **块克隆（Block Cloning）**：Windows Server 2016+ 新增，克隆文件时不复制数据块，而是共享物理块（类似 Reflink），修改时才触发 CoW

```
ReFS CoW 与其他实现的对比：

                 ReFS          ZFS         Btrfs
数据 CoW    可选(按文件)    始终开启     始终开启
元数据 CoW   始终开启       始终开启     始终开启
块克隆      原生支持        无原生支持   cp --reflink
快照        仅存储方案层    ZFS Snapshot Btrfs Subvolume
校验和      默认开启        默认开启     可选
```

### VSS (Volume Shadow Copy Service)

VSS 是 Windows 内置的卷级快照框架，支持多种快照提供者（Provider）：

- **系统提供者**：基于 CoW。在卷上分配 "差异区域（Diff Area）" ，快照创建后，任何对原始卷的修改都会先将原始数据复制到差异区域。快照视图 = 原始数据（未修改部分）+ 差异区域（已修改的旧值）
- **硬件提供者**：利用 SAN 存储阵列的硬件快照功能（如 NetApp WAFL 的 CoW 快照）
- **软件提供者**：如 Storage Spaces 的 CoW 层

VSS CoW 工作流程：
```
1. VSS 创建快照 → 冻结 I/O → 分配 Diff Area
2. I/O 恢复。应用尝试修改块 X：
   a. VSS 拦截写入 → 将块 X 的当前内容复制到 Diff Area
   b. 写入继续（新数据覆盖块 X）
3. 读取快照时的块 X → 从 Diff Area 读取（因为 X 已被修改）
4. 读取快照时的块 Y（未修改）→ 直接从原始卷读取
```

### NTFS 的受限 CoW

NTFS 本身不是 CoW 文件系统（它使用日志journaling），但 Windows 在 NTFS 上通过以下机制实现了受限 CoW：

- **NTFS 日志（$LogFile）**：记录元数据操作日志，崩溃恢复时重放，保证元数据一致性（但不同于 CoW 的指针原子更新）
- **NTFS 原子操作**：通过 `TxF`（Transactional NTFS，已弃用）或用户态 `NtSetInformationFile()` 重命名操作实现链式原子事务

## 与其他概念的关系

- [[../filesystems/refs|ReFS]] @based_on — ReFS 是 Windows 上 CoW 文件系统的核心实现
- [[../concepts/journaling|日志journaling]] @contrasts — CoW 通过指针原子更新保证一致性，日志通过先写日志再修改数据来保证一致性。NTFS 使用日志，ReFS 使用 CoW
- [[../solutions/storage-spaces|Storage Spaces]] @implements — Storage Spaces 在块层可提供 CoW 差分磁盘
- [[wear-leveling|磨损均衡]] @contrasts — FTL 重映射与 CoW 的间接寻址在思想上有相似之处
- [[write-amplification|写放大]] @contrasts — CoW 的元数据更新会引入额外写放大，但 ReFS 通过块克隆可显著降低某些场景的 WAF

## 具体例子：ReFS 块克隆

```powershell
# Windows Server 2022 上使用 ReFS 块克隆
# 创建一个 10GB 的文件
fsutil file createnew D:\source.dat 10737418240

# 块克隆 —— 几乎瞬间完成，不占额外空间
# (通过 CopyFileEx 或 VM 快照操作自动触发)
# 只有当修改克隆文件时才会触发 CoW 分配新块
```
