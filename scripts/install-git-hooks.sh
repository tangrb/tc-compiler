#!/bin/sh
# 安装仓库 Git hooks（core.hooksPath → scripts/git-hooks）
set -eu

repo_root=$(cd "$(dirname "$0")/.." && pwd)
hooks_dir="$repo_root/scripts/git-hooks"

cd "$repo_root"

if ! git rev-parse --git-dir >/dev/null 2>&1; then
    echo "error: not a git repository" >&2
    exit 1
fi

for hook in commit-msg pre-commit; do
    [ -f "$hooks_dir/$hook" ] && chmod +x "$hooks_dir/$hook"
done
git config core.hooksPath scripts/git-hooks

echo "Git hooks installed: core.hooksPath=scripts/git-hooks"
echo "  commit-msg — strips Co-authored-by: Cursor <cursoragent@cursor.com>"
echo "  pre-commit — runs documentation gates when docs are staged"
