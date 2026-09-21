# lcov_compat.sh — lcov 1.x / 2.x 兼容封装
#
# Source this file (not execute):
#   . scripts/lcov_compat.sh
#   lcov_with_compat --capture --directory DIR --output-file OUT.info
#   genhtml_with_compat IN.info --output-directory HTML --title "..."
#
# lcov 2.5 treats --rc lcov_branch_coverage=1 as a hard error; the
# replacement is branch_coverage. LLVM gcov also emits inconsistent
# branch records that 2.x rejects unless ignored.

LCOV_BRANCH_RC="lcov_branch_coverage=1"
LCOV_IGNORE_KINDS=""

_lcov_ver="$(lcov --version 2>/dev/null || true)"
case "$_lcov_ver" in
*"version 2."*|*"version 3."*)
    LCOV_BRANCH_RC="branch_coverage=1"
    LCOV_IGNORE_KINDS="deprecated,mismatch,gcov,source,unmapped,unused,unsupported,inconsistent"
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
