# AGENTS.md — AI 行为规范

你是本知识库的 AI 协作者。本文档定义你的行为规范、操作流程和输出标准。

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
- **wiki/** — LLM 编译产物。经过提炼的知识，包括概念、协议、硬件、软件、实体、资料摘要、分析报告。
- **projects/** — 项目文档。可引用 Wiki 层知识。完成后归档到 archived/，项目中的新发现应回流到 Wiki 层。
- **assets/** — 图片、动画、交互式 HTML 组件。Wiki 层通过相对路径 `../assets/xxx` 引用。

---

## 页面模板

每个 Wiki 页面必须包含 YAML Frontmatter：

```yaml
---
title: 页面标题
type: concept | protocol | hardware | software | entity | source | output
tags: [标签1, 标签2]
created: YYYY-MM-DD
updated: YYYY-MM-DD
source: raw/xxx/文件名  # 可选，source 和 output 类型必填
---
```

---

## 语义链接规范

使用 Obsidian Wikilink 格式 `[[页面名]]`，配合以下语义标签标注关系：

| 标签 | 含义 | 使用场景 |
|------|------|----------|
| `@supersedes` | 技术上完全取代 | NVMe → AHCI, SSD → HDD |
| `@competes` | 竞争/替代方案关系 | LSM-Tree vs B-Tree, Ceph vs GlusterFS |
| `@based_on` | 基于/衍生自 | RocksDB → LSM-Tree, ZFS CoW → Btrfs CoW |
| `@implements` | 协议/接口的具体实现 | NVMe-oF → NVMe, SPDK → NVMe |
| `@contrasts` | 对比/差异分析 | CoW vs 日志journaling, 列存 vs 行存 |

**链接格式**：`[[目标页面.md]] @标签 说明文字`

---

## 核心操作流程

### 1. 摄入（Ingest）

**触发词**：「处理 raw/xxx/文件名」「摄入 xxx」

**执行步骤**：
1. 读取 raw/ 下的源文件内容
2. 在 `wiki/sources/` 创建资料摘要页（含源文件元信息、核心内容摘要、关键发现）
3. 提取核心概念，在 `wiki/concepts/`、`wiki/protocols/`、`wiki/hardware/`、`wiki/software/` 对应目录创建或更新页面
4. 在所有新建/更新页面中添加带语义标签的 `[[双向链接]]`
5. 更新 `wiki/index.md`，将新页面加入对应分类索引
6. 更新 `wiki/inbox.md`，将处理状态标记为「✅ 已处理」
7. 在 `wiki/log.md` 追加操作记录（时间、操作、涉及文件）

**约束**：
- 每个生成的 Wiki 页面 200–500 字
- 概念页面必须包含「核心思想」「典型实现」「与其他概念的关系」三部分
- 每个概念页面至少包含一个具体例子

### 2. 查询（Query）

**触发词**：任何知识性问题（用户提问时自动触发）

**执行步骤**：
1. 先读 `wiki/index.md` 定位相关页面
2. 深入阅读 3–5 个最相关的 Wiki 页面
3. 综合信息后回答，**必须引用具体页面路径**（如 `[[concepts/lsm-tree.md]]`）
4. 若答案质量高（结构完整、覆盖全面），询问用户是否归档到 `wiki/outputs/`

**约束**：
- 不得直接引用 raw/ 层的原始文件路径回答用户
- 回答优先使用 Wiki 层已编译的知识

### 3. 体检（Lint）

**触发词**：「体检」「检查知识库」「lint」

**执行步骤**：
1. 扫描所有 Wiki 页面，找出：
   - **孤立页面**：没有任何其他页面链接到它
   - **断链**：`[[链接]]` 指向不存在的页面
   - **矛盾信息**：两个页面对同一事实有冲突描述
   - **过期页面**：updated 日期超过 6 个月且内容可能过时
   - **缺失页面**：被多次链接但尚未创建的页面
2. 输出 Markdown 格式报告，包含问题列表和建议
3. 建议可创建的新页面（基于断链和知识空白）

---

## 写作风格

- **语言**：中文写作。专业名词首次出现时保留英文原文，格式：`中文译名（English Name, 缩写）`
- **篇幅**：每个 Wiki 页面 200–500 字
- **结构**：
  - 概念页：核心思想 → 典型实现 → 与其他概念的关系
  - 协议页：背景与动机 → 核心机制 → 与相关协议的关系
  - 软件页：设计哲学 → 关键架构 → 与其他系统的关系
  - 硬件页：物理原理 → 关键参数 → 与其他组件的关系
- **例子**：每个概念页面至少包含一个具体例子
- **链接密度**：每个页面至少包含 2–3 个指向其他页面的 `[[链接]]`

---

## 版本与元数据

- `wiki/log.md` 记录所有操作日志，格式：`| 时间 | 操作类型 | 涉及文件 | 摘要 |`
- `wiki/inbox.md` 追踪 Raw 文件处理状态
- `wiki/index.md` 为知识库总导航，每次摄入操作后更新

---

## Obsidian 推荐插件

为获得最佳体验，建议在 Obsidian 中安装以下插件：

| 插件 | 用途 |
|------|------|
| **Obsidian Wikilink Types** (`obsidian-wikilink-types`) | 支持 `@supersedes`、`@competes` 等语义标签的高亮和筛选 |
| **Dataview** | 基于 YAML Frontmatter 的动态查询，如列出所有 `type: concept` 页面 |
| **Graph View** (内置) | 可视化页面之间的链接关系 |
| **Backlinks** (内置) | 查看哪些页面引用了当前页面 |
| **Tag Wrangler** | 批量管理标签 |
| **Calendar** | 按创建日期浏览页面 |
| **Note Refactor** | 按标题拆分长页面为独立笔记 |
| **Templater** | 自动插入 YAML Frontmatter 模板 |

### Dataview 查询示例

在任意页面中嵌入以下代码块，动态列出内容：

```dataview
TABLE type, tags, updated
FROM "wiki/concepts"
SORT updated DESC
```

```dataview
TABLE source, updated
FROM "wiki/sources"
WHERE status = "待处理"
```
