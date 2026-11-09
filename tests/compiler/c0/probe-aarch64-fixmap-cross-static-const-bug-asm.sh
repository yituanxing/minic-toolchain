#!/bin/sh
# Diagnostic RED until TU-local static specialization safely clones Linux's
# BUG_ON zero-operand ASM with its per-instance __bug_table numeric labels.
# Not a check-fast gate; no GCC C object fallback or alternate assembler.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mini=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host=${HOST_CC:-gcc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
source_file="$root/tests/compiler/c0/aarch64_fixmap_cross_static_const_bug_asm.c"

"$host" -E -P -std=gnu11 "$source_file" -o "$work/original.gnu.i"
"$host" -O2 -S -std=gnu11 "$source_file" -o "$work/gnu.s"
if grep -Fq '__compiletime_assert_fixmap' "$work/gnu.s"; then
    echo 'AARCH64_FIXMAP_BUG_ASM=FAIL GNU_O2 retained compiletime assert' >&2
    exit 1
fi
if ! "$mini" --target=aarch64-linux -S "$work/original.gnu.i" -o "$work/minic.s"; then
    echo 'AARCH64_FIXMAP_BUG_ASM=FAIL MiniC source/ASM lowering' >&2
    exit 1
fi
for phrase in '.pushsection __bug_table' '.pushsection .rodata.str' \
              '14470: .long 14471f - .' '14472: .string' \
              '.long 14472b - .' '14471: brk 0x800'; do
    if ! grep -Fq "$phrase" "$work/minic.s"; then
        echo "AARCH64_FIXMAP_BUG_ASM=FAIL lost original GNU ASM metadata: $phrase" >&2
        exit 1
    fi
done
asm_entry_count=$(grep -Fc '14471: brk 0x800' "$work/minic.s")
asm_table_count=$(grep -Fc '.pushsection __bug_table' "$work/minic.s")
if [ "$asm_entry_count" -lt 1 ] ||
   [ "$asm_entry_count" -ne "$asm_table_count" ]; then
    echo 'AARCH64_FIXMAP_BUG_ASM=FAIL missing/extra trap-to-bug-table owner' >&2
    exit 1
fi
gnu_as_status=SKIP
cross_as=${AARCH64_GNU_AS:-aarch64-linux-gnu-as}
cross_readelf=${AARCH64_GNU_READELF:-aarch64-linux-gnu-readelf}
cross_objcopy=${AARCH64_GNU_OBJCOPY:-aarch64-linux-gnu-objcopy}
if command -v "$cross_as" >/dev/null 2>&1 &&
   command -v "$cross_readelf" >/dev/null 2>&1 &&
   command -v "$cross_objcopy" >/dev/null 2>&1; then
    "$cross_as" "$work/minic.s" -o "$work/minic.o"
    "$cross_readelf" -rW "$work/minic.o" >"$work/relocations.txt"
    "$cross_objcopy" --dump-section "__bug_table=$work/bug_table.bin" \
        "$work/minic.o"
    entry_bytes=$(wc -c <"$work/bug_table.bin")
    relocation_count=$(awk '/^Relocation section .*\.rela__bug_table/ { active=1; next }
        /^Relocation section / { active=0 }
        active && /R_AARCH64_PREL32/ { count++ }
        END { print count+0 }' "$work/relocations.txt")
    if [ "$entry_bytes" -ne "$((12 * asm_entry_count))" ] ||
       [ "$relocation_count" -ne "$((2 * asm_entry_count))" ]; then
        echo "AARCH64_FIXMAP_BUG_ASM=FAIL __bug_table GNU ELF entries/relocs: bytes=$entry_bytes relocs=$relocation_count asm_entries=$asm_entry_count" >&2
        exit 1
    fi
    gnu_as_status=PASS
fi
if grep -Fq '__compiletime_assert_fixmap' "$work/minic.s"; then
    echo "AARCH64_FIXMAP_BUG_ASM=RED GNU_O2=PASS MiniC_ASM=PASS GNU_AS=$gnu_as_status static_const_specialization=FAIL" >&2
    exit 1
fi
grep -Fq '  bl original_set_fixmap' "$work/minic.s"
# Unproven ASM must never inherit the narrow self-contained metadata permit.
# Two malformed bug-table forms fail closed at target qualification, while a
# valid but unrelated NOP still leaves the non-inline static wrapper un-cloned.
sed 's/__bug_table/__bad_bug_table/g' "$work/original.gnu.i" >"$work/bad-section.i"
if "$mini" --target=aarch64-linux -S "$work/bad-section.i" \
        -o "$work/bad-section.s" >/dev/null 2>&1; then
    echo 'AARCH64_FIXMAP_BUG_ASM=FAIL accepted a foreign bug table section' >&2
    exit 1
fi
sed 's/14471f/14473f/g' "$work/original.gnu.i" >"$work/bad-numeric.i"
if "$mini" --target=aarch64-linux -S "$work/bad-numeric.i" \
        -o "$work/bad-numeric.s" >/dev/null 2>&1; then
    echo 'AARCH64_FIXMAP_BUG_ASM=FAIL accepted unresolved numeric label' >&2
    exit 1
fi
sed -E 's/asm volatile\(.*\); __builtin_unreachable/asm volatile("nop"); __builtin_unreachable/' \
    "$work/original.gnu.i" >"$work/unproven-nop.i"
"$mini" --target=aarch64-linux -S "$work/unproven-nop.i" -o "$work/unproven-nop.s"
if ! grep -Fq '__compiletime_assert_fixmap' "$work/unproven-nop.s"; then
    echo 'AARCH64_FIXMAP_BUG_ASM=FAIL generic ASM escaped static clone safety gate' >&2
    exit 1
fi
echo "AARCH64_FIXMAP_BUG_ASM=PASS GNU_O2=1 MiniC_ASM=1 GNU_AS=$gnu_as_status static_const_specialization=1 strict_negative=3 no_GNU_C_fallback=1"
