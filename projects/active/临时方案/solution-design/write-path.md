---
title: 写入路径 — 完整流程与崩溃点分析
type: output
tags: [存储方案, 写入路径, 崩溃恢复, Direct I/O]
created: 2026-06-14
updated: 2026-06-14
---

# 写入路径 — 完整流程与崩溃点分析

> 详细描述从应用调用 `Write()` 到数据"已提交"的完整过程，以及每个步骤的崩溃影响。

---

## 完整写入流程

### 步骤0：应用调用

```cpp
WriteRequest req;
req.data        = image_buffer;
req.size        = image_size;      // 例如 15MB
req.file_path   = "VIN12345/Front/cam01_001.jpg";
req.reliability = RELIABILITY_CRITICAL;

WriteStatus status = engine->Write(req);
// 返回 OK 时，数据已安全落入 Ring Buffer（但物化目录树可能尚未可见）
```

### 步骤1：两路分流

```cpp
WriteStatus StorageEngine::Write(const WriteRequest& req) {
    // 临时数据：直接写 TEMP 目录，不走 Ring Buffer
    if (req.reliability == RELIABILITY_TEMP) {
        return write_to_temp_dir(req);
    }

    // 小文件路由：Write Buffer 批量聚合
    if (req.size < WRITE_BUFFER_BATCH) {  // < 64KB
        return write_via_buffer(req);
    }

    // 大文件路由：直接 Direct I/O
    return write_direct(req);
}
```

### 步骤2-WB：Write Buffer 批量聚合（小文件路径）

```cpp
WriteStatus write_via_buffer(const WriteRequest& req) {
    WriteBuffer& wb = write_buffer_;

    // 追加到批次缓冲区
    // 格式: [path_len(2B)][file_path][data_size(4B)][data_bytes]
    wb.append(req.file_path, req.data, req.size, req.reliability);
    batch_count_++;

    WriteStatus status = WriteStatus::OK;

    // 触发条件：缓冲区满 或 超时
    if (wb.size() >= WRITE_BUFFER_BATCH || wb.age_ms() > WRITE_BUFFER_FLUSH_MS) {
        status = flush_write_buffer();
    }

    return status;
}

WriteStatus flush_write_buffer() {
    WriteBuffer& wb = write_buffer_;

    // 扇区对齐：向上取整到512B，尾部填零
    size_t aligned_size = ((wb.size() + SECTOR_SIZE - 1) / SECTOR_SIZE) * SECTOR_SIZE;
    uint8_t* aligned_buf = (uint8_t*)_aligned_malloc(aligned_size, SECTOR_SIZE);

    memcpy(aligned_buf, wb.data(), wb.size());
    memset(aligned_buf + wb.size(), 0, aligned_size - wb.size());

    // 聚合为一个Block，路径记录为 "__batch_NNN__"
    // 物化时扫描批次内容，逐个拆分为独立文件
    char batch_path[80];
    snprintf(batch_path, sizeof(batch_path), "__batch_%llu__", next_batch_id_++);

    WriteStatus status = commit_block(aligned_buf, aligned_size,
                                      wb.size(),  // actual_size = 原始数据大小
                                      batch_path,
                                      RELIABILITY_CRITICAL); // 批次统一为CRITICAL

    _aligned_free(aligned_buf);
    wb.reset();
    batch_count_ = 0;
    return status;
}
```

### 步骤2-D：Direct I/O（大文件路径）

```cpp
WriteStatus write_direct(const WriteRequest& req) {
    // 扇区对齐
    size_t aligned_size = ((req.size + SECTOR_SIZE - 1) / SECTOR_SIZE) * SECTOR_SIZE;
    uint8_t* aligned_buf = (uint8_t*)_aligned_malloc(aligned_size, SECTOR_SIZE);

    memcpy(aligned_buf, req.data, req.size);
    memset(aligned_buf + req.size, 0, aligned_size - req.size);

    WriteStatus status = commit_block(aligned_buf, aligned_size,
                                      req.size,
                                      req.file_path,
                                      req.reliability);

    _aligned_free(aligned_buf);
    return status;
}
```

### 步骤3：commit_block — 核心提交逻辑

