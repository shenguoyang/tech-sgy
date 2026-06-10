---
title: Ceph 分布式存储系统
type: archived
tags: [分布式存储, 对象存储, CRUSH]
created: 2026-06-10
updated: 2026-06-10
---

# Ceph 分布式存储系统

## 设计哲学

Ceph 是一个统一的分布式存储系统，同时提供对象存储（RADOSGW，兼容 S3/Swift API）、块存储（RBD）和文件存储（CephFS）三种接口。其核心设计原则是**去中心化**：不依赖元数据服务器进行数据定位，而是通过 CRUSH（Controlled Replication Under Scalable Hashing）算法让客户端自行计算出数据所在位置。

## 关键架构

- **RADOS**：最底层，可靠自主分布式对象存储。由 OSD（Object Storage Daemon）和 Monitor 组成。OSD 负责存储数据并执行副本/纠删码恢复，Monitor 维护集群状态图（Cluster Map）
- **CRUSH 算法**：客户端持有 Cluster Map，通过 CRUSH 算法直接计算任意对象所在的 OSD，无需查询中心元数据服务。这消除了单点瓶颈，实现了线性扩展
- **Placement Group（PG）**：对象先映射到 PG，PG 再根据 CRUSH 规则映射到一组 OSD。PG 是数据迁移和恢复的最小粒度
- **多副本与纠删码**：数据池支持 2x/3x 复制或纠删码（Erasure Coding）。纠删码在保证可靠性的同时将存储开销从 200% 降低到 33–50%

例如，一个 100 节点 Ceph 集群中，RBD 客户端写入 4MB 的数据块时，先切分为 4MB 对象 → 映射到 PG → CRUSH 计算目标 OSD → 并行发送到主 OSD → 主 OSD 同步到副本 OSD → 全部完成后返回确认。

## 与其他系统的关系

- [[software/glusterfs.md]] @competes — GlusterFS 是另一个去中心化的分布式文件系统，但数据定位方式不同
- [[concepts/copy-on-write.md]] @contrasts — Ceph 的 RBD 快照使用了类似于 CoW 的机制
- [[concepts/lsm-tree.md]] @based_on — Ceph OSD 后端存储引擎 BlueStore 使用了类似 LSM-Tree 的 RocksDB 存储元数据
