---
title: 知识库总导航
type: overview
tags: [meta, index, navigation]
created: 2026-06-10
updated: 2026-06-10
---

# Windows 高性能存储 — 知识库总导航

## 知识总览图

![[overview]]

---

## 按分类浏览

### 存储栈（分层架构）
```dataview
TABLE type, tags, created
FROM "wiki/storage-stack"
WHERE type != "archived"
SORT file.name ASC
```

### 文件系统
```dataview
TABLE type, tags, created
FROM "wiki/filesystems"
WHERE type != "archived"
SORT file.name ASC
```

### 存储协议
```dataview
TABLE type, tags, created
FROM "wiki/protocols"
WHERE type != "archived"
SORT file.name ASC
```

### 驱动开发模型
```dataview
TABLE type, tags, created
FROM "wiki/driver-model"
WHERE type != "archived"
SORT file.name ASC
```

### 性能分析
```dataview
TABLE type, tags, created
FROM "wiki/performance"
WHERE type != "archived"
SORT file.name ASC
```

### 存储方案与虚拟化
```dataview
TABLE type, tags, created
FROM "wiki/solutions"
WHERE type != "archived"
SORT file.name ASC
```

### 基础概念
```dataview
TABLE type, tags, created
FROM "wiki/concepts"
WHERE type != "archived"
SORT file.name ASC
```

### 存储硬件
```dataview
TABLE type, tags, created
FROM "wiki/hardware"
WHERE type != "archived"
SORT file.name ASC
```

### 资料摘要
```dataview
TABLE type, tags, created
FROM "wiki/sources"
WHERE type != "archived"
SORT file.name ASC
```

---

## 归档知识

以下为知识库聚焦调整前的 Linux 生态内容，仅作跨平台参考：

```dataview
TABLE type, tags, created
FROM "wiki/archive"
WHERE type != "archived"
SORT file.name ASC
```

---

## 统计

- **总页面数**：`$= dv.pages().length`
- **活跃页面**：`$= dv.pages().where(p => p.type != "archived").length`
- **归档页面**：`$= dv.pages().where(p => p.type == "archived").length`
- **最后更新**：`$= dv.pages().sort(p => p.updated, 'desc').first().updated`
