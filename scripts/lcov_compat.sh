# lcov_compat.sh — lcov 1.x / 2.x / 3.x 兼容封装
#
# Source this file (not execute):
#   . scripts/lcov_compat.sh
#   lcov_with_compat --capture --directory DIR --output-file OUT.info
#   genhtml_with_compat IN.info --output-directory HTML --title "..."
#
# 调用方：scripts/ci.sh --coverage、.github/workflows/ci.yml coverage 作业。
#
# RC 名：
#   1.x  — --rc lcov_branch_coverage=1
#   2.x+ — --rc branch_coverage=1（2.5 把旧名当硬错误）
#
# --ignore-errors：
#   lcov --version 不校验 kind。Ubuntu apt 2.0 与 Homebrew 2.5 的合法集合
#   不同（2.0 不认 unmapped）；同一版本里 lcov 与 genhtml 也不相同
#   （2.0 的 genhtml 不认 gcov）。对每个候选 kind 跑一次真实子命令，
#   看到 “unknown argument for --ignore-errors” 则丢弃。
#   LLVM/Apple gcov 还会发出 inconsistent 分支记录，2.x 默认当错误。
#
# 1.x 不填 ignore 列表（没有 --ignore-errors）。

LCOV_BRANCH_RC="lcov_branch_coverage=1"
LCOV_IGNORE_KINDS=""
GENHTML_IGNORE_KINDS=""

_lcov_try_ignore() {
    _kind="$1"
    # --summary /dev/null 足以触发 kind 校验；缺文件不算 unknown argument
    _out=$(lcov --ignore-errors "$_kind" --summary /dev/null --rc "$LCOV_BRANCH_RC" 2>&1) || true
    case "$_out" in
    *"unknown argument for --ignore-errors"*)
        ;;
    *)
        if [ -n "$LCOV_IGNORE_KINDS" ]; then
            LCOV_IGNORE_KINDS="$LCOV_IGNORE_KINDS,$_kind"
        else
            LCOV_IGNORE_KINDS="$_kind"
        fi
        ;;
    esac
    unset _kind _out
}

_genhtml_try_ignore() {
    _kind="$1"
    # 独立探测：不可复用 LCOV_IGNORE_KINDS（2.0 genhtml 会拒 gcov）
    _dir="${TMPDIR:-/tmp}/genhtml_probe_$$"
    mkdir -p "$_dir" || return 0
    _out=$(genhtml --ignore-errors "$_kind" /dev/null \
        --output-directory "$_dir" --rc "$LCOV_BRANCH_RC" 2>&1) || true
    rm -rf "$_dir"
    case "$_out" in
    *"unknown argument for --ignore-errors"*)
        ;;
    *)
        if [ -n "$GENHTML_IGNORE_KINDS" ]; then
            GENHTML_IGNORE_KINDS="$GENHTML_IGNORE_KINDS,$_kind"
        else
            GENHTML_IGNORE_KINDS="$_kind"
        fi
        ;;
    esac
    unset _kind _out _dir
}

_lcov_ver="$(lcov --version 2>/dev/null || true)"
case "$_lcov_ver" in
*"version 2."*|*"version 3."*)
    LCOV_BRANCH_RC="branch_coverage=1"
    # 候选 kind 取 2.5 并集；本机不认的项由探测丢掉
    for _k in deprecated mismatch gcov source unmapped unused unsupported inconsistent; do
        _lcov_try_ignore "$_k"
        _genhtml_try_ignore "$_k"
    done
    unset _k
    ;;
esac
unset _lcov_ver

lcov_with_compat() {
    # 函数内显式传参，避免 ${var:+--ignore-errors "$var"} 在 zsh 合成一词
    if [ -n "$LCOV_IGNORE_KINDS" ]; then
        command lcov "$@" --rc "$LCOV_BRANCH_RC" --ignore-errors "$LCOV_IGNORE_KINDS"
    else
        command lcov "$@" --rc "$LCOV_BRANCH_RC"
    fi
}

genhtml_with_compat() {
    if [ -n "$GENHTML_IGNORE_KINDS" ]; then
        command genhtml "$@" --rc "$LCOV_BRANCH_RC" --ignore-errors "$GENHTML_IGNORE_KINDS"
    else
        command genhtml "$@" --rc "$LCOV_BRANCH_RC"
    fi
}
