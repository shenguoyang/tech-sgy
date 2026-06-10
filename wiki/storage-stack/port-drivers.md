---
title: Windows Storport 端口驱动
type: stack-layer
tags: [Storport, 存储驱动, Windows内核, NVMe]
created: 2026-06-10
updated: 2026-06-10
---

# Windows Storport 端口驱动

## 背景与动机

Windows 存储端口驱动经历了三代演进：

| 代 | 驱动 | 时代 | 局限 |
|----|------|------|------|
| 第一代 | SCSIPORT | Windows NT → 2000 | 单队列，队列深度 32，大量 MMIO |
| 第二代 | STORPORT | Windows 2003 → 至今 | 显著改进但仍受传统 SCSI 模型限制 |
| 第三代 | **Storport** (重构) | Windows 8 / Server 2012 → 至今 | 为 NVMe/SSD 重新设计的高性能框架 |

NVMe 的出现彻底改变了 Windows 存储栈的设计考量。AHCI 时代每个命令需要 **~8 次 MMIO 寄存器写入**，而 NVMe 通过**提交队列 Doorbell 寄存器**将开销降到 **1 次写入**。Storport 必需演进以释放 NVMe SSD 的全部性能。

## 核心机制

### Storport 架构总览

```
                classpnp.sys (类驱动)
                       │ SRB
                       ▼
┌──────────────────────────────────────────┐
│              Storport.sys                │
│                                          │
│  ┌──────────┐  ┌──────────┐  ┌────────┐ │
│  │ 队列管理器 │  │ I/O调度器 │  │ 中断路由│ │
│  │64K 队列   │  │ NUMA感知  │  │MSI-X   │ │
│  └──────────┘  └──────────┘  └────────┘ │
│                                          │
│  ┌──────────────────────────────────┐   │
│  │  Miniport 接口 (HwStorXxx)       │   │
│  │  HwStorBuildIo → HwStorStartIo   │   │
│  └──────────────────────────────────┘   │
└─────────────┬────────────────────────────┘
              │ SRB / 私有命令
              ▼
┌──────────────────────────────────────────┐
│         Miniport (stornvme.sys)          │
│  将 SRB 转换为 NVMe 命令                  │
└──────────────┬───────────────────────────┘
               │ PCIe
               ▼
          NVMe SSD
```

### Storport vs SCSIPORT 关键差异

| 特性 | SCSIPORT | Storport |
|------|----------|----------|
| 最大逻辑单元 | 8 | **4096** |
| 每 LUN 队列深度 | 254 | **64,000+** |
| 队列模型 | 单队列 | **多队列（每 NUMA 节点 / 每 CPU）** |
| 中断模型 | 传统 INTx | **MSI-X，每核独立中断向量** |
| I/O 完成 | 锁定 I/O 完成 | **无锁 I/O 完成（Lock-free）** |
| I/O 优先级 | 不支持 | **支持 (per-I/O priority)** |
| 内存分配 | 通用池 | **NUMA 感知池，预分配 DMA 缓冲区** |
| 电源管理 | 基本 | **运行时电源管理 (Runtime PM)** |

### I/O 通道模型

Storport 使用 **消息传递模型**替代旧的直接调用模型：

```c
// Miniport 将 SRB 提交到 Storport 管理的队列
StorPortNotification(BusChangeDetected, ...);
StorPortNotification(RequestComplete, ...);    // I/O 完成通知
StorPortNotification(NextRequest, ...);         // 请求下一个 SRB
StorPortNotification(WMIEvent, ...);            // WMI 事件
```

**Push Model (推送模式)**：
- Miniport 准备好接收 I/O 时调用 `StorPortNotification(NextRequest)`
- Storport 将待处理的 SRB 推送下来
- Miniport 构建硬件命令并提交到硬件队列

### 中断处理路径

```
硬件中断 (MSI-X)
    │
    ▼
ISR (中断服务例程) ─── 最小化处理，确认中断
    │
    ▼
DPC (延迟过程调用) ─── 主要完成处理
    │
    ├── 读取 CQ (Completion Queue)
    ├── 匹配 SQ 条目，提取完成状态
    ├── 更新 SRB 状态
    └── StorPortNotification(RequestComplete, Srb)
          │
          ▼
    Storport 逐层完成 IRP
```

### NUMA 感知优化

Storport 在多 NUMA 节点系统上：
- 每个 NUMA 节点拥有独立的 I/O 完成队列
- SRB 内存从目标 NUMA 节点分配
- 中断路由到 I/O 发起 CPU 所在的 NUMA 节点

```c
// Miniport 可指定 NUMA 节点
StorPortGetNodeAffinity(HwDeviceExtension, Srb, &NodeIndex);
// 从指定节点分配 DMA 缓冲区
StorPortAllocateMdl(HwDeviceExtension, ..., NodeIndex, ...);
```

## 与相关协议/组件的关系

| 关系 | 页面 | 说明 |
|------|------|------|
| ↑ 上 | [[class-drivers|类驱动 (disk.sys)]] | disk.sys 将 IRP 转为 SRB 后发给 Storport |
| ↓ 下 | [[miniport-drivers|Miniport 驱动]] | Storport 调用 Miniport 的 HwStorXxx 接口 |
| ↔ 协作 | [[../protocols/nvme|NVMe 协议]] | StorNVMe 是 NVMe 协议的 Windows Miniport 实现 |
| ↔ 开发 | [[../driver-model/storport-miniport-dev|Storport Miniport 开发]] | 如何编写 Storport Miniport 驱动 |
| @supersedes | SCSIPORT | Storport 全面取代第一代 SCSIPORT |

## 具体例子：NVMe Read 在 Storport 中的路径

```
1. disk.sys 创建 SRB_FUNCTION_EXECUTE_SCSI (READ16)
2. disk.sys 调用 StorPortBuildScatterGatherList()
3. Storport 将 SRB 入队到对应 LU 的待处理队列
4. Miniport 调用 StorPortNotification(NextRequest)
5. Storport 出队一个 SRB，递给 Miniport
6. stornvme!HwStorBuildIo() 将 READ16 转为 NVMe Read 命令
7. stornvme!HwStorStartIo() 写入 SQ Tail Doorbell
8. NVMe SSD 读取数据 → DMA 到主机内存
9. NVMe SSD 写入 CQ 条目
10. MSI-X 中断 → stornvme ISR → DPC
11. DPC 读取 CQ，调用 StorPortNotification(RequestComplete, Srb)
12. Storport 将 SRB 标记完成，逐层返回至 disk.sys
13. disk.sys 将 SRB 状态转为 IRP 状态，调用 IoCompleteRequest()
```
