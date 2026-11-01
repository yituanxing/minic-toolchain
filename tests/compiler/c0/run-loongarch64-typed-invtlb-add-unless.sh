#!/bin/sh
# Same GNU .i, R5/R6 exact Loong .s, and fail-closed hardware-template edits.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
baseline=${MINIC_LOONG_TYPED_BASELINE_CC:-}
host_cc=${HOST_CC:-cc}
work=${BUILD_DIR:-"$root/build/debug"}/tests/loong-typed-invtlb-add-unless
mkdir -p "$work"
test -x "$minic"
if test -n "$baseline"; then test -x "$baseline"; fi
"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/loongarch64_typed_invtlb_add_unless.c" \
    -o "$work/source.i"
"$minic" --target=loongarch64-linux -S "$work/source.i" -o "$work/r6.s"
test "$(grep -Fc '  invtlb 0,' "$work/r6.s")" -eq 4
grep -Fq '  sc.w $t1, $t2, 0' "$work/r6.s"
grep -Fq '  sc.d $t1, $t2, 0' "$work/r6.s"
if test -n "$baseline"; then
    "$baseline" --target=loongarch64-linux -S "$work/source.i" -o "$work/r5.s"
    cmp "$work/r5.s" "$work/r6.s"
    echo 'LOONG_TYPED_SCALAR_DIFF=PASS same_gnu_i=1 byte_identical_s=1'
fi
for family in invtlb add_unless; do
    case "$family" in
        invtlb) sed 's/invtlb %0, %1, %2/invtlb %0, %1, %2; nop/' \
            "$work/source.i" >"$work/invalid.i" ;;
        add_unless) sed 's/sc.w %\[rc\], %\[c\]/sc.w %[rc], %[c]; nop/' \
            "$work/source.i" >"$work/invalid.i" ;;
    esac
    if cmp -s "$work/source.i" "$work/invalid.i"; then
        echo "LOONG_TYPED_SCALAR=FAIL missed_mutation=$family" >&2
        exit 1
    fi
    rm -f "$work/invalid.s"
    if "$minic" --target=loongarch64-linux -S "$work/invalid.i" \
        -o "$work/invalid.s" >"$work/invalid.out" 2>"$work/invalid.err"; then
        echo "LOONG_TYPED_SCALAR=FAIL accepted=$family" >&2
        exit 1
    fi
    test ! -e "$work/invalid.s"
    if test -n "$baseline"; then
        rm -f "$work/baseline-invalid.s"
        if "$baseline" --target=loongarch64-linux -S "$work/invalid.i" \
            -o "$work/baseline-invalid.s" \
            >"$work/baseline-invalid.out" 2>"$work/baseline-invalid.err"; then
            echo "LOONG_TYPED_SCALAR_DIFF=FAIL baseline_accepted=$family" >&2
            exit 1
        fi
        test ! -e "$work/baseline-invalid.s"
    fi
done
echo 'LOONG_TYPED_SCALAR=PASS invtlb_forms=4 add_unless_widths=2 invalid=2 partial_s=0 GNU_AS=NOT_RUN'
