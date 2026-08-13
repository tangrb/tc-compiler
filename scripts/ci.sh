#!/bin/sh
# ci.sh — 本地 CI 流水线
#
# 替代 GitHub Actions，在本地执行全部检查：
#   1. 构建（VM + AOT + libtc）
#   2. 全部测试（VM conformance + Unit + AOT differential）
#   3. TcRhsKind 分发覆盖检查
#   4. 源文件命名检查（tc_*.h ↔ tc_*.c）
#   5. （可选）覆盖率收集与报告
#
# 用法：
#   bash scripts/ci.sh             # 标准 CI（构建 + 测试 + 静态检查）
#   bash scripts/ci.sh --coverage  # 额外收集覆盖率
#   bash scripts/ci.sh --full      # 同 --coverage
#   bash scripts/ci.sh --help      # 帮助信息
#
# 退出码：
#   0 — 全部通过
#   1 — 至少一项失败

set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${ROOT}/build"
ANY_FAIL=0
DO_COVERAGE=0
COVERAGE_BUILD_DIR="${ROOT}/build-coverage"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# 仅在终端支持时启用颜色
if [ ! -t 1 ]; then
    RED=''; GREEN=''; YELLOW=''; CYAN=''; NC=''
fi

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

# -------------------------------------------------------------------------
# 参数解析
# -------------------------------------------------------------------------
while [ $# -gt 0 ]; do
    case "$1" in
    --coverage|--full)
        DO_COVERAGE=1
        shift
        ;;
    --help)
        echo "用法: bash scripts/ci.sh [选项]"
        echo ""
        echo "选项:"
        echo "  --coverage  收集覆盖率数据并生成 HTML 报告"
        echo "  --full      同 --coverage"
        echo "  --help      显示此帮助"
        echo ""
        echo "默认行为：构建 + 全部测试 + 静态检查"
        exit 0
        ;;
    *)
        echo "未知选项: $1" >&2
        echo "用法: bash scripts/ci.sh [--coverage] [--full] [--help]" >&2
        exit 1
        ;;
    esac
done

cd "$ROOT"

# =========================================================================
# 阶段 0：系统信息
# =========================================================================
heading "系统信息"

echo "  目录: $(uname -srm)"
echo "  CMake: $(cmake --version | head -1)"
echo "  CC:    $(cc --version | head -1)"
echo "  内核:  $(uname -v | head -1)"
echo "  仓库:  $(git rev-parse --short HEAD 2>/dev/null || echo 'N/A')"

# =========================================================================
# 阶段 1：构建
# =========================================================================
heading "阶段 1/5：构建项目 (VM + AOT + libtc)"

set +e
if [ "$DO_COVERAGE" -eq 1 ]; then
    info "覆盖率模式：使用 -O0 -g --coverage"
    cmake -S "$ROOT" -B "$COVERAGE_BUILD_DIR" \
        -DCMAKE_C_FLAGS="--coverage -O0 -g" \
        -DCMAKE_EXE_LINKER_FLAGS="--coverage" 2>&1
    if [ $? -ne 0 ]; then
        fail "cmake 配置 (coverage) 失败"
    else
        pass "cmake 配置 (coverage) 成功"
    fi

    cmake --build "$COVERAGE_BUILD_DIR" -j "$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)" 2>&1
    if [ $? -ne 0 ]; then
        fail "构建 (coverage) 失败"
    else
        pass "构建 (coverage) 成功"
    fi
fi

# 标准构建
cmake -S "$ROOT" -B "$BUILD_DIR" 2>&1
if [ $? -ne 0 ]; then
    fail "cmake 配置失败"
    echo ""
    echo "ERROR: 构建失败，终止 CI" >&2
    exit 1
fi
pass "cmake 配置成功"

cmake --build "$BUILD_DIR" -j "$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)" 2>&1
BUILD_EXIT=$?
if [ "$BUILD_EXIT" -ne 0 ]; then
    fail "构建失败"
    echo ""
    echo "ERROR: 构建失败，终止 CI" >&2
    exit 1
fi
pass "构建成功"

# =========================================================================
# 阶段 2：VM Conformance 测试
# =========================================================================
heading "阶段 2/5：VM Conformance 测试"

set +e
bash "$ROOT/scripts/vm/run_tests.sh" 2>&1
VM_EXIT=$?
set -e
if [ "$VM_EXIT" -ne 0 ]; then
    fail "VM Conformance 测试未全部通过"
else
    pass "VM Conformance 测试全部通过"
fi

# =========================================================================
# 阶段 3：单元测试
# =========================================================================
heading "阶段 3/5：单元测试"

set +e
cmake --build "$BUILD_DIR" --target check-unit 2>&1
UNIT_EXIT=$?
set -e
if [ "$UNIT_EXIT" -ne 0 ]; then
    fail "单元测试未全部通过"
else
    pass "单元测试全部通过"
fi

# =========================================================================
# 阶段 4：AOT Differential 测试
# =========================================================================
heading "阶段 4/5：AOT Differential 测试"

