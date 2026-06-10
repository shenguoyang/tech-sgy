---
title: LSM-Tree 论文摘要 — The Log-Structured Merge-Tree
type: archived
tags: [论文, LSM-Tree, 数据结构]
created: 2026-06-10
updated: 2026-06-10
source: raw/papers/lsm-tree-original.pdf
---

# The Log-Structured Merge-Tree (LSM-Tree) — 论文摘要

## 元信息

| 属性 | 内容 |
|------|------|
| 标题 | The Log-Structured Merge-Tree (LSM-Tree) |
| 作者 | Patrick O'Neil, Edward Cheng, Dieter Gawlick, Elizabeth O'Neil |
| 年份 | 1996 |
| 机构 | UMass Boston / Oracle |
| 发表 | Acta Informatica |
| 原始文件 | raw/papers/lsm-tree-original.pdf |

## 核心问题

传统 B-Tree 索引在处理高频写入负载时存在性能瓶颈：每次写入都会引发随机磁盘 I/O（B-Tree 节点拆分、页分裂），导致较高的延迟和较低的吞吐量。对于日志、事件记录、交易流水等写多读少的工作负载，B-Tree 的代价过高。

## 关键思想

1. **写入优化**：将随机写转化为顺序写。新数据先缓存于内存中的 C0 树（类似 MemTable），满后整体刷入磁盘中的 C1 树（第一个 SSTable 层级），后续层级（C2、C3…）容量逐层增大
2. **后台合并**：通过 Rolling Merge 过程将 C0 的数据合并到 C1，C1 到 C2，…以此类推。合并过程是顺序读写，充分利用磁盘带宽
3. **读放大控制**：通过布隆过滤器（Bloom Filter）和 Block Index 加速查找，避免遍历所有层级
4. **空间效率**：通过合并过程中的去重和数据压缩，LSM-Tree 比等价的 B-Tree 占用更少磁盘空间

## 提取的知识页面

- [[concepts/lsm-tree.md]] — LSM-Tree 概念详解
- [[concepts/write-amplification.md]] — Compaction 引入的写放大问题
- [[concepts/b-tree.md]] — LSM-Tree 的主要竞争结构（待创建）

## 论文中的关键数据

- 在多层级 LSM-Tree（C0+C1+C2）中，若每层容量比为 r:1，平均读代价为 B-Tree 的 `(1 + 1/r + 1/r²)` 倍
- 当 r=10 时，读代价约 1.11 倍 B-Tree，写性能可达 B-Tree 的 10 倍以上
- 布隆过滤器可以将非存在键的查找概率从 "必须搜索所有层" 降低到 "1–5% 误判率"
