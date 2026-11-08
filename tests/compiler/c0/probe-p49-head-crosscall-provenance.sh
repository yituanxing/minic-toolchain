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

# The direct callee may only return a pointer, with the numeric cast
# performed after the CALL by its caller.  Verify the GNU displaced oracle
# independently and refuse the unsafe physical pointer round-trip in MiniC.
return_source="$root/tests/compiler/c0/p49_x86_head_return_alias.c"
"$cc" -E -P -std=gnu11 "$return_source" -o "$work/return.i" \
    || fail return-CPP
"$cc" -O2 -fno-PIE -mcmodel=kernel -S "$work/return.i" \
    -o "$work/return.gnu.s" || fail return-GCC-O2
"$cc" -c -x assembler "$work/return.gnu.s" -o "$work/return.gnu.o" \
    || fail return-GNU-AS
readelf -rW "$work/return.gnu.o" > "$work/return.gnu.reloc" \
    || fail return-readelf
grep -Eq 'R_X86_64_PC32.*p49_head_global' "$work/return.gnu.reloc" \
    || fail 'GNU return-alias store lost physical PC32'
grep -Eq 'R_X86_64_32S.*p49_head_global' "$work/return.gnu.reloc" \
    || fail 'GNU return-alias helper argument lost link VMA'
"$cc" -O2 -DP49_POINTER_RETURN -fno-PIE -no-pie "$driver" \
    "$work/return.gnu.o" -o "$work/return.gnu.elf" || fail return-GNU-LD
"$work/return.gnu.elf" > "$work/return.gnu.out" || fail return-GNU-runtime
grep -q '^P49_HEAD_CROSSCALL=PASS ' "$work/return.gnu.out" \
    || fail return-GNU-displaced-oracle
if "$minic" --target=x86_64-linux -fno-PIE -S "$work/return.i" \
    -o "$work/return.minic.s" > "$work/return.minic.stdout" \
    2> "$work/return.minic.stderr"; then
    fail 'MiniC silently accepted returned numeric physical pointer'
fi
grep -Eiq 'early head unproven cross-function numeric pointer provenance' \
    "$work/return.minic.stderr" || fail 'missing return-alias diagnostic'
test ! -s "$work/return.minic.s" || fail 'partial return-alias assembly escaped refusal'

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

printf '%s\n' 'P49_X86_HEAD_CROSSCALL=PASS GNU_O2_same_source_displaced=2 MiniC_numeric_alias_fail_closed=2 MiniC_physical_helper_admitted=1 GNU_AS_LD=1 ELF_PC32_and_32S=1 RTL_PREMMU=NOT_RUN'
