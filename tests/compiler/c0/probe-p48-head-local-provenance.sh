#!/usr/bin/env bash
# MC-076 / P48: one fail-closed x86 .head.text memory-provenance family.
# GNU AS/LD and the physically displaced native runner distinguish linked
# numeric VMA from the cloned pre-MMU physical data object.  GCC -O2 is a
# positive control where it produces that intended effect; GCC -O0 is NOT a
# displaced golden for pointer values deliberately stored in local memory.
set -Eeuo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
cc=${HOST_CC:-gcc}
work=$(mktemp -d)
trap 'rm -r -- "$work"' EXIT
fail() { printf 'P48_X86_HEAD_LOCAL_PROVENANCE=FAIL %s\n' "$*" >&2; exit 1; }

test -x "$minic" || fail 'MiniC compiler missing'
test "$(uname -m)" = x86_64 || fail 'requires native x86_64'
for tool in "$cc" readelf nm; do
    command -v "$tool" >/dev/null || fail "missing required GNU tool=$tool"
done
source="$root/tests/compiler/c0/p48_x86_head_local_provenance.c"
driver="$root/tests/compiler/c0/p48_x86_head_local_provenance_driver.c"
"$cc" -E -P -std=gnu11 "$source" -o "$work/common.i" || fail 'GNU CPP'

"$minic" --target=x86_64-linux -fno-PIE -S "$work/common.i" \
    -o "$work/minic.s" || fail 'MiniC no-PIE source'
"$cc" -O2 -fno-PIE -no-pie -S "$work/common.i" \
    -o "$work/gnu-O2.s" || fail 'GCC -O2 same-source'
"$cc" -fno-PIE -no-pie -c -x assembler "$work/minic.s" \
    -o "$work/minic.o" || fail 'MiniC GNU AS'
"$cc" -fno-PIE -no-pie -c -x assembler "$work/gnu-O2.s" \
    -o "$work/gnu-O2.o" || fail 'GCC GNU AS'

readelf -rW "$work/minic.o" > "$work/minic.reloc" || fail 'MiniC ELF reloc'
readelf -rW "$work/gnu-O2.o" > "$work/gnu-O2.reloc" \
    || fail 'GCC ELF reloc'
for source_kind in minic gnu-O2; do
    grep -Eq 'R_X86_64_PC32.*p48_head_cells' "$work/$source_kind.reloc" \
        || fail "$source_kind lost physical PC32 .head.data access"
    grep -Eq 'R_X86_64_32(S)?.*p48_head_cells' "$work/$source_kind.reloc" \
        || fail "$source_kind lost numeric link VMA .head.data value"
done

# The displacement driver clones at most 1 KiB of machine code starting at
# the tested symbol. Fail if any function grows beyond that boundary.
for source_kind in minic gnu-O2; do
    while read -r address size kind symbol; do
        case "$symbol" in
            p48_head_*)
                case "$kind" in T|t) ;; *) continue ;; esac
                [[ "$size" =~ ^[[:xdigit:]]+$ ]] \
                    || fail "$source_kind invalid nm size for $symbol"
                test "$((16#$size))" -le 1024 \
                    || fail "$source_kind displaced function too large: $symbol"
                ;;
        esac
    done < <(nm --defined-only --print-size "$work/$source_kind.o")
done

"$cc" -std=gnu11 -O2 -Wall -Wextra -Werror -fno-PIE -no-pie \
    "$driver" "$work/minic.o" -o "$work/minic.elf" \
    || fail 'MiniC GNU LD + driver'
"$cc" -std=gnu11 -O2 -Wall -Wextra -Werror -DP48_GNU_GOLDEN=1 \
    -fno-PIE -no-pie "$driver" "$work/gnu-O2.o" \
    -o "$work/gnu-O2.elf" || fail 'GCC GNU LD + driver'

