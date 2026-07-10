#!/bin/sh
# run_asan_all.sh — 一键 ASan 构建 + 全量测试 + 报告
#
# 用法:
#   bash scripts/run_asan_all.sh
#
# 依次执行:
#   1. cmake 配置 (build-asan, -fsanitize=address)
#   2. 构建全部目标
#   3. 运行 VM、单元、AOT 全量测试
#
# 退出码:
#   0 — 全部通过
#   1 — 至少一项失败

set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${ROOT}/build-asan"
ANY_FAIL=0

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
NC='\033[0m'

pass() { printf "  [ ${GREEN}PASS${NC} ] %s\n" "$1"; }
fail() { printf "  [ ${RED}FAIL${NC} ] %s\n" "$1"; ANY_FAIL=1; }
info() { printf "  [ ${CYAN}..${NC} ] %s\n" "$1"; }
heading() {
    echo ""
    echo "================================================================"
    echo " $1"
    echo "================================================================"
}

cd "$ROOT"

heading "ASan 全量测试"

# ---- 1. 配置 ----
heading "1/4: cmake 配置 (ASan)"
set +e
cmake -S . -B "$BUILD_DIR" \
    -DCMAKE_C_FLAGS="-fsanitize=address -g" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address" 2>&1
if [ $? -ne 0 ]; then
    fail "cmake 配置失败"
    exit 1
fi
pass "cmake 配置成功"

# ---- 2. 构建 ----
heading "2/4: 构建"
set +e
cmake --build "$BUILD_DIR" -j "$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)" 2>&1
if [ $? -ne 0 ]; then
    fail "构建失败"
    exit 1
fi
pass "构建成功"

# ---- 3. 测试 ----
heading "3/4: 运行测试"

info "VM conformance tests..."
set +e
bash "$ROOT/scripts/run_tests.sh" --asan 2>&1
VM_EXIT=$?
set -e
[ "$VM_EXIT" -eq 0 ] && pass "VM 测试全部通过" || fail "VM 测试未全部通过"

info "Unit tests..."
set +e
cmake --build "$BUILD_DIR" --target check-unit 2>&1
UNIT_EXIT=$?
set -e
[ "$UNIT_EXIT" -eq 0 ] && pass "单元测试全部通过" || fail "单元测试未全部通过"

info "AOT differential tests..."
set +e
bash "$ROOT/scripts/aot/run_tests.sh" 2>&1
AOT_EXIT=$?
set -e
[ "$AOT_EXIT" -eq 0 ] && pass "AOT 测试全部通过" || fail "AOT 测试未全部通过"

# ---- 4. 汇总 ----
heading "4/4: 汇总"
if [ "$ANY_FAIL" -ne 0 ]; then
    echo ""
    printf "  ${RED}FAIL: ASan 全量测试未全部通过${NC}\n"
    echo ""
    echo "  结果:"
    [ "$VM_EXIT" -ne 0 ]  && echo "    VM:  FAIL" || echo "    VM:  PASS"
    [ "$UNIT_EXIT" -ne 0 ] && echo "    Unit: FAIL" || echo "    Unit: PASS"
    [ "$AOT_EXIT" -ne 0 ] && echo "    AOT: FAIL" || echo "    AOT: PASS"
    exit 1
fi

echo ""
printf "  ${GREEN}全部通过！${NC}\n"
echo ""
echo "  可使用以下命令查看 ASan 构建:"
echo "    make build-asan"
echo "    bash scripts/run_tests.sh --asan"
echo ""
