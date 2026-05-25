#!/bin/bash
set -e

# 定義變數
OWNER=${GITHUB_REPOSITORY_OWNER}
REPO=${GITHUB_REPOSITORY#*/}
WIKI_URL="https://x-access-token:${GITHUB_TOKEN}@github.com/${GITHUB_REPOSITORY}.wiki.git"

echo "### Syncing .docs/ content to wiki root..."

# Clone wiki 倉庫
git clone "$WIKI_URL" wiki

# 清空 wiki 目錄（保留 .git）
find wiki -type f -name "*.md" -delete

# 使用 Python 腳本複製並重命名檔案
python3 << 'EOF'
import os
import shutil

def normalize_page_name(page_path):
    """Convert page path to GitHub Wiki page name"""
    if page_path == 'Home':
        return 'Home'
    
    parts = page_path.split('/')
    result_parts = []
    for part in parts:
        subparts = part.split('-')
        subparts = [sub.capitalize() for sub in subparts]
        result_parts.append('-'.join(subparts))
    
    return '-'.join(result_parts)

# Copy Home
if os.path.exists('.docs/Home.md'):
    shutil.copy('.docs/Home.md', 'wiki/Home.md')

# Copy benchmark
if os.path.exists('.docs/benchmark.md'):
    wiki_name = normalize_page_name('benchmark')
    shutil.copy('.docs/benchmark.md', f'wiki/{wiki_name}.md')

# Copy core files
core_dir = '.docs/core'
if os.path.exists(core_dir):
    for file in os.listdir(core_dir):
        if file.endswith('.md'):
            basename = file[:-3]  # Remove .md
            page_path = f'core/{basename}'
            wiki_name = normalize_page_name(page_path)
            shutil.copy(os.path.join(core_dir, file), f'wiki/{wiki_name}.md')
            print(f'Copied {file} -> {wiki_name}.md')

# Copy applets files
applets_dir = '.docs/applets'
if os.path.exists(applets_dir):
    for file in os.listdir(applets_dir):
        if file.endswith('.md'):
            basename = file[:-3]  # Remove .md
            page_path = f'applets/{basename}'
            wiki_name = normalize_page_name(page_path)
            shutil.copy(os.path.join(applets_dir, file), f'wiki/{wiki_name}.md')
            print(f'Copied {file} -> {wiki_name}.md')
EOF

# 從 _sidebar.yml 生成 _Sidebar.md
if [ -f ".docs/_sidebar.yml" ]; then
  echo "### Generating _Sidebar.md from _sidebar.yml..."
  python3 .github/wf_scripts/generate_sidebar.py .docs/_sidebar.yml wiki/_Sidebar.md
fi

# 提交並推送
cd wiki
git config user.name "github-actions"
git config user.email "github-actions@github.com"

git add .
if git commit -m "docs: sync changes to wiki"; then
  echo "### Pushing changes to wiki..."
  git push
else
  echo "### No changes to sync (wiki is up to date)."
fi
