---
title: Windows I/O 管理器
type: stack-layer
tags: [I/O管理器, IRP, Windows内核, 存储栈]
created: 2026-06-10
updated: 2026-06-10
---

# Windows I/O 管理器（I/O Manager）

## 核心思想

I/O 管理器是 Windows 内核中负责 **I/O 请求全生命周期管理** 的核心组件。它将所有 I/O 操作统一建模为 **IRP（I/O Request Packet）**，并通过 **IO_STACK_LOCATION** 机制在分层驱动栈中逐层传递。I/O 管理器不关心具体设备类型——无论是磁盘读写、网络收发还是 USB 传输，都走同一套 IRP 框架。

## 典型实现

### IRP 数据结构

```
IRP {
    MdlAddress          → MDL (内存描述符列表，描述用户缓冲区物理页)
    Flags               → IRP 标志 (异步/同步、分页I/O等)
    AssociatedIrp       → 关联 IRP (用于主 IRP 关联)
    ThreadListEntry     → 线程链表
    IoStatus {
        Status          → NTSTATUS (完成状态)
        Information     → 传输字节数
    }
    UserBuffer          → 用户缓冲区指针
    Tail {
        Overlay {
            DriverContext[4]   → 驱动私有数据
            Thread            → 发起线程
            AuxiliaryBuffer   → 辅助缓冲区
        }
        CurrentStackLocation  → IO_STACK_LOCATION*
    }
}
```

### IO_STACK_LOCATION（每层驱动一个）

```
IO_STACK_LOCATION {
    MajorFunction       → IRP_MJ_READ / WRITE / DEVICE_CONTROL ...
    MinorFunction       → 子功能码
    Flags               → 控制标志
    Parameters {
        Read {
            Length          → 读取长度 (字节)
            ByteOffset      → 字节偏移 (LARGE_INTEGER)
            Key             → 文件对象键
        }
        Write { ... }
        DeviceIoControl {
            IoControlCode   → IOCTL 码
            InputBufferLength
            OutputBufferLength
        }
    }
    DeviceObject        → 目标设备对象
    FileObject          → 文件对象
    CompletionRoutine   → 完成回调 (可选)
    Context             → 回调上下文
}
```

### I/O 请求生命周期

```
1. 创建
   ├── 应用调用 ReadFile() / WriteFile() / DeviceIoControl()
   ├── NtReadFile() → 系统调用进入内核
   └── I/O 管理器调用 IoAllocateIrp() 创建 IRP

2. 分发
   ├── I/O 管理器调用 IoCallDriver(DeviceObject, Irp)
   ├── 每层驱动在自己的 Dispatch 例程中处理
   ├── 可修改 IRP 参数、设置 CompletionRoutine
   └── 调用 IoCallDriver() 传递给下层

3. 完成
   ├── 最底层驱动完成 I/O 后调用 IoCompleteRequest()
   ├── IRP 沿栈逐层返回
   ├── 每层 CompletionRoutine 被回调
   │   ├── STATUS_SUCCESS → 继续向上
   │   ├── STATUS_MORE_PROCESSING_REQUIRED → 停止传播
   │   └── 错误状态 → 上层可重试或转换
   └── 最终 I/O 管理器释放 IRP，返回用户态结果
```

### 同步 vs 异步 I/O

| 特性 | 同步 I/O | 异步 I/O (OVERLAPPED) |
|------|---------|----------------------|
| 打开方式 | `CreateFile(h, ...)` | `CreateFile(h, ..., FILE_FLAG_OVERLAPPED)` |
| 等待方式 | 内核阻塞当前线程 | 应用通过 `GetOverlappedResult()` / IOCP 等待 |
| IRP 标志 | 无异步标志 | IRP 设置完成回调 |
| 适用场景 | 简单程序、小 I/O | 高性能服务器、大并发 |
| Windows 存储栈 | 原生支持 | **推荐方式** |

### 关键 I/O 管理器函数

| 函数 | 作用 |
|------|------|
| `IoAllocateIrp()` | 分配 IRP |
| `IoCallDriver()` | 将 IRP 传递给下层驱动 |
| `IoCompleteRequest()` | 标记 IRP 完成，开始向上返回 |
| `IoSetCompletionRoutine()` | 设置完成回调 |
| `IoGetNextIrpStackLocation()` | 获取下层 IO_STACK_LOCATION 指针 |
| `IoBuildAsynchronousFsdRequest()` | 构建异步 FSD 请求 |
| `IoBuildDeviceIoControlRequest()` | 构建 IOCTL 请求 |

## 与其他层的关系

| 关系 | 页面 | 说明 |
|------|------|------|
| ↓ 下发 | [[file-system-drivers|文件系统驱动]] | I/O 管理器将 IRP 分发给 FS 驱动 |
| ↔ 协作 | [[../performance/windows-perf-tooling|ETW/WPA]] | ETW 在 I/O 管理器层埋点追踪 IRP |
| @based_on | IRP 模型 | 所有 Windows 驱动 I/O 的基础抽象 |

## 具体例子：一次异步读操作

```c
// 用户态代码
HANDLE hFile = CreateFileW(L"\\\\.\\PhysicalDrive0",
    GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
    NULL, OPEN_EXISTING,
    FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING,  // 异步 + 无缓冲
    NULL);

OVERLAPPED ol = {0};
ol.Offset = 0;           // 从 0 扇区开始
ol.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

BYTE buf[512];
ReadFile(hFile, buf, 512, NULL, &ol);  // 立即返回

// ... 做其他工作 ...

WaitForSingleObject(ol.hEvent, INFINITE);  // 等待 I/O 完成
DWORD bytesRead;
GetOverlappedResult(hFile, &ol, &bytesRead, FALSE);
```

对应的内核路径：
1. `NtReadFile()` 进入内核
2. I/O 管理器分配 IRP (IRP_MJ_READ)，设置异步标志
3. NTFS.sys 将文件偏移转换为卷扇区号
4. ... 逐层下发至 Miniport
5. StorNVMe 将命令写入 NVMe 提交队列
6. NtReadFile() 返回 STATUS_PENDING（用户态 ReadFile 返回 FALSE, GetLastError() = ERROR_IO_PENDING）
7. 硬件完成后，中断 → DPC → IoCompleteRequest() → SetEvent(ol.hEvent)
8. 应用 `WaitForSingleObject()` 返回
