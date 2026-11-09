#!/usr/bin/env bash
# Real LP64D indirect-call aggregate result, with GNU callee and GNU caller.
# The static source proof runs without a target toolchain; runtime does not.
set -Eeuo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
cross_cc=${LOONGARCH64_CC:-loongarch64-linux-gnu-gcc-13}
cross_qemu=${QEMU_LOONGARCH64:-qemu-loongarch64}
host_cc=${HOST_CC:-cc}
source="$root/tests/compiler/c0/loongarch64_indirect_sret.c"
gnu_source="$root/tests/compiler/c0/loongarch64_indirect_sret_gnu.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { echo "LOONGARCH64_INDIRECT_SRET=FAIL $*" >&2; exit 1; }

test -x "$minic" || fail 'MiniC binary missing'
"$host_cc" -E -P -std=gnu11 -x c "$source" -o "$work/source.i"
"$minic" --target=loongarch64-linux -S "$work/source.i" -o "$work/source.s" \
    || fail 'MiniC strict -S rejected supported indirect record result'

# Three functions, three GNU function-pointer variables, indirect jirl,
# correctly shifted a1 arguments, hidden a0 and preserved caller sret a0.
for func in minic_indirect_17 minic_indirect_24 minic_indirect_40; do
    grep -Eq "^${func}:$" "$work/source.s" || fail "missing $func"
done
test "$(grep -Ec '^  jirl \$ra, \$t2, 0$' "$work/source.s")" -eq 3 \
    || fail 'indirect jirl count'
test "$(grep -Ec '^  st\.d \$a0, \$(sp|s0|t3),' "$work/source.s")" -ge 3 \
    || fail 'entry hidden a0 must be preserved'
test "$(grep -Ec '^  ld\.d \$a0, \$(sp|s0|t3),' "$work/source.s")" -ge 3 \
    || fail 'original hidden a0 must be restored before return'
grep -Eq '^  ld\.d \$a[1-7], \$sp,' "$work/source.s" \
    || fail 'named GP argument assignment'
grep -Eq '^  st\.d \$t0, \$sp, 16$' "$work/source.s" \
    || fail 'late arguments must spill to stack'
for size in 17 24 40; do
    grep -Fq "  li.d \$t0, $size" "$work/source.s" \
        || fail "missing exact $size-byte return copy"
done
echo 'LOONGARCH64_INDIRECT_SRET_SOURCE=PASS 17,24,40 hidden_a0=1 shifted_a1=1 stack_overflow=1 call_through_pointer=1'

if ! command -v "$cross_cc" >/dev/null 2>&1 ||
   ! command -v "$cross_qemu" >/dev/null 2>&1; then
    if [[ ${MINIC_REQUIRE_LOONG_INDIRECT_SRET:-0} == 1 ]]; then
        fail 'required GNU cross compiler or QEMU missing'
    fi
    echo 'LOONGARCH64_INDIRECT_SRET_RUNTIME=SKIP GNU cross compiler/QEMU unavailable'
    exit 0
fi

"$cross_cc" -E -P -std=gnu11 -x c "$source" -o "$work/target.i"
"$minic" --target=loongarch64-linux -S "$work/target.i" -o "$work/target.s" \
    || fail 'GNU-preprocessed MiniC -S'
"$cross_cc" -O2 -std=gnu11 -static "$source" "$gnu_source" -o "$work/gnu" \
    || fail 'GNU reference build'
"$cross_cc" -O2 -std=gnu11 -c "$gnu_source" -o "$work/gnu.o" \
    || fail 'GNU callable pointer definitions'
"$cross_cc" -static -x assembler "$work/target.s" -x none "$work/gnu.o" \
    -o "$work/mixed" || fail 'MiniC/GNU cross link'
set +e
timeout 45s "$cross_qemu" "$work/gnu" >"$work/gnu.out" 2>"$work/gnu.err"
gnu_status=$?
timeout 45s "$cross_qemu" "$work/mixed" >"$work/mixed.out" 2>"$work/mixed.err"
mixed_status=$?
set -e
[[ $gnu_status -eq 42 && $mixed_status -eq 42 ]] ||
    fail "GNU=$gnu_status MiniC=$mixed_status"
cmp "$work/gnu.out" "$work/mixed.out" || fail 'stdout mismatch'
cmp "$work/gnu.err" "$work/mixed.err" || fail 'stderr mismatch'
echo 'LOONGARCH64_INDIRECT_SRET_RUNTIME=PASS GNU_GAS_LINK_QEMU=1 sizes=17,24,40'
