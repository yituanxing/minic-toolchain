#!/usr/bin/env bash
set -Eeuo pipefail
minic="${1:?pass built minic-cc}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
source_file="$root/tests/compiler/c0/x86_typed_scalar_families.c"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
host_cc="${CC:-cc}"

"$host_cc" -E -P "$source_file" -o "$work/positive.i"
MINIC_ASM_OPERAND_PLAN_TRACE=1 "$minic" --target=x86_64-linux \
    -S "$work/positive.i" -o "$work/positive.s" \
    2>"$work/positive.log"
test "$(grep -c '^CORE_ASM_OPERAND_PLAN owner=1 ' "$work/positive.log")" -eq 6
! grep -q '^CORE_ASM_OPERAND_PLAN owner=2 ' "$work/positive.log"
"$host_cc" -c -x assembler "$work/positive.s" -o "$work/positive.o"
"$host_cc" -c -x c "$work/positive.i" -o "$work/gnu.o"
objdump -dr "$work/positive.o" >"$work/positive.dump"
objdump -dr "$work/gnu.o" >"$work/gnu.dump"
for dump in positive gnu; do
    for opcode in 'out ' 'add ' cmpxchg 'bt ' 'incl '; do
        grep -Fq "$opcode" "$work/$dump.dump"
    done
done

# Unknown instruction suffixes remain unsupported, never routed to GAS.
for invalid in NEGATIVE_PORT_IO NEGATIVE_ATOMIC_RMW NEGATIVE_RAW_CMPXCHG \
               NEGATIVE_TRYCAS NEGATIVE_BITOPS NEGATIVE_ATOMIC_CC; do
    "$host_cc" -E -P -D"$invalid" "$source_file" -o "$work/$invalid.i"
    if "$minic" --target=x86_64-linux -S "$work/$invalid.i" \
        -o "$work/$invalid.s" >"$work/$invalid.log" 2>&1; then
        echo "FAIL: migrated target form accepted $invalid" >&2
        exit 1
    fi
    test ! -e "$work/$invalid.s"
done

# Verify full immutable text revalidation even when Core metadata is forged.
"$host_cc" -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror \
    -ffunction-sections -fdata-sections -I"$root/include" -I"$root/src" \
    "$root/tests/target/x86_64_typed_scalar_families_test.c" \
    "$root/src/target/x86_64/asm_contract.c" \
    -Wl,--gc-sections -o "$work/machine-gate"
"$work/machine-gate"
echo 'X86_TYPED_SCALAR_FAMILIES=PASS families=6 source_owner=1 negative=6 ELF=2'
