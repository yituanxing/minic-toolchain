#!/bin/sh
# Core IF-fact snapshot ownership and pre-prune/post-prune status provenance.
# A dead static helper may fail initial lowering (observable trace) without
# being a final-emission failure. The same unsupported body called from a live
# function must still cause a strict compilation error.
set -eu
: "${MINIC:?Set MINIC to the absolute path of minic-cc}"
: "${CC:=cc}"

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${BUILD_DIR:-"$root/build/core-if-snapshot-prune"}
mkdir -p "$work"

dead="$root/tests/compiler/c0/core_failed_unreachable_function_prune.c"
live="$root/tests/compiler/c0/core_if_snapshot_cleanup_negative.c"
"$CC" -std=gnu11 -O2 "$dead" -o "$work/gnu"
"$CC" -E -P -x c "$dead" -o "$work/dead.i"
"$CC" -E -P -x c "$live" -o "$work/live.i"

for target in x86_64-linux riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
    MINIC_CORE_FAILURE_CENSUS=1 "$MINIC" --target="$target" -S "$work/dead.i" \
        -o "$work/$target.s" 2>"$work/$target.trace"
    grep -F 'reason=lower-status function=unused_after_core_fold kind=5 status=1' \
        "$work/$target.trace" >/dev/null
    if grep -F 'MINIC_CORE_FUNCTION_FAILURE' "$work/$target.trace" >/dev/null; then
        printf 'FAIL: dead Core function counted as live failure on %s\n' "$target" >&2
        exit 1
    fi
    if MINIC_CORE_FAILURE_CENSUS=1 "$MINIC" --target="$target" -S "$work/live.i" \
        -o "$work/live-$target.s" 2>"$work/live-$target.trace"; then
        printf 'FAIL: live unsupported IF unexpectedly compiled on %s\n' "$target" >&2
        exit 1
    fi
    grep -F 'reason=lower-status function=unsupported_if_body kind=5 status=1' \
        "$work/live-$target.trace" >/dev/null
    grep -F 'reason=lower-status function=unsupported_else_body kind=5 status=1' \
        "$work/live-$target.trace" >/dev/null
    grep -F 'stage=if reason=then-body function=unsupported_if_body status=1' \
        "$work/live-$target.trace" >/dev/null
    grep -F 'stage=if reason=else-body function=unsupported_else_body status=1' \
        "$work/live-$target.trace" >/dev/null
    grep -F 'MINIC_CORE_FUNCTION_FAILURE kind=UNSUPPORTED' \
        "$work/live-$target.trace" >/dev/null
done

"$CC" -no-pie "$work/x86_64-linux.s" -o "$work/minic"
"$work/gnu"
"$work/minic"
printf 'PASS Core IF cleanup and failed-dead-function post-prune provenance: 5 ISA -S, GCC/MiniC native\n'
