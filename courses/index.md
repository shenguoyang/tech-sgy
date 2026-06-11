---
title: 课程层总导航
type: overview
tags: [meta, feynman, courses, navigation]
created: 2026-06-11
updated: 2026-06-11
---

# 课程层总导航

> 这是知识库的**费曼学习层（第 5 层）**。目标是：通过「教别人」来学得更深。
> 每门课程回答一个真实问题，每个对打发现一个盲点，每个类比简化一个概念。

---

## 费曼学习法速览

```
1. 选一个项目中的问题 ──→  2. 教给别人（写教学笔记）
                               │
    4. 简化和类比  ←──────── 3. 发现盲点（AI 对打）
```

---

## 课程进度

### 草稿区（Drafts）
```dataview
TABLE course_type, driving_question, related_phase, audience
FROM "courses/drafts"
WHERE status != "published"
SORT created ASC
```

### 已发布教程（Published）
```dataview
TABLE course_type, driving_question, related_phase, audience
FROM "courses/published"
WHERE status = "published"
SORT created ASC
```

---

## AI 对打记录

```dataview
TABLE course_ref, date
FROM "courses/_battles"
SORT date DESC
```

---

## 类比库

```dataview
TABLE course_ref, analogies
FROM "courses/drafts" OR "courses/published"
WHERE analogies
FLATTEN analogies
SORT file.name ASC
```

---

## 关联知识

- 课程选题来源于 [[projects/active/windows高性能存储方案/requirements.md]] 中的需求
- 课程内容引用 [[wiki/index.md]] 层的深度知识
- 课程发现通过**知识回流**补充 wiki 层

---

## 统计

- **微课程**：`$= dv.pages().where(p => p.course_type == "micro").length`
- **章节式课程**：`$= dv.pages().where(p => p.course_type == "chapter").length`
- **对打记录**：`$= dv.pages('"courses/_battles"').length`
