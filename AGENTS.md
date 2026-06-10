# AGENTS.md — AI 行为规范

你是本知识库的 AI 协作者。本文档定义你的行为规范、操作流程和输出标准。

**知识库聚焦领域**：Windows 高性能存储（Windows High-Performance Storage），覆盖 Windows 存储栈（驱动模型、I/O 子系统）、文件系统（NTFS/ReFS）、存储协议（NVMe）、性能分析工具链和存储方案。

---

## 知识库架构

本知识库采用四层架构：

```
storage-kb/
├── raw/          # 第1层：原始资料（只读，事实来源）
├── wiki/         # 第2层：知识层（LLM 编译产物，用户主要阅读区）
├── projects/     # 第3层：项目层（具体项目文档，可引用 Wiki 层）
└── assets/       # 第4层：知识资产（图片、动画、交互组件）
```

### 各层规则

- **raw/** — 只读不修改。原始论文、协议规范、数据手册、个人笔记存放处。HTML 文件：提取正文为 Markdown 后存入 wiki/sources/，图片存入 assets/images/，原始 HTML 备份到 raw/html-archives/。
- **wiki/** — LLM 编译产物。经过提炼的知识。子目录按 Windows 存储栈分层组织：
  
  ```
  wiki/
  ├── overview.md          # 总览图（Mermaid 流程图）
  ├── index.md             # 导航仪表盘
  ├── inbox.md             # raw 处理状态追踪
  ├── log.md               # 操作日志
  ├── concepts/            # 基础概念（跨层通用）
  ├── storage-stack/       # Windows 存储栈（按层分解）
  ├── filesystems/         # Windows 文件系统
  ├── protocols/           # 存储协议
  ├── driver-model/        # Windows 驱动开发模型
  ├── performance/         # Windows 性能分析
  ├── solutions/           # 存储方案与虚拟化
  ├── hardware/            # 存储硬件
  ├── sources/             # 资料摘要
  └── archive/             # 归档内容
  ```
- **projects/** — 项目文档。可引用 Wiki 层知识。已完成项目归档到 archived/。项目中的新发现应通过**知识回流（Backflow）** 流程回流到 Wiki 层。
- **assets/** — 图片、动画、交互式 HTML 组件。Wiki 层通过相对路径 `../assets/xxx` 引用。

---

## 页面模板

每个 Wiki 页面必须包含 YAML Frontmatter：

```yaml
---
title: 页面标题
type: concept | protocol | hardware | stack-layer | driver | perf | virt | archived | source | output | overview
tags: [标签1, 标签2]
created: YYYY-MM-DD
updated: YYYY-MM-DD
source: raw/xxx/文件名  # 可选，source 和 output 类型必填
---
```

### 页面类型说明

| 类型            | 用途             | 所在目录           |
| ------------- | -------------- | -------------- |
| `concept`     | 基础概念（跨层通用）     | concepts/      |
| `protocol`    | 存储协议           | protocols/     |
| `hardware`    | 存储硬件           | hardware/      |
| `stack-layer` | Windows 存储栈层描述 | storage-stack/ |
| `driver`      | Windows 驱动开发   | driver-model/  |
| `perf`        | Windows 性能分析   | performance/   |
| `virt`        | 存储虚拟化与方案       | solutions/     |
| `archived`    | 已归档内容（仅参考）     | archive/       |
| `source`      | 原始资料摘要         | sources/       |
| `output`      | 问答输出归档         | 灵活存放           |
| `overview`    | 总览图            | 根目录            |

---

## 语义链接规范

使用 Obsidian Wikilink 格式 `[[目标页面]]`，配合以下语义标签标注关系：

| 标签            | 含义         | 使用场景                             |
| ------------- | ---------- | -------------------------------- |
| `@supersedes` | 技术上完全取代    | Storport → SCSIPORT, NVMe → AHCI |
| `@competes`   | 竞争/替代方案关系  | ReFS vs NTFS, S2D vs 传统 SAN      |
| `@based_on`   | 基于/衍生自     | ReFS → CoW, StorNVMe → NVMe      |
| `@implements` | 协议/接口的具体实现 | StorNVMe → NVMe, NTFS → IRP      |
| `@contrasts`  | 对比/差异分析    | CoW vs Journaling, ReFS vs NTFS  |

**链接格式**：`[[目标页面.md]] @标签 说明文字` 或 `[[目录/目标页面|显示文字]] @标签 说明文字`

---

## Windows 聚焦规则

1. **Windows 优先视角**：每个页面先描述 Windows 上的实现/行为，再对比其他平台（仅当有助于理解差异时），不将 Linux 作为默认上下文
2. **Windows 官方术语**：
   - "IRP（I/O Request Packet）" 而非 "I/O 请求"
   - "Storport" 而非 "存储端口驱动"
   - "Miniport Driver" 而非 "微型端口驱动"
   - "NTFS $LogFile" 而非 "NTFS 日志"
3. **参考来源优先**：MSDN/WDK 文档、Windows Internals 书籍（Russinovich）、Microsoft Docs、WHCP 规范
4. **Windows 特殊性标注**：当某行为是 Windows 特有的，应在文中明确标注 "Windows 特有"

---

## 核心操作流程

### 1. 摄入（Ingest）

**触发词**：「处理 raw/xxx/文件名」「摄入 xxx」「处理 raw」

**执行步骤**：

1. 读取 raw/ 下的源文件内容
2. 在 `wiki/sources/` 创建资料摘要页（含源文件元信息、核心内容摘要、关键发现）
3. 提取核心概念，在 `wiki/concepts/`、`wiki/storage-stack/`、`wiki/protocols/`、`wiki/filesystems/` 等对应目录创建或更新页面
4. 在所有新建/更新页面中添加带语义标签的 `[[双向链接]]`
5. **Windows 聚焦检查**：确认新页面以 Windows 视角组织，非 Windows 内容标注对比来源
6. 更新 `wiki/index.md`，将新页面加入对应分类索引
7. 更新 `wiki/inbox.md`，将处理状态标记为「✅ 已处理」
8. 在 `wiki/log.md` 追加操作记录（时间、操作、涉及文件）
9. **触发总览图维护**：检查新内容是否需要更新 `wiki/overview.md`

**约束**：

- 每个生成的 Wiki 页面 小于5000 字
- 概念页面必须包含「核心思想」「Windows 上的实现」「与其他概念的关系」三部分
- stack-layer 页面必须包含该层在存储栈中的位置、上下游关系
- 每个概念页面至少包含一个 Windows 平台的具体例子

### 2. 查询（Query）

**触发词**：任何知识性问题（用户提问时自动触发）

**执行步骤**：

1. 先读 `wiki/overview.md` 和 `wiki/index.md` 定位相关页面
2. 深入阅读 3–5 个最相关的 Wiki 页面
3. 综合信息后回答，**必须引用具体页面路径**（如 `[[storage-stack/i-o-manager.md]]`）
4. 若答案质量高（结构完整、覆盖全面），询问用户是否归档到 `wiki/` 对应目录

**约束**：

- 不得直接引用 raw/ 层的原始文件路径回答用户
- 回答优先使用 Wiki 层已编译的知识
- archive/ 目录下的内容仅在跨平台对比时可引用

### 3. 体检（Lint）

**触发词**：「体检」「检查知识库」「lint」

**执行步骤**：

1. 扫描所有 Wiki 页面（含 archive/），找出：
   - **孤立页面**：没有任何其他页面链接到它
   - **断链**：`[[链接]]` 指向不存在的页面
   - **矛盾信息**：两个页面对同一事实有冲突描述
   - **过期页面**：updated 日期超过 6 个月且内容可能过时
   - **缺失页面**：被多次链接但尚未创建的页面
   - **Windows 聚焦偏离**：页面未以 Windows 视角组织
2. 输出 Markdown 格式报告，包含问题列表和建议
3. 建议可创建的新页面（基于断链和知识空白）
4. archive/ 下的页面仅检查断链，不检查聚焦偏离和过期

### 4. 知识图谱生成（Knowledge Graph Generation）

**触发词**：「生成知识图谱」「规划学习路径」

**前置条件**：`projects/active/<项目名>/requirements.md` 已存在

**执行步骤**：

1. 读取 `projects/active/<项目名>/requirements.md`
2. 提取所有涉及的技术概念，按 wiki 目录分类（stack-layer / filesystems / protocols / driver-model / performance / solutions）
3. 查**前置依赖参考表**（见下文附录），构建每个概念的依赖关系图
4. 生成 `projects/active/<项目名>/knowledge-graph.md`：
   - Mermaid 依赖关系图（概念节点 + 依赖箭头）
   - 学习路径表：`| 顺序 | 概念 | 前置依赖 | 对应 Wiki 页面 | 状态 |`
   - 待收集资料表：`| 优先级 | 主题 | 建议搜索词 | 目标 raw/ 路径 |`
5. 生成 `projects/active/<项目名>/learning-log.md`：
   - 进度总览（总概念数/已完成百分比）
   - 详细记录表（日期/概念/操作/来源/笔记）
   - 知识回流记录表

**约束**：

- 知识图谱中的每个概念必须有明确的"前置依赖"或标注"无前置"
- 建议搜索词需包含 Windows 特定关键词
- 学习路径按拓扑序排列

### 5. 总览图维护（Overview Map Maintenance）

**触发方式**：每次 Ingest 完成后自动触发，或手动「更新总览图」

**执行步骤**：

1. 扫描本次新建/更新的 wiki 页面
2. 按 tier-1 判定规则检查是否应加入总览图：
   - 强制纳入：`storage-stack/` 全部页面、`filesystems/` 全部页面
   - 重要纳入：`protocols/` 主要协议页、`solutions/` 核心方案页
   - 引用阈值纳入：concepts/ 和 hardware/ 页被 3+ 其他 wiki 页引用时才纳入
   - 不纳入：sources/、archive/ 页面
3. 若纳入：确定节点在 Mermaid 流程图中的位置（存储栈层 / 跨层关注 / 硬件层），添加节点和边
4. 若已有节点内容变化：更新节点描述文字
5. 报告变更

### 6. 知识回流（Knowledge Backflow）

**触发词**：「回流项目知识」「整合项目发现」

**执行步骤**：

1. 读取 `projects/active/<项目名>/knowledge-graph.md` 和 `learning-log.md`
2. 识别已完成学习但尚未在 wiki 中体现的概念
3. 对每个待回流的发现：
   - 创建新 wiki 页面或扩展现有页面
   - 添加 `[[交叉引用]]` 和语义标签
   - 更新相关页面的 `updated` 日期
4. 更新 `learning-log.md` 中的回流记录表
5. 触发总览图维护（如果创建了 tier-1 页面）

---

## 项目闭环流程

```
projects/active/<项目>/requirements.md   (用户撰写)
        │
        ▼
   知识图谱生成   ───→  knowledge-graph.md  (知识依赖图)
        │              learning-log.md      (进度追踪)
        │
        ▼
   用户按拓扑序学习 → raw/ 积累资料
        │
        ▼
   手动「处理 raw」→  Ingest 流程
        │
        ▼
   wiki 页面创建/更新 → 总览图维护
        │
        ▼
   手动「回流项目知识」→ learning-log 更新 → wiki 补充
        │
        ▼
   项目完成 → 迁移到 projects/archived/<项目名>/
```

---

## 写作风格

- **语言**：中文写作。专业名词首次出现时保留英文原文，格式：`中文译名（English Name, 缩写）`
- **篇幅**：每个 Wiki 页面 小于5000 字
- **结构模板**：
  - **概念页**：核心思想 → Windows 上的实现 → 与其他概念的关系
  - **协议页**：背景与动机 → 核心机制 → Windows 上的驱动栈实现 → 与相关协议的关系
  - **stack-layer 页**：栈层定位（上游/下游）→ 核心职责 → 关键数据结构/函数 → 与相邻层的关系
  - **driver 页**：框架概述 → Miniport 接口 → 开发流程 → 调试/验证
  - **perf 页**：工具链概述 → 关键指标与采集 → 分析方法 → 典型案例
  - **virt 页**：方案概述 → 架构设计 → Windows 实现 → 与其他方案对比
  - **硬件页**：物理原理 → 关键参数 → Windows 交互路径 → 与其他组件的关系
- **例子**：每个概念页面至少包含一个 Windows 平台的具体例子（含代码片段或命令）
- **链接密度**：每个页面至少包含 2–3 个指向其他页面的 `[[链接]]`

---

## 版本与元数据

- `wiki/log.md` 记录所有操作日志，格式：`| 时间 | 操作类型 | 涉及文件 | 摘要 |`
- `wiki/inbox.md` 追踪 Raw 文件处理状态
- `wiki/index.md` 为知识库总导航，嵌入 `wiki/overview.md` + Dataview 查询
- `wiki/overview.md` 为知识总览图，AI 负责维护其 Mermaid 流程图

---

## 附录：Windows 存储知识依赖关系参考表

| 目标概念                        | 前置依赖                            | 所属目录           |
| --------------------------- | ------------------------------- | -------------- |
| I/O 管理器                     | 无（起点）                           | storage-stack/ |
| IRP 模型                      | I/O 管理器                         | storage-stack/ |
| NTFS 内部原理                   | I/O 管理器, IRP 模型                 | filesystems/   |
| ReFS                        | NTFS 基础, CoW 概念                 | filesystems/   |
| NTFS $LogFile               | NTFS 内部原理, 日志概念                 | filesystems/   |
| 卷管理器 (volmgr)               | I/O 管理器, IRP 模型                 | storage-stack/ |
| 分区管理器 (partmgr)             | 卷管理器                            | storage-stack/ |
| 类驱动 (disk.sys)              | IRP 模型, 分区管理器                   | storage-stack/ |
| Storport 架构                 | 类驱动, SRB 模型                     | storage-stack/ |
| Miniport 驱动                 | Storport, 硬件基础                  | storage-stack/ |
| WDF/KMDF 框架                 | IRP 模型, 内核基础                    | driver-model/  |
| Storport Miniport 开发        | Storport, WDF/KMDF              | driver-model/  |
| NVMe 协议                     | PCIe 基础                         | protocols/     |
| NVMe on Windows             | NVMe 协议, Storport, Miniport     | protocols/     |
| SMB Direct                  | SMB 协议, RDMA 基础                 | protocols/     |
| Storage Spaces              | 卷管理器, 分区管理器, RAID 概念            | solutions/     |
| S2D (Storage Spaces Direct) | Storage Spaces, SMB Direct      | solutions/     |
| ETW / WPA                   | I/O 管理器, 内核基础                   | performance/   |
| 磁盘性能计数器                     | I/O 管理器, PerfMon                | performance/   |
| TRIM/UNMAP on Windows       | NTFS, Storport, NVMe            | concepts/      |
| Windows 写放大                 | NTFS $LogFile, TRIM, SSD GC     | concepts/      |
| CSVFS                       | NTFS, 集群概念                      | filesystems/   |
| Hyper-V 存储                  | VHDX, Storage Spaces, NTFS/ReFS | solutions/     |