"$work/gnu-O2.elf" > "$work/gnu-O2.out" || {
    sed -n '1,4p' "$work/gnu-O2.out" >&2
    fail 'GCC -O2 displaced positive controls'
}
grep -Fxq 'P48_HEAD_LOCAL gnu_baseline=1 gnu_plain_local=1 normal=1' \
    "$work/gnu-O2.out" || fail 'GCC -O2 same-source controls'

# Verify the unrelated position-independent and ordinary .text contracts
# before the intentionally difficult pre-MMU runtime cases below.
"$minic" --target=x86_64-linux -fPIE -S "$work/common.i" \
    -o "$work/pie.s" || fail 'MiniC PIE source'
"$cc" -fPIE -c -x assembler "$work/pie.s" -o "$work/pie.o" \
    || fail 'PIE GNU AS'
readelf -rW "$work/pie.o" > "$work/pie.reloc" || fail 'PIE ELF reloc'
if grep -Eq 'R_X86_64_32S?.*p48_head_(cells|other)' "$work/pie.reloc"; then
    fail 'PIE inherited early-head absolute link-VMA relocation'
fi
"$cc" -std=gnu11 -O2 -Wall -Wextra -Werror -Wno-unused-function \
    -DP48_NATIVE_ONLY=1 -fPIE -pie "$driver" "$work/pie.o" \
    -o "$work/pie.elf" || fail 'PIE GNU LD + driver'
"$work/pie.elf" > "$work/pie.out" || fail 'PIE native .text runtime'
grep -Fxq 'P48_HEAD_LOCAL pic_normal=1' "$work/pie.out" \
    || fail 'ordinary .text pointer local changed under PIE'

"$work/minic.elf" > "$work/minic.out" || {
    sed -n '1,4p' "$work/minic.out" >&2
    fail 'MiniC pre-MMU displaced native runtime'
}
grep -Fxq 'P48_HEAD_LOCAL baseline=1 plain=1 local=1 roundtrip0=1 roundtrip1=1 branch0=1 branch1=1 record=1 overflow=1 normal=1' \
    "$work/minic.out" || fail 'MiniC pointer transport / VMA oracle'

# Conflicting-origin and address-escaping local pointer slots are not proven
# physically safe; the x86 target MUST reject instead of silently assigning
# one global's physical policy. GCC compiles both identical, valid C source
# cases for comparison, but the pre-MMU execution semantics are deliberately
# not claimed.  Compile separately so either bad admission is observable.
for case in CONFLICT ALIAS; do
    label=$(printf '%s' "$case" | tr '[:upper:]' '[:lower:]')
    "$cc" -E -P -std=gnu11 "-DP48_CASE_$case" \
        "$root/tests/compiler/c0/p48_x86_head_local_uncertain.c" \
        -o "$work/$label.i" || fail "GCC CPP negative=$label"
    "$cc" -O2 -fno-PIE -no-pie -S "$work/$label.i" \
        -o "$work/$label.gnu.s" || fail "GCC same-source negative=$label"
    "$cc" -c -x assembler "$work/$label.gnu.s" \
        -o "$work/$label.gnu.o" || fail "GNU AS negative=$label"
    if "$minic" --target=x86_64-linux -fno-PIE -S "$work/$label.i" \
        -o "$work/$label.minic.s" > "$work/$label.stdout" \
        2> "$work/$label.stderr"; then
        fail "unsafe uncertain head pointer was admitted: $label"
    fi
    grep -Eiq 'head.*(unproven|provenance|alias|ambiguous)' \
        "$work/$label.stderr" \
        || fail "missing head provenance diagnostic: $label"
    test ! -s "$work/$label.minic.s" \
        || fail "partial ELF assembly escaped reject transaction: $label"
done

printf '%s\n' 'P48_X86_HEAD_LOCAL_PROVENANCE=PASS MiniC_displaced=9/9 GNU_O2_same_source_control=2/2 ordinary_text=1 PIE_ordinary_text=1 GNU_AS_LD=1 ELF_PC32_and_32S=1 head_uncertain_fail_closed=2/2 GCC_O0_local_displaced=NOT_GOLDEN x86_Linux_QEMU=NOT_RUN'
