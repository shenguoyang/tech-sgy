#!/bin/bash
# storage-kb 一键初始化脚本 (Windows 高性能存储)
# 用法: bash setup.sh

set -e

KB_ROOT="$(cd "$(dirname "$0")" && pwd)"

echo "=== 创建 storage-kb 目录结构 ==="

mkdir -p "$KB_ROOT/raw"/{papers,specs,datasheets,notes,html-archives}
mkdir -p "$KB_ROOT/wiki"/{concepts,storage-stack,filesystems,protocols,driver-model,performance,solutions,hardware,sources,archive}
mkdir -p "$KB_ROOT/projects"/{active,archived}
mkdir -p "$KB_ROOT/assets"/{images,animations,interactive}

echo "=== 添加 .gitkeep ==="

for d in \
    raw/papers raw/specs raw/datasheets raw/notes raw/html-archives \
    wiki/concepts wiki/storage-stack wiki/filesystems wiki/protocols \
    wiki/driver-model wiki/performance wiki/solutions wiki/hardware wiki/sources wiki/archive \
    projects/active projects/archived \
    assets/images assets/animations assets/interactive; do
    touch "$KB_ROOT/$d/.gitkeep"
done

echo "=== 初始化 Git 仓库 ==="
cd "$KB_ROOT"
if [ ! -d .git ]; then
    git init
    echo "storage-kb initialized."
else
    echo "Git repo already exists, skipped."
fi

echo ""
echo "目录结构："
find . -type d -not -path './.git/*' | sort | sed 's|^\.|storage-kb|'

echo ""
echo "=== 完成 ==="
echo "知识库已创建在: $KB_ROOT"
echo ""
echo "下一步："
echo "  1. 将原始资料放入 raw/ 对应目录"
echo "  2. 对 AI 助手说「处理 raw」开始摄入"
echo "  3. 对 AI 助手说「生成知识图谱」基于项目需求生成学习路径"
echo "  4. 用 Obsidian 打开 storage-kb/ 作为 Vault"