```cpp
WriteStatus commit_block(uint8_t* aligned_buf, size_t aligned_size,
                         size_t actual_size, const char* file_path,
                         uint8_t reliability) {

    // ═══ 3a: 准备 BlockMeta ═══
    BlockMeta meta = {};
    meta.sequence    = InterlockedIncrement64(&global_seq_);  // 原子递增
    meta.timestamp   = get_filetime_now();
    meta.actual_size = static_cast<uint32_t>(actual_size);
    meta.file_path_hash = fnv1a_64(file_path);
    strncpy_s(meta.file_path, file_path, sizeof(meta.file_path) - 1);
    meta.reliability = reliability;
    meta.flags       = FLAG_WRITTEN;
    meta.data_crc32  = crc32c(aligned_buf, aligned_size);

    // ═══ 3b: 检查背压 ═══
    double usage = ring_usage_pct();
    if (usage >= BACKPRESSURE_REJECT_PCT) {
        if (reliability == RELIABILITY_TEMP) return WriteStatus::REJECTED_TEMP;
        if (reliability == RELIABILITY_NORMAL) return WriteStatus::REJECTED_DISK_FULL;
    }

    // ═══ 3c: 环形覆盖检查 ═══
    uint32_t cursor = write_cursor_;
    BlockMeta* old_meta = &meta_array_[cursor];
    if (old_meta->flags != FLAG_FREE && old_meta->flags != FLAG_MATERIALIZED) {
        // 要被覆盖的Block尚未物化 — 强制同步物化
        materialize_block(cursor, old_meta);
    }

    // ═══ 3d: 计算偏移 ═══
    uint64_t block_offset = HEADER_SIZE
                          + static_cast<uint64_t>(BLOCK_META_SIZE) * block_count_
                          + static_cast<uint64_t>(cursor) * block_size_;

    uint64_t meta_offset = HEADER_SIZE
                         + static_cast<uint64_t>(cursor) * BLOCK_META_SIZE;

    // ═══ 3e: 三步 Direct I/O 写入 ═══
    // 注意：必须严格按此顺序写入！

    // —— 子步骤 3e-1: 写入 Block 数据 ——
    // 【崩溃点A】如果此时崩溃：Meta仍为FREE，该Block视为空闲
    OVERLAPPED ov_data = {};
    ov_data.Offset     = static_cast<DWORD>(block_offset & 0xFFFFFFFF);
    ov_data.OffsetHigh = static_cast<DWORD>(block_offset >> 32);
    DWORD written_data = 0;
    BOOL ok = WriteFile(hRingFile_, aligned_buf,
                        static_cast<DWORD>(aligned_size),
                        &written_data, &ov_data);
    if (!ok || written_data != aligned_size) {
        return WriteStatus::ERROR_ALIGNMENT;
    }

    // —— 子步骤 3e-2: 写入 BlockMeta ——
    // 【崩溃点B】如果此时崩溃：Block数据存在但Meta损坏或不完整
    //   恢复：CRC32校验失败 → 标记FREE
    OVERLAPPED ov_meta = {};
    ov_meta.Offset     = static_cast<DWORD>(meta_offset & 0xFFFFFFFF);
    ov_meta.OffsetHigh = static_cast<DWORD>(meta_offset >> 32);
    DWORD written_meta = 0;
    ok = WriteFile(hRingFile_, &meta, sizeof(BlockMeta), &written_meta, &ov_meta);
    if (!ok || written_meta != sizeof(BlockMeta)) {
        return WriteStatus::ERROR_ALIGNMENT;
    }

    // —— 子步骤 3e-3: 更新 RingHeader（COMMIT 点） ——
    // 【崩溃点C】如果此时崩溃：数据+Meta完整但Header未更新
    //   恢复：Header.committed_seq 不包含该sequence → 视为未提交
    header_.committed_seq = meta.sequence;
    header_.write_cursor  = (cursor + 1) % block_count_;
    header_.crc32         = crc32c(&header_, offsetof(RingHeader, crc32));

    OVERLAPPED ov_hdr = {};
    DWORD written_hdr = 0;
    ok = WriteFile(hRingFile_, &header_, sizeof(RingHeader), &written_hdr, &ov_hdr);
    if (!ok || written_hdr != sizeof(RingHeader)) {
        return WriteStatus::ERROR_ALIGNMENT;
    }

    // ═══ 3f: 更新内存状态 ═══
    write_cursor_ = header_.write_cursor;
    // 注意：meta_array_[cursor] 已在步骤3e-2写入磁盘
    // 内存中需同步更新
    memcpy(&meta_array_[cursor], &meta, sizeof(BlockMeta));

    // ═══ 3g: 信号量通知 Materializer ═══
    ReleaseSemaphore(hMaterializerSem_, 1, NULL);

    // ═══ 3h: 更新性能统计 ═══
    update_write_stats(meta.sequence, actual_size, /* latency */);

    return WriteStatus::OK;
}
```

---

## 崩溃点逐项分析

