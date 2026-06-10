---
title: LSM-Tree（日志结构合并树）
type: concept
tags: [数据结构, 存储引擎, 写入优化]
created: 2026-06-10
updated: 2026-06-10
source: raw/papers/lsm-tree-original.pdf
---

# 日志结构合并树（Log-Structured Merge-Tree, LSM-Tree）

## 核心思想

LSM-Tree 是一种专为**写密集型**工作负载设计的数据结构。它的核心思想是将随机写操作转化为顺序写：新数据首先写入内存中的 MemTable（有序结构，通常用跳表或红黑树实现），当 MemTable 满后，将其整体刷入磁盘形成一个不可变的 Sorted String Table（SSTable）。由于数据是批量顺序写入的，LSM-Tree 能充分利用磁盘的顺序写带宽，写入性能远超 B-Tree。

LSM-Tree 的读操作需要从最新的 MemTable 开始，逐层向下查找，直到找到目标数据或确认不存在。为了控制读放大，LSM-Tree 在后台运行 Compaction（合并压缩）过程，将多个有序的 SSTable 合并成更大的有序文件，同时剔除过期或已删除的数据。

## 典型实现

- **LevelDB**（Google）：C++ 实现的键值存储，Level 0 层 SSTable 之间可能重叠
- **RocksDB**（Meta）：LevelDB 的增强版，支持多线程 Compaction、Column Family 等
- **Cassandra**（Apache）：分布式数据库，使用 LSM-Tree 作为单节点存储引擎
- **HBase**（Apache）：基于 HDFS 的列式数据库，底层使用 LSM-Tree 变体

例如，RocksDB 在写入时先将数据写入 MemTable → 刷入 Level 0 SSTable → 通过 Compaction 逐层下沉到 Level 1/2/…，每层容量约为上一层的 10 倍。

## 与其他概念的关系

- [[concepts/write-amplification.md]] @contrasts — LSM-Tree 的 Compaction 过程会引入写放大，这是其主要的性能代价
- [[concepts/b-tree.md]] @competes — B-Tree 是传统的读优化结构，适合读多写少的场景
- [[concepts/copy-on-write.md]] @contrasts — CoW 通过快照实现一致性，而 LSM-Tree 通过不可变 SSTable 自然支持快照
- [[software/rocksdb.md]] @based_on — RocksDB 基于 LSM-Tree 设计，是该结构最广泛使用的工程实现
