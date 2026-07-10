#!/bin/sh
# run_memcheck_macos.sh — macOS 内存安全检查一键脚本
#
# 在 macOS 上依次执行：
#   1. 标准构建
#   2. MallocScribble 模式（越界 / UAF 检测）
#   3. leaks --atExit 模式（泄漏检测）
#
# 依赖：Xcode Command Line Tools（提供 leaks）
#   xcode-select --install
#
# 用法:
#   bash scripts/run_memcheck_macos.sh
#
# 退出码:
#   0 — 全部通过
#   1 — 至少一项失败

set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ANY_FAIL=0

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

pass() { printf "  [ ${GREEN}PASS${NC} ] %s\n" "$1"; }
fail() { printf "  [ ${RED}FAIL${NC} ] %s\n" "$1"; ANY_FAIL=1; }
info() { printf "  [ ${CYAN}..${NC} ] %s\n" "$1"; }
warn() { printf "  [ ${YELLOW}WARN${NC} ] %s\n" "$1"; }
heading() {
    echo ""
    echo "================================================================"
    echo " $1"
    echo "================================================================"
}

cd "$ROOT"

# 先检查 leaks 是否可用
if ! command -v leaks >/dev/null 2>&1; then
    echo "error: 'leaks' 未安装 — 请运行 'xcode-select --install'" >&2
    exit 1
fi

heading "macOS 内存安全检查"

# ---- 1. 构建 ----
heading "1/3: 标准构建"
set +e
make vm 2>&1
if [ $? -ne 0 ]; then
    fail "构建失败"
    exit 1
fi
pass "构建成功"

TC_VM_BIN="$ROOT/build/vm/bin/tc-vm"
if [ ! -x "$TC_VM_BIN" ]; then
    echo "error: binary not found at $TC_VM_BIN" >&2
    exit 1
fi

# ---- 2. MallocScribble（越界 / UAF） ----
#
# MallocScribble=1: 释放后内存填 0x55，新分配填 0xAA，捕获野指针/未初始化
# MallocGuardEdges=1: 在分配区域尾端添加保护页，越界立即 crash
# MallocCheckHeapStart / MallocCheckHeapEach: 每次 malloc/free 检查堆完整性
#
# 注意：MallocGuardEdges 会大幅降低性能，但能精确定位越界。
# 这里只启用 MallocScribble（轻量），因为完整 guard edges 与 REPL 不兼容。
heading "2/3: MallocScribble（越界 / UAF 检测）"

echo "  MallocScribble=1 + MallocPreScribble=1 + MallocCheckHeapStart=1024"
echo ""

set +e
MallocScribble=1 MallocPreScribble=1 MallocCheckHeapStart=1024 \
    bash "$ROOT/scripts/run_tests.sh" 2>&1
SCRIBBLE_EXIT=$?
set -e
echo ""
if [ "$SCRIBBLE_EXIT" -ne 0 ]; then
    fail "MallocScribble 测试未全部通过"
    echo "  提示：可能检测到越界读取或使用已释放内存" >&2
else
    pass "MallocScribble 测试全部通过"
fi

# ---- 3. leaks（泄漏检测） ----
heading "3/3: leaks --atExit（泄漏检测）"

set +e
bash "$ROOT/scripts/run_tests.sh" --leaks 2>&1
LEAKS_EXIT=$?
set -e
echo ""
if [ "$LEAKS_EXIT" -ne 0 ]; then
    fail "leaks 泄漏检测未全部通过"
else
    pass "leaks 泄漏检测全部通过"
fi

# ---- 汇总 ----
echo ""
echo "================================================================"
echo " macOS 内存检查结果"
echo "================================================================"
echo ""
[ "$SCRIBBLE_EXIT" -eq 0 ] && pass "MallocScribble" || fail "MallocScribble"
[ "$LEAKS_EXIT" -eq 0 ]    && pass "leaks --atExit" || fail "leaks --atExit"
echo ""

if [ "$ANY_FAIL" -ne 0 ]; then
    printf "  ${RED}FAIL: macOS 内存检查未全部通过${NC}\n"
    echo ""
    echo "  提示："
    echo "    - MallocScribble 失败 → 检查越界访问 / 使用已释放内存"
    echo "    - leaks 失败        → 使用 'leaks --atExit' 定位未释放的内存"
    echo "    - 单步调试: MallocScribble=1 $TC_VM_BIN tests/valid/xxx.tc"
    echo "    - 或运行: ASan 构建 (bash scripts/run_asan_all.sh)"
    exit 1
fi

printf "  ${GREEN}全部通过！${NC}\n"
echo ""
echo "  快速命令备忘:"
echo "    MallocScribble=1 $TC_VM_BIN file.tc     # 单文件越界检查"
echo "    leaks --atExit -- $TC_VM_BIN file.tc    # 单文件泄漏检查"
echo "    make test-leaks                          # 全量泄漏检查"
echo ""
