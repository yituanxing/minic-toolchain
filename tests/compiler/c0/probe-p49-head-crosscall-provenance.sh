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

# P50: a returned pointer passes through a volatile local pointer slot
# before the caller interprets it numerically.  Same GNU source, real
# physical-displacement execution and the same fail-closed contract as P49.
local_source="$root/tests/compiler/c0/p50_x86_head_return_local_alias.c"
"$cc" -E -P -std=gnu11 "$local_source" -o "$work/local.i" \
    || fail local-CPP
"$cc" -O2 -fno-PIE -mcmodel=kernel -S "$work/local.i" \
    -o "$work/local.gnu.s" || fail local-GCC-O2
"$cc" -c -x assembler "$work/local.gnu.s" -o "$work/local.gnu.o" \
    || fail local-GNU-AS
readelf -rW "$work/local.gnu.o" > "$work/local.gnu.reloc" \
    || fail local-readelf
grep -Eq 'R_X86_64_PC32.*p49_head_global' "$work/local.gnu.reloc" \
    || fail 'GNU local-return store lost physical PC32'
grep -Eq 'R_X86_64_32S.*p49_head_global' "$work/local.gnu.reloc" \
    || fail 'GNU local-return helper argument lost link VMA'
"$cc" -O2 -DP49_POINTER_RETURN -fno-PIE -no-pie "$driver" \
    "$work/local.gnu.o" -o "$work/local.gnu.elf" \
    || fail local-GNU-LD
"$work/local.gnu.elf" > "$work/local.gnu.out" \
    || fail local-GNU-runtime
grep -q '^P49_HEAD_CROSSCALL=PASS ' "$work/local.gnu.out" \
    || fail local-GNU-displaced-oracle
if "$minic" --target=x86_64-linux -fno-PIE -S "$work/local.i" \
    -o "$work/local.minic.s" > "$work/local.minic.stdout" \
    2> "$work/local.minic.stderr"; then
    fail 'MiniC silently accepted locally reloaded numeric physical pointer'
fi
grep -Eiq 'early head unproven cross-function numeric pointer provenance' \
    "$work/local.minic.stderr" \
    || fail 'missing local-return provenance diagnostic'
test ! -s "$work/local.minic.s" \
    || fail 'partial local-return assembly escaped refusal'

# P51: the first helper returns another helper's CALL result.  The same
# source must succeed under the GNU link-VMA/physical-store displaced oracle,
# but MiniC must conservatively refuse an unproven two-hop return alias.
nested_source="$root/tests/compiler/c0/p51_x86_head_nested_return_alias.c"
"$cc" -E -P -std=gnu11 "$nested_source" -o "$work/nested.i" \
    || fail nested-CPP
"$cc" -O2 -fno-PIE -mcmodel=kernel -S "$work/nested.i" \
    -o "$work/nested.gnu.s" || fail nested-GCC-O2
"$cc" -c -x assembler "$work/nested.gnu.s" -o "$work/nested.gnu.o" \
    || fail nested-GNU-AS
readelf -rW "$work/nested.gnu.o" > "$work/nested.gnu.reloc" \
    || fail nested-readelf
grep -Eq 'R_X86_64_PC32.*p49_head_global' "$work/nested.gnu.reloc" \
    || fail 'GNU nested store lost physical PC32'
grep -Eq 'R_X86_64_32S.*p49_head_global' "$work/nested.gnu.reloc" \
    || fail 'GNU nested helper argument lost absolute link VMA'
"$cc" -O2 -DP49_POINTER_RETURN -DP51_NESTED_RETURN -fno-PIE -no-pie \
    "$driver" "$work/nested.gnu.o" -o "$work/nested.gnu.elf" \
    || fail nested-GNU-LD
"$work/nested.gnu.elf" > "$work/nested.gnu.out" || fail nested-GNU-runtime
grep -q '^P49_HEAD_CROSSCALL=PASS ' "$work/nested.gnu.out" \
    || fail nested-GNU-displaced-oracle
if "$minic" --target=x86_64-linux -fno-PIE -S "$work/nested.i" \
    -o "$work/nested.minic.s" > "$work/nested.minic.stdout" \
    2> "$work/nested.minic.stderr"; then
    fail 'MiniC silently accepted two-hop numeric physical pointer alias'
fi
grep -Eiq 'early head unproven cross-function numeric pointer provenance' \
    "$work/nested.minic.stderr" \
    || fail 'missing two-hop pointer provenance diagnostic'
test ! -s "$work/nested.minic.s" \
    || fail 'partial two-hop assembly escaped rejection'

# P52: function-pointer INDIRECT_CALL.  GNU emits separate physical
# PC32 STORE and absolute 32S link-VMA address; execute its *cloned* code
# and data through the existing displaced GNU native oracle.  MiniC must
# decline the unproven pointer-return/numeric-use path without any output.
indirect_source="$root/tests/compiler/c0/p52_x86_head_indirect_return_alias.c"
"$cc" -E -P -std=gnu11 "$indirect_source" -o "$work/indirect.i" \
    || fail indirect-CPP
