---
title: 核心数据结构
type: output
tags: [存储方案, C++, 数据结构, 头文件]
created: 2026-06-14
updated: 2026-06-14
---

# 核心数据结构

> 环形缓冲摄入 + 异步目录物化 方案的核心数据结构定义。

---

## 常量定义

```cpp
// ═══════════════════════════════════════════
// 文件格式常量
// ═══════════════════════════════════════════
constexpr uint32_t RING_MAGIC          = 0x52494E47;  // "RING"
constexpr uint32_t RING_VERSION        = 1;
constexpr uint32_t HEADER_SIZE         = 4096;        // 4KB, 扇区对齐
constexpr uint32_t BLOCK_META_SIZE     = 128;         // 每条BlockMeta 128B
constexpr uint32_t DEFAULT_BLOCK_SIZE  = 512 * 1024 * 1024; // 512 MB
constexpr uint32_t WRITE_BUFFER_BATCH  = 64 * 1024;   // 小文件聚合阈值 64KB
constexpr uint32_t WRITE_BUFFER_FLUSH_MS = 100;       // 聚合超时 100ms
constexpr uint32_t SECTOR_SIZE         = 512;         // 磁盘扇区大小

// ═══════════════════════════════════════════
// Block标志位
// ═══════════════════════════════════════════
constexpr uint8_t FLAG_FREE            = 0;  // 空闲, 可写入
constexpr uint8_t FLAG_WRITTEN         = 1;  // 已写入, 等待物化
constexpr uint8_t FLAG_MATERIALIZED    = 2;  // 已物化, 目录树可见

// ═══════════════════════════════════════════
// 可靠性等级
// ═══════════════════════════════════════════
constexpr uint8_t RELIABILITY_CRITICAL = 0;  // 关键数据, 不可丢失
constexpr uint8_t RELIABILITY_NORMAL   = 1;  // 普通数据, 可接受偶发丢失
constexpr uint8_t RELIABILITY_TEMP     = 2;  // 临时数据, 可丢弃

// ═══════════════════════════════════════════
// 背压状态
// ═══════════════════════════════════════════
constexpr double BACKPRESSURE_WARN_PCT     = 80.0;  // 温和告警
constexpr double BACKPRESSURE_REJECT_PCT   = 95.0;  // 拒绝临时/普通
```

---

## 文件头 (RingHeader)

```cpp
#pragma pack(push, 1)
struct RingHeader {
    uint32_t magic;               // RING_MAGIC "RING"
    uint32_t version;             // 格式版本号
    uint64_t created_at;          // FILETIME, 创建时间
    uint64_t block_size;          // 单Block最大字节数 (如512MB)
    uint32_t block_count;         // Block总数
    uint32_t write_cursor;        // 下一个要写入的Block索引 (0..N-1)
    uint64_t committed_seq;       // 最后提交的全局序列号
    uint64_t materialized_seq;    // 最后物化完成的序列号
    uint32_t crc32;               // Header自身CRC32 (不含crc32字段)
    uint8_t  reserved[4048];      // 填充到4096B
};
static_assert(sizeof(RingHeader) == 4096, "RingHeader must be 4KB, sector-aligned");
#pragma pack(pop)
```

### 字段说明

| 字段 | 说明 |
|------|------|
| `magic` | 魔数 0x52494E47 ("RING")，用于识别文件格式 |
| `version` | 格式版本号，用于向前兼容 |
| `created_at` | 文件创建时间（Windows FILETIME） |
| `block_size` | 单个Block的最大字节数（固定值，创建时确定） |
| `block_count` | Block总数 = Ring Buffer 大小 / block_size |
| `write_cursor` | 环形写入指针，写完一圈后回到0 |
| `committed_seq` | 崩溃恢复的"提交边界"——大于此seq的Block视为未提交 |
| `materialized_seq` | 已物化到目录树的最大序列号 |
| `crc32` | Header前60字节的CRC32校验（不含crc32字段和reserved） |

