---
title: NVMe（非易失性内存 Express）
type: protocol
tags: [接口协议, SSD, PCIe, Windows, StorNVMe, Storport]
created: 2026-06-10
updated: 2026-06-10
---

# NVMe（Non-Volatile Memory Express）

## 背景与动机

NVMe 是专门为 NAND Flash 等非易失性存储介质设计的主机控制器接口标准。在 NVMe 出现之前，SSD 大多通过 SATA 接口使用 AHCI（Advanced Host Controller Interface）协议，但 AHCI 是 2004 年为机械硬盘设计的，存在根本局限：仅支持单个命令队列（深度 32），每次命令提交需要多次 MMIO 寄存器读写（∼8 次），适合 HDD 的毫秒级延迟，但远跟不上 SSD 的微秒级延迟。

NVMe 充分利用 PCIe 总线的并行能力：支持 **64K 个队列**，每个队列深度 **64K**，命令提交仅需 **1 次 MMIO 写**（Doorbell 机制），将命令开销降低了一个数量级。

## 核心机制

- **多队列并行**：每个 CPU 核心可以绑定独立的提交队列（SQ）和完成队列（CQ），避免锁竞争
- **Doorbell 寄存器**：Host 写完命令后只需写入 Doorbell 通知 Controller，大幅减少 MMIO 操作
- **MSI-X 中断**：支持多达 2048 个中断向量，完成通知精确路由到提交命令的 CPU 核心
- **SGL/PRP 数据传递**：支持 Scatter-Gather List（SGL）和 Physical Region Page（PRP）两种方式描述数据缓冲区

例如，在实际测试中，NVMe SSD 的 4K 随机读 IOPS 可达 1M 以上，而 SATA SSD 通常不超过 100K，差异主要来自协议开销而非 NAND 带宽。

## Windows NVMe 驱动栈

Windows 上 NVMe 支持通过**两套驱动**实现：

```
                 用户态
    ┌───────────────┴───────────────┐
    │ 应用 I/O (ReadFile / WriteFile) │
    └───────────────┬───────────────┘
                    │ IRP
                    ▼
    ┌───────────────────────────────┐
    │     NTFS.sys / ReFS.sys       │  文件系统
    └───────────────┬───────────────┘
                    │ IRP
                    ▼
    ┌───────────────────────────────┐
    │        disk.sys               │  类驱动 (IRP → SRB)
    └───────────────┬───────────────┘
                    │ SRB
                    ▼
    ┌───────────────────────────────┐
    │      Storport.sys             │  端口驱动
    │  ┌───────────────────────┐    │
    │  │  NVMe Miniport 接口   │    │
    │  │  (HwStorBuildIo等)    │    │
    │  └───────────────────────┘    │
    └───────────────┬───────────────┘
                    │ SRB / NVMe Cmd
                    ▼
    ┌───────────────────────────────┐
    │     stornvme.sys              │  Microsoft NVMe Miniport
    │  (Windows 8.1+ 内置)          │
    │  ┌───────────────────────┐    │
    │  │ SQ/CQ 管理             │    │
    │  │ Doorbell 写入           │    │
    │  │ MSI-X 中断处理          │    │
    │  │ PRP/SGL 构建           │    │
    │  └───────────────────────┘    │
    └───────────────┬───────────────┘
                    │ PCIe MMIO
                    ▼
    ┌───────────────────────────────┐
    │        NVMe SSD               │
    └───────────────────────────────┘
```

### stornvme.sys — Microsoft NVMe Miniport 驱动

`stornvme.sys` 是 Windows 内置的 NVMe Miniport 驱动（Windows 8.1+ / Server 2012 R2+），特点：

- **符合 WHCP（Windows Hardware Compatibility Program）** 认证要求
- **Storport Miniport 模型**：实现 HwStorFindAdapter、HwStorBuildIo、HwStorStartIo 等标准接口
- **SCSI 命令转译**：将 SCSI READ16/WRITE16 转译为 NVMe Read/Write 命令、SCSI UNMAP 转译为 NVMe Deallocate
- **NVMe 1.4 支持**（Windows Server 2019+）：包括多路径（Multipathing）、命名空间管理、Sanitize

### 厂商 NVMe 驱动 vs Microsoft 内置驱动

| 维度 | Microsoft stornvme.sys | 厂商驱动 (如三星 NVMe 驱动) |
|------|----------------------|---------------------------|
| 认证 | WHCP 认证 | 可选 WHCP |
| 更新 | Windows Update 自动推送 | 需用户手动下载 |
| 功能 | 标准 NVMe 功能 | 可选厂商特有功能（魔术师工具等） |
| 稳定 | 经 Windows 全版本测试 | 视厂商测试覆盖 |
| 推荐 | Windows 默认选择 | 仅当需要厂商特有功能时 |

### NVMe Passthrough

Windows 支持通过 `IOCTL_STORAGE_QUERY_PROPERTY` 和 `IOCTL_STORAGE_FIRMWARE_ACTIVATE` 与 NVMe 设备直接通信：

```c
// 发送 NVMe Admin Command
STORAGE_PROTOCOL_COMMAND cmd = {0};
cmd.ProtocolType = ProtocolTypeNvme;
cmd.Command = NVME_ADMIN_COMMAND_IDENTIFY;
cmd.TransferLength = sizeof(NVME_IDENTIFY_CONTROLLER_DATA);

DeviceIoControl(hDevice,
    IOCTL_STORAGE_QUERY_PROPERTY,
    &query, sizeof(query),
    &identifyData, sizeof(identifyData),
    &bytesReturned, NULL);
```

## 与相关协议/组件的关系

- [[../storage-stack/port-drivers|Storport 端口驱动]] @implements — Storport 是 stornvme.sys 的上层框架
- [[../storage-stack/miniport-drivers|Miniport 驱动]] @based_on — stornvme.sys 是一个具体的 NVMe Miniport 实现
- [[../concepts/wear-leveling|磨损均衡]] @implements — NVMe Dataset Management (Deallocate) 命令承载 TRIM/UNMAP
- [[../hardware/nvme-ssd-internals|NVMe SSD 内部原理]] @based_on — NVMe 协议基于 SSD 硬件架构设计
- AHCI @supersedes — NVMe 在技术上完全取代 AHCI 作为 SSD 接口协议
- NVMe-oF @implements — NVMe over Fabrics 将 NVMe 命令集扩展到 RDMA、FC、TCP 等网络传输
