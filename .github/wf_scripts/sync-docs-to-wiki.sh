#!/bin/bash
set -e

# 定義變數
OWNER=${GITHUB_REPOSITORY_OWNER}
REPO=${GITHUB_REPOSITORY#*/}
WIKI_URL="https://x-access-token:${GITHUB_TOKEN}@github.com/${GITHUB_REPOSITORY}.wiki.git"

echo "### Syncing .docs/ content to wiki root..."

# Clone wiki 倉庫
git clone "$WIKI_URL" wiki

# 從 _sidebar.yml 生成 _Sidebar.md
if [ -f ".docs/_sidebar.yml" ]; then
  echo "### Generating _Sidebar.md from _sidebar.yml..."
  python3 .github/wf_scripts/generate_sidebar.py .docs/_sidebar.yml wiki/_Sidebar.md
fi

# 同步內容 (平鋪在跟目錄，排除 .git 目錄)
rsync -av --delete .docs/ wiki/ --exclude .git

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
