---
title: ZFS 文件系统
type: archived
tags: [文件系统, 数据完整性, CoW, 快照]
created: 2026-06-10
updated: 2026-06-10
---

# ZFS 文件系统

## 设计哲学

ZFS 由 Sun Microsystems 于 2005 年发布，其设计哲学是"信任但验证"：假设底层硬件不可靠，通过端到端的数据校验、自我修复能力、以及永不覆写数据来保证数据的完整性。ZFS 同时整合了文件系统和卷管理功能，用一个统一的存储池（zpool）管理所有物理设备，消除了传统分层带来的复杂性。

## 关键架构

- **存储池（zpool）**：由一个或多个 VDEV（虚拟设备）组成。VDEV 支持 Mirror、RAID-Z1/Z2/Z3 等冗余模式。与传统 RAID 不同，ZFS 自行管理数据分布，不需要专用 RAID 控制器
- **写时复制**：所有数据写入遵循 [[concepts/copy-on-write.md]] 原则。数据写入新块后原子更新 uberblock 指针，确保崩溃一致性
- **Merkle Tree 校验**：数据块通过 SHA-256 校验和构建哈希树，每次读取自动校验。发现校验错误时，如果有冗余副本则自动修复
- **ZIL 和 ARC**：ZFS Intent Log（ZIL）缓存同步写入以防崩溃丢失，Adaptive Replacement Cache（ARC）管理读缓存

例如，一个 4 盘 RAID-Z2 的 zpool 创建快照只需毫秒级，快照数据通过 CoW 机制与活跃数据共享未修改的块，空间开销极低。

## 与其他系统的关系

- [[concepts/copy-on-write.md]] @based_on — ZFS 的写时复制机制源自 CoW 概念
- [[software/btrfs.md]] @competes — Btrfs 是 Linux 生态中的 CoW 文件系统，设计受 ZFS 影响但独立实现
- [[concepts/write-amplification.md]] @contrasts — ZFS 的 CoW 和 ZIL 双重写入会引入额外的写放大
