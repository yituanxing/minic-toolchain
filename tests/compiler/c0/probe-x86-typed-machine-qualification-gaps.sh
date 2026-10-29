#!/usr/bin/env bash
set -Eeuo pipefail
minic="${1:?pass built minic-cc}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
source_file="$root/tests/compiler/c0/x86_typed_machine_qualification_gaps.c"
temp="$(mktemp -d)"
trap 'rm -rf "$temp"' EXIT
host_cc="${CC:-cc}"

# The four forms previously arrived as Core generic owner=2. After adding
# complete target-owned GNU source qualifiers, every positive must be owner=1.
"$host_cc" -E -P "$source_file" -o "$temp/positive.i"
MINIC_ASM_OPERAND_PLAN_TRACE=1 "$minic" --target=x86_64-linux \
    -S "$temp/positive.i" -o "$temp/positive.s" \
    2>"$temp/positive.log"
test "$(grep -c '^CORE_ASM_OPERAND_PLAN owner=1 ' "$temp/positive.log")" -eq 4
! grep -q '^CORE_ASM_OPERAND_PLAN owner=2 ' "$temp/positive.log"
"$host_cc" -c -x assembler "$temp/positive.s" -o "$temp/positive.o"
"$host_cc" -c -x c "$temp/positive.i" -o "$temp/gnu.o"
objdump -dr "$temp/positive.o" >"$temp/positive.dump"
objdump -dr "$temp/gnu.o" >"$temp/gnu.dump"
for disassembly in positive gnu; do
    test "$(grep -c 'stmxcsr' "$temp/$disassembly.dump")" -eq 2
    test "$(grep -c 'ldmxcsr' "$temp/$disassembly.dump")" -eq 1
    grep -q 'lea.*%rip' "$temp/$disassembly.dump"
    grep -q 'mov.*%rsp' "$temp/$disassembly.dump"
    grep -q 'mov.*%rbp' "$temp/$disassembly.dump"
done

# A target qualifier must not widen old admission: its seven source failures
# still reach the generic preflight but ultimately fail closed at machine ASM.
for negative in \
    NEGATIVE_MXCSR_REGISTER NEGATIVE_MXCSR_WIDTH \
    NEGATIVE_MXCSR_CLOBBER NEGATIVE_MXCSR_SUFFIX \
    NEGATIVE_UNWIND_MEMORY NEGATIVE_UNWIND_CLOBBER \
    NEGATIVE_UNWIND_SUFFIX; do
    "$host_cc" -E -P -D"$negative" "$source_file" \
        -o "$temp/$negative.i"
    if MINIC_ASM_OPERAND_PLAN_TRACE=1 "$minic" --target=x86_64-linux \
            -S "$temp/$negative.i" -o "$temp/$negative.s" \
            2>"$temp/$negative.log"; then
        echo "FAIL: invalid x86 target-owned $negative was admitted" >&2
        exit 1
    fi
    grep -q '^CORE_ASM_OPERAND_PLAN owner=2 ' "$temp/$negative.log"
done

# Do not use Core's source_form_id hash as a machine opcode. Form-ID or
# variant forgeries against an unchanged literal must fail target revalidation.
"$host_cc" -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror \
    -ffunction-sections -fdata-sections -I"$root/include" -I"$root/src" \
    "$root/tests/target/x86_64_typed_machine_qualification_test.c" \
    "$root/src/target/x86_64/asm_contract.c" \
    -Wl,--gc-sections -o "$temp/form-gate"
"$temp/form-gate"
echo 'X86_MXCSR_UNWIND_TYPED=PASS positives=4 owner=1 negative=7 machine_gate=11 GNU_ELF=2'
