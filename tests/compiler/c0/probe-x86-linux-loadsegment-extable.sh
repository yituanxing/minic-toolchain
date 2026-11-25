#!/usr/bin/env bash
# Linux loadsegment() is a finite five-form privileged target family. Preserve
# the original three-word ELF exception-table record and its two PC32 fixups.
# These selector writes are never executed in userspace.
set -Eeuo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
fixture="$root/tests/target/x86_64_linux_segment_load_extable.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { printf '%s\n' "X86_LOADSEG_EXTABLE=FAIL $*" >&2; exit 1; }

"$host_cc" -std=gnu11 -c "$fixture" -o "$work/gnu.o" \
    || fail 'GNU GCC rejected original selector source'
"$host_cc" -std=gnu11 -E -P -x c "$fixture" -o "$work/positive.i"
"$minic" --target=x86_64-linux -S "$work/positive.i" -o "$work/positive.s" \
    >"$work/stdout" 2>"$work/stderr" \
    || { sed -n '1,15p' "$work/stderr" >&2; fail 'qualified source rejected'; }
as --64 "$work/positive.s" -o "$work/positive.o" \
    || fail 'GNU assembler rejected selector fixups'
objdump -d "$work/positive.o" >"$work/objdump"
for selector in fs gs ds es ss; do
    # objdump disassembles the segment-register MOV operand as `%eax` even
    # though GNU source and the physical machine binding are 16-bit `%ax`.
    grep -Eq "mov[[:space:]]+%eax,%${selector}" "$work/objdump" \
        || fail "missing 16-bit ${selector} hardware write"
done
readelf -Wr "$work/positive.o" >"$work/relocs"
grep -Eq "Relocation section '.rela__ex_table'.*10 entries" "$work/relocs" \
    || fail 'expected 5 records with 2 relocations each'
test "$(rg -c 'R_X86_64_PC32' "$work/relocs")" -eq 10 \
    || fail 'wrong exception table PC32 relocation count'
readelf -x __ex_table "$work/positive.o" >"$work/extable"
test "$(rg -o '05000000' "$work/extable" | wc -l)" -eq 5 \
    || fail 'missing five type-5 exception records'

reject() {
    local label=$1
    local target=${2:-x86_64-linux}
    "$host_cc" -std=gnu11 -E -P -x c "$work/$label.c" -o "$work/$label.i"
    if "$minic" --target="$target" -S "$work/$label.i" \
            -o "$work/$label.s" >"$work/$label.stdout" 2>"$work/$label.stderr"; then
        fail "$label invalid selector form was admitted"
    fi
    test ! -e "$work/$label.s" || fail "$label left partially emitted ASM"
}
sed 's/long 5/long 17/' "$fixture" >"$work/wrong_fixup.c"
reject wrong_fixup
sed 's/"rm"(selector)/"r"(selector)/' "$fixture" >"$work/wrong_constraint.c"
reject wrong_constraint
sed 's/"memory");/"cc");/' "$fixture" >"$work/wrong_clobber.c"
reject wrong_clobber
cp "$fixture" "$work/wrong_target.c"
reject wrong_target aarch64-linux
printf '%s\n' 'X86_LOADSEG_EXTABLE=PASS variants=5 GNU_as=1 R_X86_64_PC32=10 type5=5 rejects=4'
