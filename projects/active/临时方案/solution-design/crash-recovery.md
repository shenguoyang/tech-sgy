---
title: 崩溃恢复 — 启动恢复与异常场景处理
type: output
tags: [存储方案, 崩溃恢复, 数据完整性, 异常处理]
created: 2026-06-14
updated: 2026-06-14
---

# 崩溃恢复 — 启动恢复与异常场景处理

> 详细描述每个异常场景的检测、恢复和降级策略。

---

## 启动恢复流程

`StorageEngine::Initialize()` 在每次启动时执行，无论上次是否正常关闭。

### 完整流程

```cpp
bool StorageEngine::Initialize(const wchar_t* ring_path,
                               const wchar_t* data_root,
                               const wchar_t* archive_root) {
    // ═══ 步骤1: 打开或创建 Ring Buffer 文件 ═══
    hRingFile_ = CreateFileW(ring_path,
        GENERIC_READ | GENERIC_WRITE,
        0,                          // dwShareMode=0 独占
        NULL,                       // 默认安全描述符
        OPEN_ALWAYS,                // 存在则打开，不存在则创建
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING,
        NULL);

    if (hRingFile_ == INVALID_HANDLE_VALUE) {
        return false;  // 无法打开文件
    }

    // ═══ 步骤2: 判断是新文件还是已有文件 ═══
    LARGE_INTEGER file_size;
    GetFileSizeEx(hRingFile_, &file_size);

    if (file_size.QuadPart == 0) {
        // ── 新文件: 预分配 + 初始化 ──
        return initialize_new_file();
    } else {
        // ── 已有文件: 崩溃恢复扫描 ──
        return recover_existing_file();
    }
}
```

### 新文件初始化

```cpp
bool initialize_new_file() {
    // 1. 计算总大小
    total_size_ = HEADER_SIZE
                + static_cast<uint64_t>(BLOCK_META_SIZE) * block_count_
                + static_cast<uint64_t>(block_size_) * block_count_;

    // 2. 预分配（瞬时完成）
    // 方案A: 有 SE_MANAGE_VOLUME_NAME 权限
    if (!SetFileValidData(hRingFile_, total_size_)) {
        // 方案B: 回退方案 — SetEndOfFile + 分块写零（64MB Overlapped I/O）
        fallback_preallocate(total_size_);
    }

    // 3. 初始化 Header
    RingHeader header = {};
    header.magic       = RING_MAGIC;
    header.version     = RING_VERSION;
    header.created_at  = get_filetime_now();
    header.block_size  = block_size_;
    header.block_count = block_count_;
    header.write_cursor     = 0;
    header.committed_seq    = 0;
    header.materialized_seq = 0;
    header.crc32 = crc32c(&header, offsetof(RingHeader, crc32));

    WriteFile(hRingFile_, &header, sizeof(RingHeader), &written, NULL);

    // 4. 初始化 BlockMeta 区为零（所有Block标记为FREE）
    //    可以稀疏写入——只写第一个BlockMeta表示"全是零"
    //    或：依赖 SetFileValidData 的零填充

    // 5. 分配内存中的 meta_array_
    meta_array_ = (BlockMeta*)_aligned_malloc(
        BLOCK_META_SIZE * block_count_, SECTOR_SIZE);
    memset(meta_array_, 0, BLOCK_META_SIZE * block_count_);

    // 6. 创建物化根目录
    CreateDirectoryW(data_root_, NULL);
    CreateDirectoryW(archive_root_, NULL);

    return true;
}
```

### 已有文件恢复