"$cc" -O2 -fno-PIE -mcmodel=kernel -S "$work/indirect.i" \
    -o "$work/indirect.gnu.s" || fail indirect-GCC-O2
"$cc" -c -x assembler "$work/indirect.gnu.s" -o "$work/indirect.gnu.o" \
    || fail indirect-GNU-AS
readelf -rW "$work/indirect.gnu.o" > "$work/indirect.gnu.reloc" \
    || fail indirect-readelf
grep -Eq 'R_X86_64_PC32.*p49_head_global' "$work/indirect.gnu.reloc" \
    || fail 'GNU indirect store lost physical PC32'
grep -Eq 'R_X86_64_32S.*p49_head_global' "$work/indirect.gnu.reloc" \
    || fail 'GNU indirect argument lost absolute link VMA'
"$cc" -O2 -DP49_POINTER_RETURN -fno-PIE -no-pie "$driver" \
    "$work/indirect.gnu.o" -o "$work/indirect.gnu.elf" \
    || fail indirect-GNU-LD
"$work/indirect.gnu.elf" > "$work/indirect.gnu.out" \
    || fail indirect-GNU-displaced-runtime
grep -q '^P49_HEAD_CROSSCALL=PASS ' "$work/indirect.gnu.out" \
    || fail indirect-GNU-displaced-oracle
if "$minic" --target=x86_64-linux -fno-PIE -S "$work/indirect.i" \
    -o "$work/indirect.minic.s" > "$work/indirect.minic.stdout" \
    2> "$work/indirect.minic.stderr"; then
    fail 'MiniC silently accepted indirect physical-to-numeric pointer'
fi
grep -Eiq 'early head unproven cross-function numeric pointer provenance' \
    "$work/indirect.minic.stderr" \
    || fail 'missing indirect-call provenance diagnostic'
test ! -s "$work/indirect.minic.s" \
    || fail 'partial indirect-call assembly escaped rejection'

# P53: external direct callee has no Core body to inspect.  This source
# has a real GNU ELF relocation oracle, but cannot use the displaced
# runtime driver without introducing a different external linkage model.
external_source="$root/tests/compiler/c0/p53_x86_head_external_return_alias.c"
"$cc" -E -P -std=gnu11 "$external_source" -o "$work/external.i" \
    || fail external-CPP
"$cc" -O2 -fno-PIE -mcmodel=kernel -S "$work/external.i" \
    -o "$work/external.gnu.s" || fail external-GCC-O2
"$cc" -c -x assembler "$work/external.gnu.s" -o "$work/external.gnu.o" \
    || fail external-GNU-AS
readelf -rW "$work/external.gnu.o" > "$work/external.gnu.reloc" \
    || fail external-readelf
grep -Eq 'R_X86_64_PC32.*p49_head_global' "$work/external.gnu.reloc" \
    || fail 'GNU external store lost physical PC32'
grep -Eq 'R_X86_64_32S.*p49_head_global' "$work/external.gnu.reloc" \
    || fail 'GNU external argument lost absolute link VMA'
if "$minic" --target=x86_64-linux -fno-PIE -S "$work/external.i" \
    -o "$work/external.minic.s" > "$work/external.minic.stdout" \
    2> "$work/external.minic.stderr"; then
    fail 'MiniC silently accepted external physical-to-numeric pointer'
fi
grep -Eiq 'early head unproven cross-function numeric pointer provenance' \
    "$work/external.minic.stderr" \
    || fail 'missing external-call provenance diagnostic'
test ! -s "$work/external.minic.s" \
    || fail 'partial external-call assembly escaped rejection'

# P54 positive: external helper with no numeric pointer result must
# remain admitted.  Assembling with unresolved external relocations is
# valid here; no runtime semantic proof of that external body is claimed.
physical_source="$root/tests/compiler/c0/p54_x86_head_external_physical_only.c"
"$cc" -E -P -std=gnu11 "$physical_source" \
    -o "$work/external-physical.i" || fail physical-external-CPP
"$minic" --target=x86_64-linux -fno-PIE -S \
    "$work/external-physical.i" -o "$work/external-physical.s" \
    || fail 'physical-only external helper incorrectly rejected'
"$cc" -c -x assembler "$work/external-physical.s" \
    -o "$work/external-physical.o" || fail physical-external-GNU-AS
readelf -rW "$work/external-physical.o" \
    > "$work/external-physical.reloc" || fail physical-external-readelf
grep -Eq 'R_X86_64_PC32.*p54_physical_global' \
    "$work/external-physical.reloc" \
    || fail 'physical-only external helper lost PC32 data store'

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

printf '%s\n' 'P49_X86_HEAD_CROSSCALL=PASS GNU_O2_same_source_displaced=5 MiniC_numeric_alias_fail_closed=6 MiniC_physical_helper_admitted=2 GNU_external_ELF_only=1 GNU_AS_LD=1 ELF_PC32_and_32S=1 RTL_PREMMU=NOT_RUN'
