#!/usr/bin/env bash
# x86 consumes the Common Core use census without eliding observable effects.
# Assemble with GNU as, link with GNU cc and compare execution with GNU C.
set -Eeuo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
fail() { printf 'X86_CORE_DEAD_SSA=FAIL %s\n' "$*" >&2; exit 1; }
source_file="$root/tests/compiler/c0/core_dead_ssa_writeback.c"
cc -E -P -std=gnu11 -x c "$source_file" -o "$work/source.i"
"$minic" -S "$work/source.i" --target=x86_64-linux -o "$work/mini.s"
as --64 "$work/mini.s" -o "$work/mini.o"

awk '/^dead_add:$/ {inside=1} inside {print} /^\.size dead_add,/ {exit}' \
    "$work/mini.s" > "$work/dead.s"
awk '/^live_store:$/ {inside=1} inside {print} /^\.size live_store,/ {exit}' \
    "$work/mini.s" > "$work/live.s"
awk '
    /^  (addq|xorq) %r10, %rax$/ {
        if (getline <= 0 || $0 ~ /^  movq %rax, -[0-9]+\(%rbp\)$/) exit 1
        seen++
    }
    END {if (seen != 2) exit 1}
' "$work/dead.s" || fail 'unused arithmetic result emitted a stack spill'
# A later single-use GP forwarding pass may legally remove an SSA spill,
# whereas the volatile global write and the addition must still execute.
# Do not force an implementation-specific stack roundtrip for a live value.
grep -Fq '  addq %r10, %rax' "$work/live.s" ||
    fail 'live arithmetic computation disappeared'
grep -Fq 'movq %rax, (%r10)' "$work/live.s" ||
    fail 'observable global store disappeared'

cat > "$work/driver.c" <<'EOF'
extern long dead_add(long, long);
extern long live_store(long);
extern volatile long observed;
int main(void) {
    long discarded = dead_add(11, 7);
    long written = live_store(5);
    return discarded != 11 || written != 14 || observed != 14;
}
EOF
cc -std=gnu11 -O2 -fno-pie -no-pie "$source_file" "$work/driver.c" \
    -o "$work/gnu"
cc -std=gnu11 -O2 -no-pie -Wl,-z,noexecstack "$work/mini.o" \
    "$work/driver.c" -o "$work/mini"
"$work/gnu" || fail 'GNU C oracle failed'
"$work/mini" || fail 'MiniC GNU-linked runtime disagrees with GNU C'
printf 'X86_CORE_DEAD_SSA=PASS dead_spills=0 live_effects_preserved=1 GNU_AS=PASS GNU_LINK=PASS runtime=PASS\n'
