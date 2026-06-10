---
title: 写放大（Write Amplification）
type: concept
tags: [性能, SSD, 存储栈, Windows, NTFS, TRIM]
created: 2026-06-10
updated: 2026-06-10
---

# 写放大（Write Amplification）

## 核心思想

写放大是指存储系统实际写入物理介质的数据量，高于上层应用发出的逻辑写入量的现象。写放大因子（Write Amplification Factor, WAF）= 物理写入量 / 逻辑写入量。WAF > 1 意味着额外开销，影响存储设备的性能和使用寿命（尤其对 SSD）。

写放大在存储栈的**每一层**都可能发生。理解各层的写放大来源并进行针对性优化，是 Windows 高性能存储调优的关键。

## Windows 存储栈中的写放大来源

```
应用层  ──── 数据库 Compaction (如 ESE/RocksDB on Windows)
             WAF: 10–50×
  ↓
文件系统 ──── NTFS $LogFile 写入、MFT 更新、USN Journal
             WAF: 1.5–3×
  ↓
卷管理  ──── 动态磁盘元数据同步
             WAF: ~1.05×
  ↓
Storport ──── TRIM/UNMAP 命令下发、I/O 拆分
              影响间接 WAF
  ↓
FTL     ──── SSD 垃圾回收 (GC)、磨损均衡搬迁
             WAF: 2–5× (高负载随机写)
  ↓
NAND    ──── 实际编程操作
             物理写入量
```

### NTFS 写放大来源

1. **$LogFile 日志写入**：NTFS 在修改元数据前必须先将日志记录写入 $LogFile。每次元数据变更产生额外日志写入
2. **MFT 更新**：NTFS 主文件表（Master File Table）本身是文件，文件创建/扩展/属性变更都会触发 MFT 记录的写入
3. **USN Journal**：变更日志（Update Sequence Number Journal）记录每次文件/目录变更，产生持续的后台写入流
4. **碎片整理**：Windows 内置的自动碎片整理（`dfrgui.exe`）会移动文件数据块，产生额外写入

```powershell
# 查看 NTFS 日志大小
fsutil fsinfo ntfsinfo C:
# 输出示例：
# NTFS Volume Serial Number : 0x12345678
# $LogFile 大小             : 67108864 字节 (64 MB)
# USN 已分配                : 0x...
```

### Windows TRIM/UNMAP 路径

TRIM/UNMAP 命令是 Windows 减少 SSD 写放大的关键机制：

```
应用删除文件
    → NTFS 标记空间为空闲
    → 文件系统定期通过 Storport 发送 UNMAP 命令
    → StorNVMe 将 UNMAP 转为 NVMe Dataset Management (Deallocate)
    → SSD FTL 标记物理页为无效（减少 GC 搬迁量）
```

关键行为：
- **内联 TRIM**：Windows 8+ / Server 2012+，文件删除立即发出 TRIM
- **定期 TRIM**：优化驱动器工具（`defrag.exe C: /L`）定期执行批量 TRIM
- **Storport UNMAP**：Storport 负责将文件系统层的 UNMAP 请求转换为 SCSI UNMAP 或 NVMe Deallocate

```powershell
# 检查 TRIM 是否启用
fsutil behavior query DisableDeleteNotify
# NTFS DisableDeleteNotify = 0 → TRIM 已启用

# 手动触发 TRIM
defrag.exe C: /L

# 查看 TRIM 统计
Get-PhysicalDisk | Select FriendlyName, Size, MediaType, `
    @{N='TRIMEnabled';E={(Get-StorageAdvancedProperty -PhysicalDisk $_).IsTrimEnabled}}
```

### ReFS 的写放大优势

相比 NTFS，ReFS 在某些场景具有更低的写放大：
- **块克隆**：克隆文件不产生数据拷贝，WAF 接近于 0（对比拷贝操作）
- **无 $LogFile**：ReFS 使用 CoW 而非日志，减少元数据日志写入开销
- **稀疏 VDL**：ReFS 的 Valid Data Length (VDL) 追踪机制可减少零填充写入

## 通用来源（跨平台）

- **LSM-Tree Compaction**：LevelDB/RocksDB 中相同数据被多次重写到不同层级，WAF 10–50×
- **SSD GC**：SSD 以页写入、以块擦除，回收块时搬迁有效页，WAF 2–5×
- **RAID 校验**：RAID 5/6 小写读-修改-写周期，WAF 可达 4×

## 与其他概念的关系

- [[../storage-stack/port-drivers|Storport 端口驱动]] @implements — Storport 承载 Windows 上的 TRIM/UNMAP 命令传递
- [[wear-leveling|磨损均衡]] @contrasts — 磨损均衡通过搬迁数据会引入额外写入，加剧写放大，两者需要权衡
- [[copy-on-write|Copy-on-Write]] @contrasts — CoW 元数据更新引入额外写放大，但 ReFS 块克隆可抵消
- [[../filesystems/ntfs|NTFS]] @based_on — NTFS $LogFile 和 USN Journal 是 Windows 上层写放大主要来源
