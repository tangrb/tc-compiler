# lcov_compat.sh — lcov 1.x / 2.x 兼容封装
#
# Source this file (not execute):
#   . scripts/lcov_compat.sh
#   lcov_with_compat --capture --directory DIR --output-file OUT.info
#   genhtml_with_compat IN.info --output-directory HTML --title "..."
#
# lcov 2.5 treats --rc lcov_branch_coverage=1 as a hard error; the
# replacement is branch_coverage. LLVM gcov also emits inconsistent
# branch records that 2.x rejects unless ignored. Ignore-kind names
# differ across 2.0 (Ubuntu apt) and 2.5 (Homebrew), so each kind is
# probed instead of hard-coded.

LCOV_BRANCH_RC="lcov_branch_coverage=1"
LCOV_IGNORE_KINDS=""

_lcov_try_ignore() {
    _kind="$1"
    # lcov --version 不校验 kind；2.0 在真实子命令上才会报 unknown argument
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

_lcov_ver="$(lcov --version 2>/dev/null || true)"
case "$_lcov_ver" in
*"version 2."*|*"version 3."*)
    LCOV_BRANCH_RC="branch_coverage=1"
    _lcov_try_ignore deprecated
    _lcov_try_ignore mismatch
    _lcov_try_ignore gcov
    _lcov_try_ignore source
    _lcov_try_ignore unmapped
    _lcov_try_ignore unused
    _lcov_try_ignore unsupported
    _lcov_try_ignore inconsistent
    ;;
esac
unset _lcov_ver

lcov_with_compat() {
    if [ -n "$LCOV_IGNORE_KINDS" ]; then
        command lcov "$@" --rc "$LCOV_BRANCH_RC" --ignore-errors "$LCOV_IGNORE_KINDS"
    else
        command lcov "$@" --rc "$LCOV_BRANCH_RC"
    fi
}

genhtml_with_compat() {
    if [ -n "$LCOV_IGNORE_KINDS" ]; then
        command genhtml "$@" --rc "$LCOV_BRANCH_RC" --ignore-errors "$LCOV_IGNORE_KINDS"
    else
        command genhtml "$@" --rc "$LCOV_BRANCH_RC"
    fi
}