```cpp
bool recover_existing_file() {
    // ═══ 步骤1: 读取 Header ═══
    RingHeader header;
    OVERLAPPED ov = {};
    DWORD read = 0;
    ReadFile(hRingFile_, &header, sizeof(RingHeader), &read, &ov);

    bool header_valid = true;

    // 校验 magic
    if (header.magic != RING_MAGIC) {
        header_valid = false;
    }
    // 校验 version
    if (header.version != RING_VERSION) {
        // 可在此实现版本升级逻辑
        header_valid = false;
    }
    // 校验 crc32
    uint32_t expected_crc = crc32c(&header, offsetof(RingHeader, crc32));
    if (header.crc32 != expected_crc) {
        header_valid = false;
    }

    uint32_t block_count = header.block_count;
    uint64_t block_size  = header.block_size;
    uint64_t committed_seq;
    uint32_t write_cursor;

    // ═══ 步骤2: 读取 BlockMeta 数组 ═══
    BlockMeta* meta_array = (BlockMeta*)_aligned_malloc(
        BLOCK_META_SIZE * block_count, SECTOR_SIZE);
    // ... ReadFile 读入全部BlockMeta ...

    // ═══ 步骤3: Header有效则依Header，否则全量扫描 ═══
    if (header_valid) {
        committed_seq = header.committed_seq;
        write_cursor  = header.write_cursor;
    } else {
        // 回退: 从 BlockMeta 全量扫描重建
        committed_seq = 0;
        for (uint32_t i = 0; i < block_count; i++) {
            BlockMeta* meta = &meta_array[i];
            if (meta->flags == FLAG_WRITTEN || meta->flags == FLAG_MATERIALIZED) {
                if (meta->data_crc32 != 0) {
                    // 验证CRC
                    uint64_t block_offset = HEADER_SIZE
                        + BLOCK_META_SIZE * block_count
                        + i * block_size;
                    size_t check_size = ((meta->actual_size + SECTOR_SIZE - 1)
                                        / SECTOR_SIZE) * SECTOR_SIZE;
                    uint8_t* check_buf = (uint8_t*)_aligned_malloc(check_size, SECTOR_SIZE);
                    // ... ReadFile(block_offset, check_buf, check_size) ...
                    uint32_t computed = crc32c(check_buf, check_size);
                    _aligned_free(check_buf);

                    if (computed == meta->data_crc32) {
                        if (meta->sequence > committed_seq) {
                            committed_seq = meta->sequence;
                        }
                    } else {
                        // CRC不匹配，标记损坏
                        meta->flags = FLAG_FREE;
                        corrupt_count_++;
                    }
                }
            }
        }
        // 回写重建的Header
        write_cursor = find_first_free_cursor(meta_array, block_count);
        rebuild_and_write_header(committed_seq, write_cursor, block_count);
    }

    // ═══ 步骤4: 依 committed_seq 修正BlockMeta ═══
    for (uint32_t i = 0; i < block_count; i++) {
        BlockMeta* meta = &meta_array[i];
        if (meta->flags == FLAG_WRITTEN) {
            // 校验CRC
            // ... (同步骤3的CRC校验逻辑) ...
            if (crc_ok && meta->sequence > committed_seq) {
                // 未提交的Block → 标记FREE
                meta->flags = FLAG_FREE;
            }
        }
    }

    // ═══ 步骤5: 重新物化缺失的文件 ═══
    uint64_t materialized_seq = header_valid ? header.materialized_seq : 0;
    for (uint32_t i = 0; i < block_count; i++) {
        BlockMeta* meta = &meta_array[i];
        if (meta->flags == FLAG_WRITTEN && meta->sequence > materialized_seq) {
            // Ring Buffer中有数据，但目录树可能缺失 → 重新物化
            materialize_block(i, meta);
        }
    }

    // ═══ 步骤6: 启动后台线程 ═══
    start_materializer_thread();
    start_lifecycle_thread();
    start_stats_thread();

    return true;
}
```

---

## 异常场景处理

### 场景1：突然断电

```
恢复流程:
  1. Initialize() → recover_existing_file()
  2. Header校验 → 通过或从Meta重建
  3. committed_seq 之后的Block → 标记FREE（丢弃未提交数据）
  4. CRC不匹配的Block → 标记FREE（丢弃损坏数据）
  5. 已提交但未物化的Block → 重新物化
  
恢复结果:
  已提交数据（committed_seq之前）: ✅ 100%恢复
  未提交数据（committed_seq之后）: ❌ 丢弃（应用层重传）
```

