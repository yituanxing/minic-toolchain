#!/usr/bin/env bash
set -Eeuo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
cc=${HOST_CC:-gcc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { echo "X86_KERNEL_HEAD_LINK_ADDRESS=FAIL $*" >&2; exit 1; }

test -x "$minic" || fail 'MiniC compiler missing'
src="$root/tests/compiler/c0/x86_kernel_head_link_address.c"
driver="$root/tests/compiler/c0/x86_kernel_head_link_address_driver.c"
"$cc" -E -P -std=gnu11 "$src" -o "$work/probe.i" \
    || fail 'GNU preprocessing'
"$minic" --target=x86_64-linux -fno-PIE -S "$work/probe.i" -o "$work/head.s" \
    || fail 'MiniC non-PIE x86 assembly'
"$cc" -no-pie -x assembler -c "$work/head.s" -o "$work/head.o" \
    || fail 'GNU assembler rejected x86 non-PIE'
readelf -rW "$work/head.o" > "$work/head.reloc" || fail 'readelf non-PIE'
sed -n '/\.rela.head.text/,/Relocation section/p' "$work/head.reloc" \
    | grep -Eq 'R_X86_64_32S.*kernel_link_address_probe_anchor' \
    || fail '.head.text global is not GNU absolute signed32 relocation'
sed -n '/\.rela.head.text/,/Relocation section/p' "$work/head.reloc" \
    | grep -Eq 'R_X86_64_PC32.*kernel_head_direct_descriptor' \
    || fail '.head.text direct data access did not get RIP-relative relocation'
if sed -n '/\.rela.head.text/,/Relocation section/p' "$work/head.reloc" \
    | grep -Eq 'R_X86_64_32S.*kernel_head_direct_descriptor'; then
    fail '.head.text dereferenced global still has unmapped high-half address'
fi
sed -n '/\.rela.text/,/Relocation section/p' "$work/head.reloc" \
    | grep -Eq 'R_X86_64_PC32.*kernel_link_address_probe_anchor' \
    || fail 'regular .text changed its established PC-relative relocation'
"$cc" -no-pie "$driver" "$work/head.o" -o "$work/relocated" \
    || fail 'GNU link failed'
"$work/relocated" > "$work/actual" \
    || fail 'relocated early-head function lost link-time symbol address'

"$minic" --target=x86_64-linux -fPIE -S "$work/probe.i" -o "$work/pie.s" \
    || fail 'MiniC PIE x86 assembly'
"$cc" -x assembler -c "$work/pie.s" -o "$work/pie.o" \
    || fail 'GNU assembler rejected x86 PIE'
readelf -rW "$work/pie.o" > "$work/pie.reloc" || fail 'readelf PIE'
if sed -n '/\.rela.head.text/,/Relocation section/p' "$work/pie.reloc" \
    | grep -Eq 'R_X86_64_32S.*kernel_link_address_probe_anchor'; then
    fail 'PIE .head.text unexpectedly received kernel absolute relocation'
fi
echo 'X86_KERNEL_HEAD_LINK_ADDRESS=PASS head_link=R_X86_64_32S head_direct=R_X86_64_PC32 normal=R_X86_64_PC32 PIE=no_32S relocated_runtime=PASS'
