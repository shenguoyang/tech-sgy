---
title: Windows 高性能存储方案 — 学习日志
type: project
tags: [学习日志, 进度追踪, 知识回流]
created: 2026-06-10
updated: 2026-06-10
---

# Windows 高性能存储方案 — 学习日志

> 追踪本项目的知识学习进度和知识回流记录。
> 关联：[[knowledge-graph.md]] | [[requirements.md]]

---

## 进度总览

```
Phase 1 基础概念    ░░░░░░░░░░  0 / 4  (I/O管理器, IRP, CoW, NVMe)
Phase 2 文件系统    ░░░░░░░░░░  0 / 3  (NTFS, $LogFile, 文件系统驱动)
Phase 3 中间层      ░░░░░░░░░░  0 / 4  (过滤管理器, 卷/分区, 类驱动, Storport)
Phase 4 写放大      ░░░░░░░░░░  0 / 4  (写放大, TRIM, 磨损均衡, ReFS)
Phase 5 性能与开发   ░░░░░░░░░░  0 / 4  (性能工具链, WDF/KMDF, Miniport开发, S2D)
─────────────────────────────────────────
总计               ░░░░░░░░░░  0 / 22  完成率 0 %
```

| 阶段 | 已完成 | 总数 | 完成率 |
|------|--------|------|--------|
| Phase 1 基础概念 | 0 | 4 | 0% |
| Phase 2 文件系统核心 | 0 | 3 | 0% |
| Phase 3 存储栈中间层 | 0 | 4 | 0% |
| Phase 4 写放大与完整性 | 0 | 4 | 0% |
| Phase 5 性能分析与开发 | 0 | 4 | 0% |
| **总计** | **0** | **22** | **0%** |

---

## 详细记录

| 日期 | 概念 | 操作 | 来源/资料 | 笔记 | Phase |
|------|------|------|----------|------|-------|
| — | — | — | — | — | — |

### 使用说明

每完成一个概念的学习，在此表中添加一行记录：

- **日期**：学习日期
- **概念**：对应 `knowledge-graph.md` 中的概念名称
- **操作**：`📖 学习` / `✍️ 创建Wiki页` / `🔗 扩展Wiki页` / `📊 对比分析`
- **来源/资料**：阅读的 raw/ 资料路径或外部链接
- **笔记**：关键收获、疑问、与项目需求的关联
- **Phase**：所属学习阶段

---

## 知识回流记录

> 记录从本项目新建/扩展的 Wiki 页面，确保项目发现回流到知识库。

| 日期 | Wiki 页面 | 回流类型 | 关键贡献 | 状态 |
|------|----------|---------|---------|------|
| — | — | — | — | — |

### 回流类型说明

| 类型 | 说明 |
|------|------|
| **新建** | 基于项目需要从零创建了 Wiki 页面 |
| **扩展** | 在已有 Wiki 页面中补充了 Windows 特定内容、性能数据或实践案例 |
| **修正** | 纠正了已有页面的错误或过时信息 |
| **关联** | 在已有页面间添加了新的语义链接 |

---

## 待处理清单

### 立即需要（P0 — 方案预研阻塞项）

- [ ] 学习 [[wiki/storage-stack/i-o-manager.md]] — 理解 IRP 生命周期与异步 I/O
- [ ] 创建 `wiki/filesystems/ntfs.md` — NTFS MFT、$LogFile、碎片化机制
- [ ] 创建 `wiki/storage-stack/file-system-drivers.md` — Windows 文件系统驱动架构
- [ ] 学习 [[wiki/storage-stack/port-drivers.md]] — Storport 队列管理与 I/O 路径
- [ ] 学习 [[wiki/concepts/write-amplification.md]] — 定位本场景写放大热点
- [ ] 创建 `wiki/performance/windows-perf-tooling.md` — ETW/WPA 存储栈追踪

### 短期需要（P1 — 方案收敛后需要）

- [ ] 创建 `wiki/concepts/trim-unmap-windows.md` — TRIM/UNMAP 在 Windows 的下发路径
- [ ] 创建 `wiki/storage-stack/class-drivers.md` — disk.sys IRP→SRB 转换
- [ ] 创建 `wiki/storage-stack/filter-manager.md` — Minifilter 开发模型
- [ ] 创建 `wiki/filesystems/refs.md` — ReFS CoW/块克隆/完整性流

### 中期需要（P2 — 进阶对比）

- [ ] 创建 `wiki/driver-model/wdf-kmdf.md` — Windows 驱动框架
- [ ] 创建 `wiki/driver-model/storport-miniport-dev.md` — Storport Miniport 开发
- [ ] 创建 `wiki/solutions/storage-spaces.md` — Storage Spaces 架构
- [ ] 收集 `raw/cases/bigtech-storage.md` — 大厂私有文件系统资料

---

## 决策待办

| ID | 决策项 | 依赖概念 | 状态 |
|----|--------|---------|------|
| ADR-001 | 选择方案方向（A/B/C/D/E） | Phase 1–2 完成后 | 🔴 待决策 |
| ADR-002 | 用户态 vs 内核态开发 | Phase 3 完成后 | 🔴 待决策 |
| ADR-003 | 冷热分层策略（应用层 vs 文件系统层） | Phase 4 完成后 | 🔴 待决策 |
| ADR-004 | 数据完整性实现方式（CoW / WAL / Journal） | Phase 4 完成后 | 🔴 待决策 |
| ADR-005 | 开发语言与框架（C/++内核 vs C#用户态 vs Rust） | ADR-001 完成后 | 🔴 待决策 |

---

## 日志

| 时间 | 操作 | 说明 |
|------|------|------|
| 2026-06-10 | 📋 创建 | 基于 [[requirements.md]] 初始化学习日志和知识图谱 |
