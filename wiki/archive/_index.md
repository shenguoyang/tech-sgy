---
title: 归档知识
type: archived
tags: [meta, archive, index]
created: 2026-06-10
updated: 2026-06-10
---

# 归档知识

本目录存放知识库聚焦调整前的 **Linux 生态内容**。这些页面不再积极维护，仅作为跨平台参考保留。

> **为什么归档？** 本知识库已聚焦于 **Windows 高性能存储**。以下页面原本是初始化阶段的示例内容，偏向 Linux 存储生态（ZFS、Ceph、LSM-Tree），与当前方向不匹配。

若需要 Windows 相关概念，请从 [[../index|总导航页]] 开始查阅。

## 归档页面列表

| 页面 | 原分类 | Windows 对应概念 |
|------|--------|-----------------|
| [[lsm-tree|LSM-Tree]] | concept | Windows ESE (Extensible Storage Engine) 也使用类似 LSM 的结构 |
| [[zfs|ZFS]] | software | [[../filesystems/refs|ReFS]] 是 Windows 上的 CoW 文件系统 |
| [[ceph|Ceph]] | software | [[../solutions/storage-spaces|Storage Spaces Direct]] 是 Windows 分布式存储方案 |
| [[lsm-tree-paper|LSM-Tree 论文摘要]] | source | 通用存储概念，可跨平台参考 |

## Dataview 查询

```dataview
TABLE type, tags, created
FROM "wiki/archive"
WHERE type = "archived"
SORT file.name ASC
```
