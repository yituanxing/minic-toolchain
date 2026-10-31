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
mixed_physical=$(sed -n '/\.rela.head.text/,/Relocation section/p' "$work/head.reloc" \
    | grep -Ec 'R_X86_64_PC32.*kernel_head_mixed_global' || true)
mixed_link_vma=$(sed -n '/\.rela.head.text/,/Relocation section/p' "$work/head.reloc" \
    | grep -Ec 'R_X86_64_32S.*kernel_head_mixed_global' || true)
test "$mixed_physical" -ge 2 && test "$mixed_link_vma" -eq 2 \
    || fail 'same head global lost its two physical-store/link-VMA address roles'
# The .head.text call graph extends into unannotated static helpers. Their
# fixup_pointer inputs still need link VMAs (32S); a static helper also called
# from normal .text must remain ordinary PC-relative instead.
sed -n '/^kernel_head_only_helper:$/,/^\.size kernel_head_only_helper,/p' "$work/head.s" \
    | grep -Eq 'movq \$kernel_head_helper_target, %rax' \
    || fail 'head-only static helper lost link-VMA global producer'
sed -n '/^kernel_head_shared_helper:$/,/^\.size kernel_head_shared_helper,/p' "$work/head.s" \
    | grep -Eq 'leaq kernel_head_helper_target\(%rip\), %rax' \
    || fail 'shared static helper incorrectly inherited early-head role'
sed -n '/\.rela.text/,/Relocation section/p' "$work/head.reloc" \
    | grep -Eq 'R_X86_64_32S.*kernel_head_helper_target' \
    || fail 'head-only static helper did not produce ELF absolute relocation'
sed -n '/\.rela.text/,/Relocation section/p' "$work/head.reloc" \
    | grep -Eq 'R_X86_64_PC32.*kernel_head_helper_target' \
    || fail 'normal shared static helper lost PC-relative ELF relocation'
sed -n '/\.rela.text/,/Relocation section/p' "$work/head.reloc" \
    | grep -Eq 'R_X86_64_PC32.*kernel_link_address_probe_anchor' \
    || fail 'regular .text changed its established PC-relative relocation'
"$cc" -no-pie "$driver" "$work/head.o" -o "$work/relocated" \
    || fail 'GNU link failed'
"$work/relocated" > "$work/actual" \
    || fail 'relocated early-head function lost link-time symbol address'

# GNU's own compiler independently demonstrates that the same C global has
# both direct physical memory and link-VMA address roles. Also run the same
# displaced code+data runtime oracle with its output, not only MiniC's.
"$cc" -std=gnu11 -O0 -fno-PIE -S "$work/probe.i" -o "$work/gnu-head.s" \
    || fail 'GNU baseline x86 assembly'
"$cc" -std=gnu11 -O0 -fno-PIE -mcmodel=kernel -S "$work/probe.i" \
    -o "$work/gnu-kernel.s" || fail 'GNU kernel-code-model assembly'
sed -n '/^kernel_head_only_helper:$/,/^\s*\.size\s*kernel_head_only_helper,/p' "$work/gnu-kernel.s" \
    | grep -Eq 'movq[[:space:]]+\$kernel_head_helper_target,' \
    || fail 'GNU kernel model does not confirm helper link-VMA address role'
"$cc" -no-pie -x assembler -c "$work/gnu-head.s" -o "$work/gnu-head.o" \
    || fail 'GNU baseline object'
readelf -rW "$work/gnu-head.o" > "$work/gnu-head.reloc" \
    || fail 'GNU baseline readelf'
sed -n '/\.rela.head.text/,/Relocation section/p' "$work/gnu-head.reloc" \
    | grep -Eq 'R_X86_64_PC32.*kernel_head_mixed_global' \
    || fail 'GNU baseline lost head mixed physical address'
sed -n '/\.rela.head.text/,/Relocation section/p' "$work/gnu-head.reloc" \
    | grep -Eq 'R_X86_64_32(S)?[[:space:]].*kernel_head_mixed_global' \
    || fail 'GNU baseline lost head mixed link VMA'
"$cc" -no-pie "$driver" "$work/gnu-head.o" -o "$work/gnu-relocated" \
    || fail 'GNU baseline link'
"$work/gnu-relocated" > "$work/gnu-actual" \
    || fail 'GNU baseline displaced runtime'

"$minic" --target=x86_64-linux -fPIE -S "$work/probe.i" -o "$work/pie.s" \
    || fail 'MiniC PIE x86 assembly'
"$cc" -x assembler -c "$work/pie.s" -o "$work/pie.o" \
    || fail 'GNU assembler rejected x86 PIE'
readelf -rW "$work/pie.o" > "$work/pie.reloc" || fail 'readelf PIE'
if sed -n '/\.rela.head.text/,/Relocation section/p' "$work/pie.reloc" \
    | grep -Eq 'R_X86_64_32S.*kernel_link_address_probe_anchor'; then
    fail 'PIE .head.text unexpectedly received kernel absolute relocation'
fi
echo 'X86_KERNEL_HEAD_LINK_ADDRESS=PASS head_link=R_X86_64_32S head_direct=R_X86_64_PC32 head_mixed=PC32+32S helper_closure=32S shared_helper=PC32 normal=R_X86_64_PC32 PIE=no_32S relocated_runtime=PASS relocated_mixed=PASS gnu_same_source=PASS'