set +e
bash "$ROOT/scripts/aot/run_tests.sh" 2>&1
AOT_EXIT=$?
set -e
if [ "$AOT_EXIT" -ne 0 ]; then
    fail "AOT Differential 测试未全部通过"
else
    pass "AOT Differential 测试全部通过"
fi

# =========================================================================
# 阶段 5：静态检查
# =========================================================================
heading "阶段 5/5：静态检查"

# 5a. TcRhsKind 分发覆盖
set +e
python3 "$ROOT/scripts/sync/check_rhs_coverage.py" 2>&1
RHS_EXIT=$?
set -e
if [ "$RHS_EXIT" -ne 0 ]; then
    fail "TcRhsKind 分发覆盖检查未通过"
else
    pass "TcRhsKind 分发覆盖检查通过"
fi

# 5b. 源文件命名检查
set +e
python3 "$ROOT/scripts/sync/check_source_naming.py" 2>&1
NAMING_EXIT=$?
set -e
if [ "$NAMING_EXIT" -ne 0 ]; then
    fail "源文件命名检查未通过"
else
    pass "源文件命名检查通过"
fi

# 5c. 类型事实源（禁止 TcTypeTag 作为完整类型字段）
set +e
python3 "$ROOT/scripts/sync/check_type_fact_source.py" 2>&1
TYPE_FACT_EXIT=$?
set -e
if [ "$TYPE_FACT_EXIT" -ne 0 ]; then
    fail "类型事实源检查未通过"
else
    pass "类型事实源检查通过"
fi

# =========================================================================
# （可选）覆盖率
# =========================================================================
if [ "$DO_COVERAGE" -eq 1 ]; then
    heading "（可选）覆盖率收集"

    info "运行测试以生成 .gcda 文件..."
    TC_VM_BIN="$COVERAGE_BUILD_DIR/vm/bin/tc-vm" \
        bash "$ROOT/scripts/vm/run_tests.sh" 2>&1 || true
    TC_VM_BIN="$COVERAGE_BUILD_DIR/vm/bin/tc-vm" \
        TC_AOT_BIN="$COVERAGE_BUILD_DIR/aot/bin/tc-aot" \
        bash "$ROOT/scripts/aot/run_tests.sh" 2>&1 || true
    cmake --build "$COVERAGE_BUILD_DIR" --target check-unit 2>&1 || true

    info "收集覆盖率数据..."
    if ! command -v lcov >/dev/null 2>&1; then
        warn "lcov 未安装，跳过覆盖率收集"
        warn "  macOS: brew install lcov"
        warn "  Linux: apt install lcov / yum install lcov"
    else
        set +e
        lcov --capture --directory "$COVERAGE_BUILD_DIR" \
            --output-file "$COVERAGE_BUILD_DIR/coverage_raw.info" \
            --rc lcov_branch_coverage=1 2>&1
        if [ $? -ne 0 ]; then
            fail "lcov capture 失败"
        else
            pass "覆盖率原始数据收集成功"
        fi

        lcov --remove "$COVERAGE_BUILD_DIR/coverage_raw.info" \
            '/usr/*' '*/tests/*' \
            --output-file "$COVERAGE_BUILD_DIR/coverage.info" \
            --rc lcov_branch_coverage=1 2>&1
        if [ $? -ne 0 ]; then
            fail "覆盖率数据过滤失败"
        else
            pass "覆盖率数据过滤成功"
        fi

        info "生成 HTML 报告..."
        genhtml "$COVERAGE_BUILD_DIR/coverage.info" \
            --output-directory "$COVERAGE_BUILD_DIR/coverage_html" \
            --rc lcov_branch_coverage=1 \
            --title "TC-Compiler Coverage" 2>&1
        if [ $? -ne 0 ]; then
            fail "覆盖率报告生成失败"
        else
            pass "覆盖率报告已生成：${COVERAGE_BUILD_DIR}/coverage_html/index.html"
        fi

        # 打印摘要
        echo ""
        lcov --summary "$COVERAGE_BUILD_DIR/coverage.info" --rc lcov_branch_coverage=1 2>&1
        echo ""
    fi
fi

# =========================================================================
# 汇总
# =========================================================================
echo ""
echo "================================================================"
echo " 本地 CI 结果汇总"
echo "================================================================"

print_result() {
    local name=$1 code=$2
    if [ "$code" -eq 0 ]; then
        printf "  ${GREEN}%-40s 通过${NC}\n" "$name"
    else
        printf "  ${RED}%-40s 失败${NC}\n" "$name"
    fi
}

echo ""
print_result "构建"       "$BUILD_EXIT"
print_result "VM 测试"    "$VM_EXIT"
print_result "单元测试"   "$UNIT_EXIT"
print_result "AOT 测试"   "$AOT_EXIT"
print_result "RHS 覆盖检查" "$RHS_EXIT"
print_result "命名检查"   "$NAMING_EXIT"
echo ""

if [ "$ANY_FAIL" -ne 0 ]; then
    echo "  ${RED}FAIL: 本地 CI 未全部通过${NC}"
    echo ""
    echo "  提示：修正失败项后重新运行 ci.sh"
    exit 1
fi

echo "  ${GREEN}全部通过！${NC}"
echo ""
