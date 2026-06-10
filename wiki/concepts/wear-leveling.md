---
title: 磨损均衡（Wear Leveling）
type: concept
tags: [SSD, NAND, 可靠性]
created: 2026-06-10
updated: 2026-06-10
---

# 磨损均衡（Wear Leveling）

## 核心思想

磨损均衡是 SSD 控制器中的一项关键技术，用于解决 NAND Flash 的编程/擦除（P/E）周期限制问题。每个 NAND 单元只能承受有限次数的擦除操作（SLC ∼100K 次，MLC ∼10K 次，TLC ∼3K 次，QLC ∼1K 次），而工作负载往往对某些逻辑地址（如元数据区域）产生集中写入，若不干预，这些"热点"块会提前耗尽寿命。

磨损均衡算法的核心是**动态重映射**：通过 FTL（Flash Translation Layer）维护逻辑块地址（LBA）到物理块地址（PBA）的映射表，将写入分散到不同的物理块，使得所有块的擦除次数尽可能均匀分布。

## 两种策略

- **动态磨损均衡（Dynamic）**：只对空闲块池中的块做均衡选择。当热点频繁更新同一 LBA 时，每次分配不同的空闲 PBA。策略简单，但无法处理冷数据长期占用的块
- **静态磨损均衡（Static）**：主动将冷数据（很少被修改的块）搬迁到擦除次数较高的块上，释放低擦除次数的块供热数据使用。代价是增加了额外的数据搬迁开销

例如，一个企业的 SSD 在日常混合负载下，通过静态磨损均衡可将 P/E 周期差异从 10 倍降低到 1.5 倍以内。

## 与其他概念的关系

- [[concepts/write-amplification.md]] @contrasts — 磨损均衡通过搬迁数据会引入额外的写入，加剧写放大，两者需要权衡
- [[concepts/copy-on-write.md]] @contrasts — FTL 的重映射机制与 CoW 的间接寻址在思想上相似
- [[hardware/nand-flash.md]] @based_on — NAND Flash 的 P/E 限制是磨损均衡存在的根本原因
