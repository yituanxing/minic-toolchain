#!/usr/bin/env bash
# MC-076/P49: same GNU input has genuine pre-MMU physical STORE + link-VMA
# helper argument. Until cross-function Core parameter provenance exists,
# MiniC must fail closed on the observed numeric alias, but it must still
# admit a helper that only dereferences the incoming physical pointer.
set -Eeuo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
cc=${HOST_CC:-gcc}
work=$(mktemp -d)
trap 'rm -r -- "$work"' EXIT
fail() { printf 'P49_X86_HEAD_CROSSCALL=FAIL %s\n' "$*" >&2; exit 1; }

test -x "$minic" || fail 'MiniC compiler missing'
test "$(uname -m)" = x86_64 || fail 'requires native x86_64'
for tool in "$cc" readelf; do
    command -v "$tool" >/dev/null || fail "missing required GNU tool=$tool"
done
source="$root/tests/compiler/c0/p49_x86_head_crosscall_provenance.c"
driver="$root/tests/compiler/c0/p49_x86_head_crosscall_driver.c"
"$cc" -E -P -std=gnu11 "$source" -o "$work/common.i" || fail GNU-CPP

"$cc" -O2 -fno-PIE -mcmodel=kernel -S "$work/common.i" \
    -o "$work/gnu.s" || fail GCC-O2
"$cc" -c -x assembler "$work/gnu.s" -o "$work/gnu.o" || fail GNU-AS
readelf -rW "$work/gnu.o" > "$work/gnu.reloc" || fail readelf
grep -Eq 'R_X86_64_PC32.*p49_head_global' "$work/gnu.reloc" \
    || fail 'GNU direct store lost physical PC32'
grep -Eq 'R_X86_64_32S.*p49_head_global' "$work/gnu.reloc" \
    || fail 'GNU helper argument lost absolute link VMA'
"$cc" -O2 -fno-PIE -no-pie "$driver" "$work/gnu.o" \
    -o "$work/gnu.elf" || fail GNU-LD
"$work/gnu.elf" > "$work/gnu.out" || fail GNU-displaced-runtime
grep -q '^P49_HEAD_CROSSCALL=PASS ' "$work/gnu.out" \
    || fail GNU-displaced-oracle

if "$minic" --target=x86_64-linux -fno-PIE -S "$work/common.i" \
    -o "$work/minic.s" > "$work/minic.stdout" \
    2> "$work/minic.stderr"; then
    fail 'MiniC silently accepted numeric cross-function physical pointer'
fi
grep -Eiq 'early head unproven cross-function numeric pointer provenance' \
    "$work/minic.stderr" || fail 'missing precise fail-closed diagnostic'
test ! -s "$work/minic.s" || fail 'partial assembly escaped refusal'

"$cc" -E -P -std=gnu11 \
    "$root/tests/compiler/c0/p49_x86_head_physical_helper.c" \
    -o "$work/physical.i" || fail physical-CPP
"$minic" --target=x86_64-linux -fno-PIE -S "$work/physical.i" \
    -o "$work/physical.s" || fail 'physical helper incorrectly rejected'
"$cc" -c -x assembler "$work/physical.s" -o "$work/physical.o" \
    || fail physical-GNU-AS
readelf -rW "$work/physical.o" > "$work/physical.reloc" || fail physical-readelf
grep -Eq 'R_X86_64_PC32.*p49_physical_global' "$work/physical.reloc" \
    || fail 'physical helper lost PC32 direct global'

printf '%s\n' 'P49_X86_HEAD_CROSSCALL=PASS GNU_O2_same_source_displaced=1 MiniC_numeric_alias_fail_closed=1 MiniC_physical_helper_admitted=1 GNU_AS_LD=1 ELF_PC32_and_32S=1 RTL_PREMMU=NOT_RUN'
