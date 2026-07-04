#!/bin/sh
# run_tests.sh — 统一测试入口
#
# 依次运行:
#   1. VM conformance tests   (scripts/vm/run_tests.sh)
#   2. AOT differential tests (scripts/aot/run_tests.sh)
#   3. Unit tests             (cmake target check-unit)
#
# 所有参数直接透传给 VM 测试脚本 (--asan, --verbose, --filter)。
# AOT 和单元测试总运行全部用例，不支持子集过滤。
#
# 用法:
#   bash scripts/run_tests.sh                 # 运行全部测试
#   bash scripts/run_tests.sh --verbose       # VM 部分显示详细日志
#   bash scripts/run_tests.sh --filter foo    # 仅 VM 测试中匹配 "foo" 的用例
#   bash scripts/run_tests.sh --asan          # AddressSanitizer 模式
#
# 构建:
#   make test                                 # 等价的 cmake 入口

set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PASSED_COUNT=""
FAILED_COUNT=""
ANY_FAIL=0
# 传给 vm 测试脚本的参数
VM_ARGS=""

# 解析已知参数；其余原样透传给 vm 脚本
while [ $# -gt 0 ]; do
    case "$1" in
    --asan|--verbose)
        VM_ARGS="${VM_ARGS} $1"
        shift
        ;;
    --filter)
        VM_ARGS="${VM_ARGS} $1 ${2:-}"
        if [ -n "${2:-}" ]; then shift 2; else shift; fi
        ;;
    --help)
        echo "用法: $0 [--asan] [--verbose] [--filter PATTERN]"
        echo ""
        echo "统一运行 VM、AOT、单元测试。参数透传给 VM 测试。"
        exit 0
        ;;
    *)
        echo "未知选项: $1" >&2
        echo "用法: $0 [--asan] [--verbose] [--filter PATTERN]" >&2
        exit 1
        ;;
    esac
done

echo "================================================================"
echo " TC-Compiler 统一测试入口"
echo "================================================================"
echo ""

# ---- 1. VM conformance tests ----
echo "================================================================"
echo " [1/3] VM Conformance Tests"
echo "================================================================"
set +e
bash "$ROOT/scripts/vm/run_tests.sh" $VM_ARGS
VM_EXIT=$?
set -e
echo ""

# ---- 2. AOT differential tests ----
echo "================================================================"
echo " [2/3] AOT Differential Tests"
echo "================================================================"
set +e
bash "$ROOT/scripts/aot/run_tests.sh"
AOT_EXIT=$?
set -e
echo ""

# ---- 3. Unit tests (cmake) ----
echo "================================================================"
echo " [3/3] Unit Tests"
echo "================================================================"
echo "--- Running Unit Tests ---"
set +e
make -C "$ROOT" test-unit 2>&1
UNIT_EXIT=$?
set -e
echo ""

# ---- Summary ----
echo "================================================================"
echo " 结果汇总"
echo "================================================================"
FAIL_ANY=""
[ "$VM_EXIT" -ne 0 ]  && FAIL_ANY="${FAIL_ANY} VM(FAIL) "   || FAIL_ANY="${FAIL_ANY} VM(PASS) "
[ "$AOT_EXIT" -ne 0 ] && FAIL_ANY="${FAIL_ANY} AOT(FAIL) "  || FAIL_ANY="${FAIL_ANY} AOT(PASS) "
[ "$UNIT_EXIT" -ne 0 ] && FAIL_ANY="${FAIL_ANY} Unit(FAIL) " || FAIL_ANY="${FAIL_ANY} Unit(PASS) "
echo "  ${FAIL_ANY}"
echo ""

if [ "$VM_EXIT" -ne 0 ] || [ "$AOT_EXIT" -ne 0 ] || [ "$UNIT_EXIT" -ne 0 ]; then
    echo "FAIL: 部分测试未通过" >&2
    exit 1
fi

echo "all tests passed"