---

## Block元数据 (BlockMeta)

```cpp
#pragma pack(push, 1)
struct BlockMeta {
    uint64_t sequence;            // 全局单调递增序列号 (1, 2, 3, ...)
    uint64_t timestamp;           // FILETIME 写入时间
    uint32_t actual_size;         // 实际数据字节数 (≤ block_size)
    uint64_t file_path_hash;      // FNV-1a 64bit hash of file_path (快速查找)
    char     file_path[80];       // 相对路径, null-terminated
                                  // e.g. "VIN12345/Front/cam01_001.jpg"
    uint8_t  reliability;         // 0=关键, 1=普通, 2=临时
    uint8_t  flags;               // 0=空闲, 1=已写入, 2=已物化
    uint16_t padding;             // 对齐填充
    uint32_t data_crc32;          // Block数据区CRC32校验
    uint8_t  reserved[12];        // 预留扩展 (以后可能需要 SHA256 等)
};
static_assert(sizeof(BlockMeta) == 128, "BlockMeta must be 128B");
#pragma pack(pop)
```

### 字段说明

| 字段 | 说明 |
|------|------|
| `sequence` | 全局单调递增。崩溃恢复时据此判断提交边界 |
| `timestamp` | 文件写入时间，用于时间范围查询 |
| `actual_size` | 实际数据大小，物化时据此截断对齐填充 |
| `file_path_hash` | FNV-1a hash，用于 O(1) 快速查找 |
| `file_path` | 相对路径，物化时拼接为 `data_root + file_path` |
| `reliability` | 三档可靠性标识 |
| `flags` | Block生命周期状态机：FREE → WRITTEN → MATERIALIZED |
| `data_crc32` | 数据区CRC32，启动时校验数据完整性 |

---

## 文件布局

```
┌───────────────────────────────────────────────┐  offset 0
│  RingHeader (4KB, 扇区对齐)                     │
│  magic | version | block_size | block_count     │
│  write_cursor | committed_seq | ...             │
├───────────────────────────────────────────────┤  offset 4096
│  BlockMeta[0] (128B)                           │
│  BlockMeta[1] (128B)                           │
│  ...                                           │
│  BlockMeta[N-1] (128B)                         │
├───────────────────────────────────────────────┤  offset 4096 + N×128
│  Block[0] Data (≤ block_size, 512B对齐)         │
│  Block[1] Data                                 │
│  ...                                           │
│  Block[N-1] Data                               │
└───────────────────────────────────────────────┘  offset 4096 + N×128 + N×aligned_block_size

Block数据区偏移计算:
  block_offset[i] = HEADER_SIZE + BLOCK_META_SIZE * block_count + i * block_size

总文件大小:
  total_size = HEADER_SIZE + BLOCK_META_SIZE * block_count + block_size * block_count
```

### 对齐保证

- `HEADER_SIZE = 4096 = 8 × 512` → ✅ 扇区对齐
- `BLOCK_META_SIZE * block_count` 在创建时对齐到512B边界 → ✅ 扇区对齐
- `block_size` 是512B的整数倍 → ✅ 扇区对齐
- 所有Block数据区偏移自然满足 `offset % 512 == 0`

---

## 应用层写入请求 (WriteRequest)

```cpp
struct WriteRequest {
    const void*  data;            // 数据首地址
    uint32_t     size;            // 实际字节数
    const char*  file_path;       // 目标相对路径
                                  // e.g. "VIN12345/Front/cam01_001.jpg"
    uint8_t      reliability;     // RELIABILITY_CRITICAL / NORMAL / TEMP
};
```

---

## 可观测性统计 (EngineStats)

