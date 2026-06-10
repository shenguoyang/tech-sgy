---
title: 写放大（Write Amplification）
type: concept
tags: [性能, SSD, 存储栈]
created: 2026-06-10
updated: 2026-06-10
source: raw/papers/lsm-tree-original.pdf
---

# 写放大（Write Amplification）

## 核心思想

写放大是指存储系统实际写入物理介质的数据量，高于上层应用发出的逻辑写入量的现象。写放大因子（Write Amplification Factor, WAF）= 物理写入量 / 逻辑写入量。WAF > 1 意味着额外开销，影响存储设备的性能和寿命（尤其对 SSD）。

写放大在存储栈的每一层都可能发生：应用层（LSM-Tree Compaction），文件系统层（CoW 元数据更新、碎片整理），闪存转换层（FTL 的垃圾回收 GC）。理解各层的写放大来源是系统优化的关键。

## 典型来源

- **LSM-Tree Compaction**：LevelDB/RocksDB 中，相同数据可能被多次重写到不同层级。读-修改-写周期中，写入放大可达 10–50 倍
- **SSD GC**：SSD 写入以页（Page）为单位，擦除以块（Block）为单位。当需要回收一个 Block 时，其中的有效页需要先读出来再写回新位置。WAF 在高负载随机写入时可达 2–5 倍
- **RAID 校验**：RAID 5/6 的小写操作需要读-修改-写两次（先读旧数据+旧校验 → 计算新校验 → 写新数据+新校验），WAF 可达 4 倍

例如，一个 4KB 的随机写入在通过 LSM-Tree 引擎到达 SSD 后，因 Compaction 和 SSD GC 的综合作用，实际写入的物理数据可能远超 4KB。

## 与其他概念的关系

- [[concepts/lsm-tree.md]] @based_on — LSM-Tree 的 Compaction 是写放大的主要上层来源
- [[concepts/wear-leveling.md]] @contrasts — 磨损均衡通过均匀分布写入来延长 SSD 寿命，而降低写放大是减少磨损的根本手段
- [[concepts/copy-on-write.md]] @contrasts — CoW 的元数据更新会引入额外写放大，但比日志journaling 的代价更可预测
