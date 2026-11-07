#!/usr/bin/env bash
# MC-076: execute identical GCC/MiniC C after relocating .head.text and data
# by the same displacement, checking both source/destination pointer roles.
set -Eeuo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
cc=${HOST_CC:-gcc}
work=$(mktemp -d)
trap 'rm -r -- "$work"' EXIT
fail() { printf 'P46_X86_HEAD_AGGREGATE=FAIL %s\n' "$*" >&2; exit 1; }

test -x "$minic" || fail 'MiniC compiler missing'
source="$root/tests/compiler/c0/p46_x86_head_aggregate.c"
driver="$root/tests/compiler/c0/p46_x86_head_aggregate_driver.c"
"$cc" -E -P -std=gnu11 "$source" -o "$work/common.i" || fail preprocessing
"$minic" --target=x86_64-linux -fno-PIE -S "$work/common.i" \
    -o "$work/minic.s" || fail 'MiniC assembly'
"$cc" -O0 -fno-PIE -no-pie -S "$work/common.i" \
    -o "$work/gnu.s" || fail 'GCC assembly'
"$cc" -no-pie -c -x assembler "$work/minic.s" \
    -o "$work/minic.o" || fail 'MiniC GNU AS'
"$cc" -no-pie -c -x assembler "$work/gnu.s" \
    -o "$work/gnu.o" || fail 'GCC GNU AS'

readelf -rW "$work/minic.o" > "$work/minic.reloc" || fail 'MiniC readelf'
readelf -rW "$work/gnu.o" > "$work/gnu.reloc" || fail 'GCC readelf'
for name in x86_p46_head_src x86_p46_head_dst; do
    grep -Eq "R_X86_64_PC32.*$name" "$work/minic.reloc" \
        || fail "MiniC lost physical $name PC32 access"
    grep -Eq "R_X86_64_PC32.*$name" "$work/gnu.reloc" \
        || fail "GCC lost physical $name PC32 access"
    grep -Eq "R_X86_64_32S.*$name" "$work/minic.reloc" \
        || fail "MiniC cast lost numeric link VMA $name"
done

"$cc" -no-pie "$driver" "$work/minic.o" -o "$work/minic.elf" \
    || fail 'MiniC GNU LD'
"$cc" -no-pie "$driver" "$work/gnu.o" -o "$work/gnu.elf" \
    || fail 'GCC GNU LD'
"$work/minic.elf" > "$work/minic.out" || {
    sed -n '1,2p' "$work/minic.out" >&2
    fail 'MiniC displaced runtime'
}
"$work/gnu.elf" > "$work/gnu.out" || {
    sed -n '1,2p' "$work/gnu.out" >&2
    fail 'GCC displaced runtime'
}
cmp "$work/minic.out" "$work/gnu.out" \
    || fail 'MiniC/GCC displaced outcome mismatch'
grep -Fxq 'HEAD_AGGREGATE displace_direct=1 displace_cast=1 displace_load=1' \
    "$work/minic.out" || fail 'incorrect physical/link VMA address roles'

"$minic" --target=x86_64-linux -fPIE -S "$work/common.i" \
    -o "$work/pie.s" || fail 'MiniC PIE'
"$cc" -c -x assembler "$work/pie.s" -o "$work/pie.o" \
    || fail 'PIE GNU AS'
readelf -rW "$work/pie.o" > "$work/pie.reloc" || fail 'PIE readelf'
if grep -Eq 'R_X86_64_32S.*x86_p46_head_(src|dst)' "$work/pie.reloc"; then
    fail 'PIE inherited non-PIE physical/link VMA policy'
fi

printf '%s\n' 'P46_X86_HEAD_AGGREGATE=PASS GCC_same_source=1 GNU_AS=3 GNU_LD=2 displaced_direct=1 displaced_cast=1 displaced_record_load=1 ELF_RIP_and_VMA=1 PIE_no_absolute=1'