```cpp
struct EngineStats {
    // ═══ Ring Buffer 状态 ═══
    uint64_t ring_total_bytes;           // 预分配总大小
    uint64_t ring_used_bytes;            // 已写入(未物化)数据量
    uint32_t ring_block_count;           // Block总数
    uint32_t ring_written_blocks;        // flags==WRITTEN 的数量
    uint32_t ring_materialized_blocks;   // flags==MATERIALIZED 的数量
    double   ring_usage_pct;             // 使用率 (0.0~100.0)
    uint64_t materialize_lag_ms;         // 物化积压时间

    // ═══ 物化目录状态 ═══
    uint64_t warm_total_files;           // D:\Data\ 下文件总数
    uint64_t warm_total_bytes;           // D:\Data\ 下总字节数
    uint64_t warm_total_dirs;            // D:\Data\ 下目录总数
    uint64_t cold_total_files;           // E:\Archive\ 下文件总数
    uint64_t cold_total_bytes;           // E:\Archive\ 下总字节数

    // ═══ 文件类型分布 ═══
    uint64_t count_jpg, bytes_jpg;
    uint64_t count_pcd, bytes_pcd;
    uint64_t count_json, bytes_json;
    uint64_t count_other, bytes_other;

    // ═══ 性能指标 (滑动窗口,最近1000次写入) ═══
    double   write_latency_p50_us;       // 中位数延迟
    double   write_latency_p99_us;       // P99 延迟
    double   write_latency_p999_us;      // P999 延迟
    double   write_throughput_mbps;      // 最近1秒吞吐量

    // ═══ 异常标记 ═══
    uint32_t large_dir_count;            // 文件数>10000的目录数
    uint32_t crash_recovery_count;       // 历史崩溃恢复次数
    uint32_t corrupt_blocks_detected;    // CRC32检测到的损坏Block数
    bool     backpressure_active;        // 当前是否处于背压状态
    uint8_t  backpressure_level;         // 0=正常, 1=温和, 2=强力

    // ═══ 磁盘空间 ═══
    uint64_t ssd_free_bytes;
    uint64_t ssd_total_bytes;
    uint64_t hdd_free_bytes;
    uint64_t hdd_total_bytes;
};
```

---

## 写入状态枚举

```cpp
enum class WriteStatus : uint8_t {
    OK                  = 0,  // 写入成功，已提交到Ring Buffer
    BACKPRESSURE        = 1,  // 背压中，但数据已写入（建议应用减速）
    REJECTED_TEMP       = 2,  // 拒绝临时数据（Ring Buffer使用率 > 95%）
    REJECTED_DISK_FULL  = 3,  // 拒绝——磁盘空间不足
    ERROR_CRC_MISMATCH  = 4,  // 写入后CRC校验失败（非常罕见，建议重试）
    ERROR_ALIGNMENT     = 5,  // 缓冲区或偏移未对齐（编程错误）
};
```

---

## File Path Hash (FNV-1a 64bit)

```cpp
// FNV-1a 64bit hash — 用于 file_path 快速等值查找
// 不用于加密目的，仅用于 O(1) 的 hash table 查找
inline uint64_t fnv1a_64(const char* str) {
    constexpr uint64_t FNV_OFFSET = 14695981039346656037ULL;
    constexpr uint64_t FNV_PRIME  = 1099511628211ULL;
    uint64_t hash = FNV_OFFSET;
    while (*str) {
        hash ^= static_cast<uint64_t>(static_cast<uint8_t>(*str++));
        hash *= FNV_PRIME;
    }
    return hash;
}
```

---

## CRC32C (Castagnoli)

```cpp
// CRC32C (Castagnoli variant, polynomial 0x1EDC6F41)
// 用于数据完整性校验（硬件加速：SSE4.2 CRC32 指令）
// 不用于安全目的
uint32_t crc32c(const void* data, size_t size);
```

> 注：Windows 上可利用 SSE4.2 的 `_mm_crc32_u8/u64` 指令硬件加速。回退方案：查表法。

---

## 参考

- [[design-overview.md]] — 方案总览
- [[requirement-fulfillment.md]] — 需求实现详情
- [[write-path.md]] — 写入路径
