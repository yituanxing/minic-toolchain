#!/usr/bin/env bash
# Core adjacent-use forwarding: Common owns liveness, Loong owns $t0.
set -Eeuo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { echo "CORE_GP_FORWARD=FAIL $*" >&2; exit 1; }

test -x "$minic" || fail 'MiniC compiler missing'
"${HOST_CC:-cc}" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/core_gp_forward.c" -o "$work/source.i"
"$minic" --target=loongarch64-linux -S "$work/source.i" \
    -o "$work/loong.s" || fail 'LoongArch MiniC compile'
"$minic" --target=aarch64-linux -S "$work/source.i" \
    -o "$work/aarch64.s" || fail 'AArch64 untouched target compile'
awk '/^minic_gp_anti_chain:$/ {inside=1} inside {print}
     /^\.size minic_gp_anti_chain,/ {exit}' "$work/loong.s" > "$work/anti.s"
awk '/^minic_gp_chain:$/ {inside=1} inside {print}
     /^\.size minic_gp_chain,/ {exit}' "$work/loong.s" > "$work/chain.s"

# Former LOAD→STORE GP SSA stack cycles here had six exact adjacent
# st.d/ld.d pairs. Core proves two pairs are single-use and can be elided.
roundtrips=$(awk '
    /^  st.d \$t0, \$sp, 16$/ {prior=1; next}
    prior {if ($0 == "  ld.d $t0, $sp, 16") n++; prior=0}
    END {print n+0}' "$work/anti.s")
test "$roundtrips" -le 4 || fail "adjacent SSA cycles remain: $roundtrips"
# Folded OBJECT_ADDRESS instructions leave no machine opcode and must not
# prematurely interrupt a genuine GP live interval. Without cross-address
# forwarding these two functions use 64 and 73 lines respectively.
test "$(wc -l < "$work/chain.s")" -le 56 \
    || fail 'transparent address split the scalar chain'
test "$(wc -l < "$work/anti.s")" -le 61 \
    || fail 'transparent address split the anti-chain'
grep -Fq '  mul.d $t0, $t0, $t1' "$work/anti.s" \
    || fail 'multiply effect missing'
grep -Fq '  xor $t0, $t0, $t1' "$work/anti.s" \
    || fail 'XOR effect missing'
grep -Fq '  ld.d $t0, $sp,' "$work/anti.s" \
    || fail 'required stack loads missing'
if command -v loongarch64-linux-gnu-as >/dev/null 2>&1; then
    loongarch64-linux-gnu-as "$work/loong.s" -o "$work/loong.o" \
        || fail 'Loong GNU AS rejected forwarding'
fi
if command -v aarch64-linux-gnu-as >/dev/null 2>&1; then
    aarch64-linux-gnu-as "$work/aarch64.s" -o "$work/aarch64.o" \
        || fail 'AArch GNU AS rejected unchanged dispatch'
fi
"${HOST_CC:-cc}" -std=gnu11 -O2 "$root/tests/compiler/c0/core_gp_forward.c" \
    -o "$work/gnu.exe" || fail 'GNU reference build'
"$minic" --target=x86_64-linux -S "$work/source.i" -o "$work/x86.s" \
    || fail 'MiniC native target build'
MINIC_X86_DISABLE_ADJACENT_GP_FORWARD=1 \
    "$minic" --target=x86_64-linux -S "$work/source.i" \
    -o "$work/x86-baseline.s" || fail 'x86 conservative baseline build'
MINIC_X86_DISABLE_DIRECT_OBJECT_ADDRESS=1 \
    "$minic" --target=x86_64-linux -S "$work/source.i" \
    -o "$work/x86-address-baseline.s" || fail 'x86 direct-address baseline build'
"${HOST_CC:-cc}" -c -x assembler "$work/x86.s" -o "$work/x86.o" \
    || fail 'GNU assembled forwarded x86'
"${HOST_CC:-cc}" -c -x assembler "$work/x86-baseline.s" -o "$work/x86-baseline.o" \
    || fail 'GNU assembled conservative x86'
"${HOST_CC:-cc}" -c -x assembler "$work/x86-address-baseline.s" \
    -o "$work/x86-address-baseline.o" || fail 'GNU assembled non-direct-address x86'
x86_bytes=$(size -A "$work/x86.o" | awk '$1 == ".text" {print $2}')
baseline_bytes=$(size -A "$work/x86-baseline.o" | awk '$1 == ".text" {print $2}')
address_baseline_bytes=$(size -A "$work/x86-address-baseline.o" |
    awk '$1 == ".text" {print $2}')
x86_instructions=$(objdump -d --no-show-raw-insn "$work/x86.o" |
    awk '/^[[:space:]]*[0-9a-f]+:/ {n++} END {print n+0}')
baseline_instructions=$(objdump -d --no-show-raw-insn "$work/x86-baseline.o" |
    awk '/^[[:space:]]*[0-9a-f]+:/ {n++} END {print n+0}')
address_baseline_instructions=$(objdump -d --no-show-raw-insn "$work/x86-address-baseline.o" |
    awk '/^[[:space:]]*[0-9a-f]+:/ {n++} END {print n+0}')
test -n "$baseline_bytes" && test -n "$x86_bytes" &&
    test "$x86_bytes" -lt "$baseline_bytes" &&
    test "$x86_instructions" -lt "$baseline_instructions" \
    || fail "x86 adjacent GP machine delta absent: text $baseline_bytes->$x86_bytes instructions $baseline_instructions->$x86_instructions"
test -n "$address_baseline_bytes" &&
    test "$x86_bytes" -lt "$address_baseline_bytes" &&
    test "$x86_instructions" -lt "$address_baseline_instructions" \
    || fail "x86 direct object address delta absent: text $address_baseline_bytes->$x86_bytes instructions $address_baseline_instructions->$x86_instructions"
# Directly inspect the target-owned aligned-object address mode. RSP-relative
# local objects cannot silently be rebased to negative RBP displacements.
awk '/^minic_gp_aligned_local:$/ {inside=1} inside {print}
     /^\.size minic_gp_aligned_local,/ {exit}' "$work/x86.s" > "$work/aligned.s"
grep -Fq '  andq $-64, %rsp' "$work/aligned.s" &&
    grep -Eq '^  movq %rax, [0-9]+\(%rsp\)$' "$work/aligned.s" &&
    grep -Eq '^  movq [0-9]+\(%rsp\), %rax$' "$work/aligned.s" \
    || fail '64-byte aligned local lost folded RSP-based object memory access'
# The packed test is deliberately unaligned. Verify actual 8-bit and 32-bit
# memory instructions remain present in addition to the GNU runtime oracle.
awk '/^minic_gp_packed_local:$/ {inside=1} inside {print}
     /^\.size minic_gp_packed_local,/ {exit}' "$work/x86.s" > "$work/packed.s"
grep -Eq '^  movb %al, -[0-9]+\(%rbp\)$' "$work/packed.s" &&
    grep -Eq '^  movl -[0-9]+\(%rbp\), %eax$' "$work/packed.s" \
    || fail 'packed scalar memory widths were collapsed by address forwarding'
"${HOST_CC:-cc}" -no-pie "$work/x86.s" -o "$work/minic.exe" \
    || fail 'GNU assembler/linker of MiniC native output'
"${HOST_CC:-cc}" -no-pie "$work/x86-baseline.s" -o "$work/x86-baseline.exe" \
    || fail 'GNU assembler/linker of MiniC conservative x86 baseline'
"${HOST_CC:-cc}" -no-pie "$work/x86-address-baseline.s" \
    -o "$work/x86-address-baseline.exe" \
    || fail 'GNU assembler/linker of MiniC non-direct-address x86 baseline'
gnu_status=0
"$work/gnu.exe" || gnu_status=$?
minic_status=0
"$work/minic.exe" || minic_status=$?
baseline_status=0
"$work/x86-baseline.exe" || baseline_status=$?
address_baseline_status=0
"$work/x86-address-baseline.exe" || address_baseline_status=$?
test "$gnu_status" -eq 42 && test "$minic_status" -eq "$gnu_status" &&
    test "$baseline_status" -eq "$gnu_status" &&
    test "$address_baseline_status" -eq "$gnu_status" \
    || fail "GNU/MiniC native runtime mismatch gnu=$gnu_status minic=$minic_status gp_baseline=$baseline_status address_baseline=$address_baseline_status"
"${HOST_CC:-cc}" -E -P -std=gnu11 -x c -DMINIC_GP_REJECT_ALIGN_ALLOCA \
    "$root/tests/compiler/c0/core_gp_forward.c" -o "$work/forbidden.i"
if "$minic" --target=x86_64-linux -S "$work/forbidden.i" \
        -o "$work/forbidden.s" 2> "$work/forbidden.err"; then
    fail 'x86 admitted aligned RSP locals together with moving-stack alloca'
fi
grep -Fq 'x86-64 Core instruction unsupported: function minic_gp_reject_aligned_alloca' \
    "$work/forbidden.err" \
    || fail 'x86 aligned-allocation rejection lacked the exact Core owner'
echo "CORE_GP_FORWARD=PASS loong_cycles=$roundtrips native_strict=42 aarch64_s=1 x86_gp_text=$baseline_bytes->$x86_bytes x86_gp_instructions=$baseline_instructions->$x86_instructions x86_object_text=$address_baseline_bytes->$x86_bytes x86_object_instructions=$address_baseline_instructions->$x86_instructions x86_native_differential=42"
