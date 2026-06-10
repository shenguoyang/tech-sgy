---
title: NVMe（非易失性内存 Express）
type: protocol
tags: [接口协议, SSD, PCIe]
created: 2026-06-10
updated: 2026-06-10
---

# NVMe（Non-Volatile Memory Express）

## 背景与动机

NVMe 是专门为 NAND Flash 等非易失性存储介质设计的主机控制器接口标准。在 NVMe 出现之前，SSD 大多通过 SATA 接口使用 AHCI（Advanced Host Controller Interface）协议，但 AHCI 是 2004 年为机械硬盘设计的，存在根本局限：仅支持单个命令队列（深度 32），每次命令提交需要多次 MMIO 寄存器读写（∼8 次），适合 HDD 的毫秒级延迟，但远跟不上 SSD 的微秒级延迟。

NVMe 充分利用 PCIe 总线的并行能力：支持 64K 个队列，每个队列深度 64K，命令提交仅需 1 次 MMIO 写（Doorbell 机制），将命令开销降低了一个数量级。

## 核心机制

- **多队列并行**：每个 CPU 核心可以绑定独立的提交队列（SQ）和完成队列（CQ），避免锁竞争
- **Doorbell 寄存器**：Host 写完命令后只需写入 Doorbell 通知 Controller，大幅减少 MMIO 操作
- **MSI-X 中断**：支持多达 2048 个中断向量，完成通知可以精确路由到提交命令的 CPU 核心
- **SGL/PRP 数据传递**：支持 Scatter-Gather List（SGL）和 Physical Region Page（PRP）两种方式描述数据缓冲区

例如，在实际测试中，NVMe SSD 的 4K 随机读 IOPS 可达 1M 以上，而 SATA SSD 通常不超过 100K，差异主要来自协议开销而非 NAND 带宽。

## 与相关协议的关系

- [[protocols/ahci.md]] @supersedes — NVMe 在技术上完全取代 AHCI 作为 SSD 接口协议
- [[protocols/nvme-of.md]] @implements — NVMe over Fabrics 将 NVMe 命令集扩展到 RDMA、FC、TCP 等网络传输
- [[protocols/pcie.md]] @based_on — NVMe 基于 PCIe 总线，利用其高带宽和低延迟特性
