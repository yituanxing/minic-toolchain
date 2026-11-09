#!/bin/sh
# REP NOP/PAUSE/FNINIT with/without GNU compiler memory clobber.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
gnu=${HOST_CC:-cc}
work=${BUILD_DIR:-"$root/build/debug"}/tests/x86-zero-barrier-clobber
mkdir -p "$work"
cat >"$work/positive.c" <<'SOURCE'
void rep_nop(void) { asm volatile("rep; nop" ::: "memory"); }
void rep_nop_unclobbered(void) { asm volatile("rep; nop"); }
void pause_clobbered(void) { asm volatile("pause" ::: "memory"); }
void pause_unclobbered(void) { asm volatile("pause"); }
void fninit_clobbered(void) { asm volatile("fninit" ::: "memory"); }
void fninit_unclobbered(void) { asm volatile("fninit"); }
SOURCE
"$gnu" -std=gnu11 -E -P -x c "$work/positive.c" -o "$work/positive.i"
"$minic" --target=x86_64-linux -S "$work/positive.i" -o "$work/positive.s"
"$gnu" -x assembler -c "$work/positive.s" -o "$work/positive.o"
for op in 'rep; nop' pause fninit; do
    test "$(grep -Fc "  $op" "$work/positive.s")" -eq 2
done
cat >"$work/negative.c" <<'SOURCE'
void bad(void) { asm volatile("rep; nop; int3" ::: "memory"); }
SOURCE
"$gnu" -E -P -x c "$work/negative.c" -o "$work/negative.i"
rm -f "$work/negative.s"
if "$minic" --target=x86_64-linux -S "$work/negative.i" -o "$work/negative.s" >"$work/negative.log" 2>&1; then
    echo 'X86_ZERO_BARRIER_CLOBBER=FAIL injected asm admitted' >&2; exit 1
fi
test ! -e "$work/negative.s"
echo 'X86_ZERO_BARRIER_CLOBBER=PASS forms=3 clobber_variants=6 GNU_AS=1 negative=1'
