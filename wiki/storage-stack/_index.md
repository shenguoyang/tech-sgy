---
title: Windows 存储栈总览
type: stack-layer
tags: [存储栈, Windows内核, IRP, 驱动]
created: 2026-06-10
updated: 2026-06-10
---

# Windows 存储栈总览

## 核心思想

Windows 存储栈采用**分层驱动模型（Layered Driver Model）**，I/O 请求以 **IRP（I/O Request Packet）** 的形式自顶向下传递，完成时沿原路返回。每一层只关心自己的职责，下层对上层透明。

## 栈层结构

```
用户态                   内核态
─────────────────────────────────────────────────
应用程序
  │ IRP
  ▼
┌──────────────────────────┐
│  I/O 管理器              │ ← IRP 分配与生命周期管理
│  IoCallDriver()          │
└──────────┬───────────────┘
           │ IRP
           ▼
┌──────────────────────────┐
│  文件系统驱动             │ ← NTFS.sys / ReFS.sys
│  (可能经 Filter Manager)  │    文件 → 卷偏移 转换
└──────────┬───────────────┘
           │ IRP
           ▼
┌──────────────────────────┐
│  卷管理器 (volmgr.sys)    │ ← 跨分区的卷管理
│                           │    动态磁盘、跨区卷
└──────────┬───────────────┘
           │ IRP
           ▼
┌──────────────────────────┐
│  分区管理器 (partmgr.sys) │ ← GPT/MBR 分区边界
│                           │    校验分区范围
└──────────┬───────────────┘
           │ IRP
           ▼
┌──────────────────────────┐
│  类驱动 (disk.sys)        │ ← 通用磁盘抽象
│                           │    IRP → SRB 转换
└──────────┬───────────────┘
           │ SRB (SCSI Request Block)
           ▼
┌──────────────────────────┐
│  端口驱动 (Storport.sys)  │ ← 高性能 SCSI 端口
│                           │    队列管理、中断处理
└──────────┬───────────────┘
           │ SRB
           ▼
┌──────────────────────────┐
│  Miniport 驱动            │ ← 硬件厂商提供
│  (stornvme.sys 等)        │    NVMe/SAS/SATA 特定
└──────────┬───────────────┘
           │ PCIe / SAS / SATA
           ▼
┌──────────────────────────┐
│  存储硬件                 │ ← NVMe SSD, SAS HDD
└──────────────────────────┘
```

## 关键设计原则

### 1. IRP 封装
所有 I/O 请求统一封装为 IRP。IRP 包含：
- **主功能码（Major Function Code）**：IRP_MJ_READ, IRP_MJ_WRITE, IRP_MJ_DEVICE_CONTROL 等
- **参数**：偏移量、长度、缓冲区
- **IO_STACK_LOCATION 数组**：每层驱动一个，层层传递

### 2. 栈式传递
每层驱动通过 `IoCallDriver()` 将 IRP 传递给下层。下层完成时通过 `IoCompleteRequest()` 将结果沿原路返回。每层可以：
- 直接透传
- 修改 IRP 参数后传递
- 自行完成（如有缓存命中）

### 3. 异步 I/O
Windows 存储栈原生支持异步 I/O。IRP 设置完成回调（Completion Routine），发起 I/O 后立即返回，I/O 完成时内核回调通知。

### 4. Storport 的高性能设计
Storport 是 Windows 2003 引入的第三代存储端口驱动，专为高性能设计：
- 支持最多 **64K 队列**，每队列深度 **64K**
- **MSI-X** 多核中断路由
- **NUMA 感知** 的内存分配
- **I/O 优先级** 感知

## 与其他层的关系

| 方向 | 页面 | 关系 |
|------|------|------|
| ↑ 上 | [[../filesystems/ntfs|NTFS]] | NTFS 向 I/O 管理器下发 IRP |
| ↓ 下 | [[../hardware/nvme-ssd-internals|NVMe SSD 内部原理]] | Miniport 通过 PCIe/NVMe 与硬件通信 |
| ↔ 平 | [[port-drivers|Storport 端口驱动]] | 存储栈中的性能关键层 |
| ↔ 平 | [[i-o-manager|I/O 管理器]] | IRP 生命周期管理的核心 |

## 典型 I/O 路径示例

以一次 `ReadFile()` 调用为例：

1. 应用调用 `ReadFile(hFile, buf, 4096, &bytesRead, NULL)`
2. 用户态 → 内核态系统调用 (syscall)
3. I/O 管理器创建 IRP (IRP_MJ_READ)，分配 IO_STACK_LOCATION
4. 文件系统驱动 (NTFS.sys)：将文件偏移转换为卷扇区号
5. 卷管理器 (volmgr.sys)：将卷扇区映射到物理磁盘偏移
6. 分区管理器 (partmgr.sys)：校验偏移在分区范围内
7. 类驱动 (disk.sys)：将 IRP 转换为 SRB
8. 端口驱动 (Storport.sys)：将 SRB 入队到对应硬件队列
9. Miniport 驱动 (stornvme.sys)：构建 NVMe 命令，写入 Doorbell 寄存器
10. NVMe SSD 执行读操作，完成后发送 MSI-X 中断
11. 中断处理 → DPC → 逐层完成 → 应用 `ReadFile()` 返回