### 场景2：系统蓝屏（BSOD）

```
与断电场景完全相同。
NTFS 保证元数据一致性（$LogFile），但 Ring Buffer 是单文件预分配，
运行时无元数据变更（仅 inode 时间戳更新），影响极小。
```

### 场景3：磁盘空间不足

```
检测:
  - Ring Buffer: 已在启动时预分配，运行时不检测
  - 物化目录: WriteFile 前调用 GetDiskFreeSpaceExW
  
处理:
  1. 物化目录剩余 < 5%:
     → 暂停 Materializer
     → 触发告警（backpressure_active = true）
     → 写入路径不受影响（Ring Buffer 继续吸收数据）
  
  2. Ring Buffer 环形覆盖时发现未物化Block:
     → 强制同步物化（可能因磁盘满失败）
     → 触发 CRITICAL 告警
     → 对应用层返回 WRITE_REJECTED_DISK_FULL
```

### 场景4：应用崩溃

```
恢复流程:
  应用重启 → Initialize() → recover_existing_file()
  
  与断电场景相同的恢复流程:
  - 未提交Block → FREE
  - 已提交Block → 保留 + 重新物化
  
  优势:
  - 应用崩溃不会破坏 Ring Buffer 数据的完整性
  - 物化目录中的 .tmp 文件会在物化重启时通过 ReplaceFileW 覆盖
```

### 场景5：Ring Buffer 文件损坏

```
检测:
  1. Header → magic/crc32 校验
  2. BlockMeta → sequence合理性, crc32校验
  3. Block数据 → data_crc32 校验
  
恢复:
  - Header损坏 → 从BlockMeta全量扫描重建
  - BlockMeta个别损坏 → 该Block标记FREE
  - Block数据个别损坏 → 该Block标记FREE (data_crc32不匹配)
  - 大面积损坏 → 标记所有受损Block为FREE → 报告corrupt_blocks_detected
```

### 场景6：物化目录被手动删除

```
检测:
  Materializer物化时若发现目标目录不存在 → 重新创建
  
  或者:
  Stats线程发现 warm_total_files 与 ring_materialized_blocks 不匹配
  → 触发全量重新物化（遍历所有 MATERIALIZED Block，检查对应文件是否存在）
```

---

## 数据完整性层次

```
                         ┌─────────────────────────┐
  committed_seq 之前     │  已提交数据              │
  + CRC32 校验通过       │  ✅ 永不可丢失           │
                         │  断电/崩溃/蓝屏 100% 恢复 │
                         └─────────────────────────┘

                         ┌─────────────────────────┐
  committed_seq 之后     │  未提交数据              │
  或 CRC32 校验失败      │  ❌ 丢弃                 │
                         │  应用层感知错误码并重传   │
                         └─────────────────────────┘

                         ┌─────────────────────────┐
  materialized_seq 之前  │  已物化数据              │
  + 物化目录文件存在     │  ✅ Explorer 可见        │
                         │  ✅ 可复制粘贴拖拽        │
                         └─────────────────────────┘
```

---

## 恢复性能

```
恢复步骤                    时间估算 (128 Block, 64GB)
─────────────────────────────────────────────────────
读取Header                    < 1ms
读取全部BlockMeta (128×128B)  < 1ms (16KB顺序读)
遍历BlockMeta + CRC校验        < 10ms (纯计算)
重新物化缺失文件              0-N个文件 × 每个~100ms
─────────────────────────────────────────────────────
总计（无重新物化）             < 12ms
总计（100个Block重新物化）     < 10s
```

CRC32校验可在恢复阶段延迟执行（仅在校验时才读取Block数据），或复用SSE4.2硬件加速指令。

---

## 参考

- [[design-overview]] — 方案总览
- [[write-path]] — 写入路径与崩溃点分析
- [[core-data-structures.h]] — 核心数据结构
- [[requirement-fulfillment]] — R3数据分析
