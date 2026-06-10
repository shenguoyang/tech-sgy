---
title: 写时复制（Copy-on-Write）
type: concept
tags: [文件系统, 快照, 数据完整性]
created: 2026-06-10
updated: 2026-06-10
---

# 写时复制（Copy-on-Write, CoW）

## 核心思想

写时复制是一种数据更新策略：当需要修改某块数据时，不直接覆盖原有数据，而是将修改后的数据写入一个新的位置，然后原子性地更新指针指向新位置。原有数据保持不变，直到没有任何指针引用它时才被回收。

这种策略带来了两个关键优势：**快照**几乎是免费的（只需保留旧指针），以及**数据完整性**（如果写操作中途崩溃，旧指针仍然完整，不会出现数据部分写入的情况）。代价是需要额外的空间和后台垃圾回收来清理孤立块。

## 典型实现

- **ZFS**：将所有数据组织成 Merkle 树（Block Tree），每次写操作都会创建新的数据块，最终更新 uberblock 指针
- **Btrfs**：Linux 原生的 CoW 文件系统，支持子卷（subvolume）快照
- **WAFL**（NetApp）：专有文件系统，广泛应用于 NetApp 存储阵列

例如，ZFS 中创建一个快照只需保存当前的 uberblock 指针，不需要复制任何数据。后续对文件系统的写操作通过 CoW 机制不会影响快照数据。

## 与其他概念的关系

- [[concepts/lsm-tree.md]] @contrasts — LSM-Tree 的不可变 SSTable 与 CoW 思路相似，均避免原地修改
- [[concepts/journaling.md]] @contrasts — 日志journaling 先写日志再修改数据，CoW 通过指针原子更新保证一致性
- [[software/zfs.md]] @based_on — ZFS 文件系统基于 CoW 实现快照和数据完整性校验
- [[concepts/wear-leveling.md]] @contrasts — 磨损均衡通过重映射避免重复写入同一物理位置，与 CoW 的间接寻址有相似之处