### 崩溃点A：Block数据写入中（步骤3e-1）

```
时序：
  磁盘: [Header √] [BlockMeta[0..N-1] √] [Block[0] √] ... [Block[cursor] ××损坏××]
  
恢复行为:
  BlockMeta[cursor].flags 仍为 FREE（3e-2未执行）
  → 该Block视为空闲，可被后续写入覆盖
  
数据结果: ✅ 该次写入丢弃，应用层收到错误码后重传
```

### 崩溃点B：BlockMeta写入中/Block数据写完但Meta未写

```
时序：
  磁盘: [Header √] [BlockMeta[0..N-1] √] [Block[cursor] √数据完整]
        但 BlockMeta[cursor] 可能是旧值(flags=FREE) 或 新值(flags=WRITTEN但中途损坏)
  
恢复行为:
  如果 flags==FREE → 该Block空闲 ✅
  如果 flags==WRITTEN 但 data_crc32 不匹配 → 标记FREE ✅
  如果 flags==WRITTEN 且 data_crc32 匹配但 sequence > committed_seq → 标记FREE ✅
  
数据结果: ✅ 未提交，丢弃
```

### 崩溃点C：Header写入中

```
时序：
  磁盘: [Header ××损坏或旧值××] [BlockMeta[cursor]=WRITTEN完整] [Block[cursor]数据完整]
  
恢复行为:
  读取Header → magic或crc32校验失败
  → 回退模式：遍历全部BlockMeta
  → 找flags==WRITTEN且data_crc32有效的最大sequence
  → 重建Header: committed_seq = 最大有效sequence
  → Block[cursor]的sequence是有效的 → commit有效 ✅
  
数据结果: ✅ 该Block成功恢复，视为已提交
```

### 崩溃点D：全部完成

```
时序：
  磁盘: [Header √ (committed_seq已更新)] [BlockMeta[cursor]=WRITTEN] [Block[cursor]数据]
  
恢复行为:
  正常恢复，committed_seq包含该sequence
  → 该Block视为已提交
  → 如果 materialized_seq < committed_seq → 触发物化
  
数据结果: ✅ 数据完整，等待物化
```

---

## 写入顺序的严格性

```
必须严格按照: 数据 → Meta → Header

原因:
  1. 如果先写Meta → Meta有效但数据可能是旧的 → CRC32校验会捕获
  2. 如果先写Header → Header已提交但Meta/Data尚未写入 → 读取该Block时发现数据损坏
  3. 只有数据→Meta→Header的顺序，才能保证：
     - Header未更新 = 未提交（安全丢弃）
     - Header已更新 = 数据和Meta都已完整（安全读取）
```

---

## 关键 Windows API

| API | 用途 | 关键参数 |
|-----|------|---------|
| `CreateFileW` | 打开Ring Buffer文件 | `FILE_FLAG_NO_BUFFERING`, `dwShareMode=0` |
| `SetFileValidData` | 预分配文件空间 | 需要 `SE_MANAGE_VOLUME_NAME` 权限 |
| `_aligned_malloc` | 分配扇区对齐缓冲区 | `alignment=512` |
| `WriteFile` | Direct I/O写入 | `OVERLAPPED` 结构指定偏移 |
| `FlushFileBuffers` | 无需调用 | `FILE_FLAG_NO_BUFFERING` 已绕过缓存，写入即持久化 |
| `ReplaceFileW` | 物化时原子重命名 | 替换临时文件为正式文件名 |

---

## 性能关键路径分析

```
典型20MB图像写入延迟分解:

  步骤1 (分流判断):      < 0.01ms  (纯计算)
  步骤2 (扇区对齐memset): ~0.05ms  (20MB填零到512B边界)
  步骤3a (CRC32计算):    ~0.5ms   (SSE4.2硬件加速, 20MB)
  步骤3b (背压检查):      < 0.01ms  (纯计算)
  步骤3d (偏移计算):      < 0.01ms  (纯计算)
  步骤3e-1 (WriteFile数据): ~7ms   (DMA 20MB @ 3GB/s)
  步骤3e-2 (WriteFile Meta): ~0.05ms (128B, 几乎瞬间)
  步骤3e-3 (WriteFile Header): ~0.1ms (4KB)
  ──────────────────────────────────
  总计: ~7.7ms  ✅ P99 < 10ms
```

---

## 参考

- [[design-overview]] — 方案总览
- [[core-data-structures.h]] — 核心数据结构
- [[crash-recovery]] — 崩溃恢复详细流程
- [[requirement-fulfillment]] — R3数据分析
