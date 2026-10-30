#!/usr/bin/env bash
# Core adjacent-use forwarding: Common owns liveness, Loong owns $t0.
set -Eeuo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { echo "CORE_GP_FORWARD=FAIL $*" >&2; exit 1; }

test -x "$minic" || fail 'MiniC compiler missing'
"${HOST_CC:-cc}" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/core_gp_forward.c" -o "$work/source.i"
"$minic" --target=loongarch64-linux -S "$work/source.i" \
    -o "$work/loong.s" || fail 'LoongArch MiniC compile'
"$minic" --target=aarch64-linux -S "$work/source.i" \
    -o "$work/aarch64.s" || fail 'AArch64 untouched target compile'
awk '/^minic_gp_anti_chain:$/ {inside=1} inside {print}
     /^\.size minic_gp_anti_chain,/ {exit}' "$work/loong.s" > "$work/anti.s"

# Former LOAD→STORE GP SSA stack cycles here had six exact adjacent
# st.d/ld.d pairs. Core proves two pairs are single-use and can be elided.
roundtrips=$(awk '
    /^  st.d \$t0, \$sp, 16$/ {prior=1; next}
    prior {if ($0 == "  ld.d $t0, $sp, 16") n++; prior=0}
    END {print n+0}' "$work/anti.s")
test "$roundtrips" -le 4 || fail "adjacent SSA cycles remain: $roundtrips"
grep -Fq '  mul.d $t0, $t0, $t1' "$work/anti.s" \
    || fail 'multiply effect missing'
grep -Fq '  xor $t0, $t0, $t1' "$work/anti.s" \
    || fail 'XOR effect missing'
grep -Fq '  ld.d $t0, $sp,' "$work/anti.s" \
    || fail 'required stack loads missing'
if command -v loongarch64-linux-gnu-as >/dev/null 2>&1; then
    loongarch64-linux-gnu-as "$work/loong.s" -o "$work/loong.o" \
        || fail 'Loong GNU AS rejected forwarding'
fi
if command -v aarch64-linux-gnu-as >/dev/null 2>&1; then
    aarch64-linux-gnu-as "$work/aarch64.s" -o "$work/aarch64.o" \
        || fail 'AArch GNU AS rejected unchanged dispatch'
fi
"${HOST_CC:-cc}" -std=gnu11 -O2 "$root/tests/compiler/c0/core_gp_forward.c" \
    -o "$work/gnu.exe" || fail 'GNU reference build'
"$minic" --target=x86_64-linux -S "$work/source.i" -o "$work/x86.s" \
    || fail 'MiniC native target build'
"${HOST_CC:-cc}" -no-pie "$work/x86.s" -o "$work/minic.exe" \
    || fail 'GNU assembler/linker of MiniC native output'
gnu_status=0
"$work/gnu.exe" || gnu_status=$?
minic_status=0
"$work/minic.exe" || minic_status=$?
test "$gnu_status" -eq 42 && test "$minic_status" -eq "$gnu_status" \
    || fail "GNU/MiniC native runtime mismatch gnu=$gnu_status minic=$minic_status"
echo "CORE_GP_FORWARD=PASS loong_cycles=$roundtrips native_strict=42 aarch64_s=1"
