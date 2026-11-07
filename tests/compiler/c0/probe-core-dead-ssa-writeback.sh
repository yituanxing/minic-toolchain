#!/usr/bin/env bash
# The target-independent Core use census suppresses only dead SSA copies.
# LoongArch keeps real ALU side effects and memory stores unchanged.
set -Eeuo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { echo "CORE_DEAD_SSA_WRITEBACK=FAIL $*" >&2; exit 1; }
test -x "$minic" || fail 'MiniC compiler missing'
"${HOST_CC:-cc}" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/core_dead_ssa_writeback.c" -o "$work/dead.i"
"$minic" --target=loongarch64-linux -S "$work/dead.i" -o "$work/loong.s"
"$minic" --target=aarch64-linux -S "$work/dead.i" -o "$work/a64.s"
awk '/^dead_add:$/ {inside=1} inside {print} /^\.size dead_add,/ {exit}' \
    "$work/loong.s" > "$work/dead.s"
awk '/^live_store:$/ {inside=1} inside {print} /^\.size live_store,/ {exit}' \
    "$work/loong.s" > "$work/live.s"
grep -Fq '  add.d $t0, $t0, $t1' "$work/dead.s" || fail 'dead ALU instruction missing'
grep -Eq '^  (xor \$t0, \$t0, \$t1|xori \$t0, \$t0, 17)$' "$work/dead.s" ||
    fail 'dead XOR instruction missing'
awk '
    /  add\.d \$t0, \$t0, \$t1$/ ||
    /  xor \$t0, \$t0, \$t1$/ ||
    /  xori \$t0, \$t0, 17$/ {
        if (getline <= 0 || $0 ~ /^  st\.[wd] \$t0, /) exit 1
        seen++
    }
    END {if (seen != 2) exit 1}
' "$work/dead.s" || fail 'unused Core result still stored to frame'
awk '
    /  add\.d \$t0, \$t0, \$t1$/ {
        if (getline <= 0 || $0 !~ /^  st\.d \$t0, /) exit 1
        found=1
    }
    END {if (!found) exit 1}
' "$work/live.s" || fail 'live Core result writeback removed'
grep -Fq '  st.d $t0, $t1, 0' "$work/live.s" || fail 'observable store missing'
test "$(wc -l < "$work/dead.s")" -le 37 || fail 'dead result code-size regression'
if command -v loongarch64-linux-gnu-as >/dev/null 2>&1; then
    loongarch64-linux-gnu-as "$work/loong.s" -o "$work/loong.o"
fi
if command -v aarch64-linux-gnu-as >/dev/null 2>&1; then
    aarch64-linux-gnu-as "$work/a64.s" -o "$work/a64.o"
fi
echo "CORE_DEAD_SSA_WRITEBACK=PASS loong_lines=$(wc -l < "$work/dead.s") dead_gp_stores=0 live_gp_store=1 aarch64_compile=1"
