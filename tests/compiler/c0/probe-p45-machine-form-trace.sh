#!/usr/bin/env bash
set -Eeuo pipefail

minic="${1:?pass a built minic-cc}"
test -x "$minic"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
host_cc="${HOST_CC:-cc}"

# These source-faithful GNU Linux fixtures exercise the actual full chain:
# C/GNU qualification -> Core operand-plan transport -> ISA machine emission.
# Formerly these qualified effects had a source hash but machine_form=0;
# P45 must publish machine ownership for the COMPLETE family, not one opcode.
cases=(
    'x86_64|x86_futex_extable_atomic.c|6'
    'aarch64|aarch64_linux_hyp_spinlock_lse.c|2'
)
for entry in "${cases[@]}"; do
    IFS='|' read -r arch source minimum <<< "$entry"
    "$host_cc" -E -P -std=gnu11 -x c \
        "$root/tests/compiler/c0/$source" -o "$work/$arch.i"
    MINIC_ASM_OPERAND_PLAN_TRACE=1 "$minic" --target="$arch-linux" \
        -S "$work/$arch.i" -o "$work/$arch.s" \
        >"$work/$arch.stdout" 2>"$work/$arch.stderr"
    test -s "$work/$arch.s"
    if [[ "$arch" == x86_64 ]]; then
        grep -Fq '__ex_table' "$work/$arch.s"
        grep -Eq 'cmpxchg|xadd' "$work/$arch.s"
    else
        grep -Eq 'ldaxr|ldadda' "$work/$arch.s"
        grep -Fq '.altinstructions' "$work/$arch.s"
    fi
    # Every trace on this exact fixture is a qualified Linux form. The new
    # target-owned machine identity must be present on all of them.
    if awk '/^CORE_ASM_OPERAND_PLAN owner=1 / && /machine_form=0( |$)/ {bad=1}
            END {exit bad ? 0 : 1}' "$work/$arch.stderr"; then
        echo "FAIL $arch source-qualified form retained machine_form=0" >&2
        grep '^CORE_ASM_OPERAND_PLAN ' "$work/$arch.stderr" >&2 || true
        exit 1
    fi
    actual="$(grep -Ec '^CORE_ASM_OPERAND_PLAN owner=1 .*machine_form=[1-9][0-9]* machine_variant=[0-9]+' \
        "$work/$arch.stderr" || true)"
    if (( actual < minimum )); then
        echo "FAIL $arch expected >=$minimum sealed forms, got $actual" >&2
        grep '^CORE_ASM_OPERAND_PLAN ' "$work/$arch.stderr" >&2 || true
        exit 1
    fi
    echo "P45_TYPED_MACHINE=$arch qualified=$actual"
done

# The 31-GPR kexec crash snapshot is a legitimate AArch64 generic Core
# producer with ID0, not one of the SourceBackend's machine-form catalog.
# It is crucial that sealing all target-only forms does not reject it. The
# existing regression also checks large-frame scratch safety and negative
# C/GNU contracts against the original Linux template.
MINIC_CC="$minic" HOST_CC="$host_cc" \
    sh "$root/tests/compiler/c0/probe-aarch64-linux-kexec-crash-regs.sh"

# The generic Core structured path is intentionally distinct from target
# qualification. Neither an ISA-owned ID nor its variant may be invented.
"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/gnu_inline_asm_readwrite_earlyclobber.c" \
    -o "$work/riscv.i"
MINIC_ASM_OPERAND_PLAN_TRACE=1 "$minic" --target=riscv64-linux \
    -S "$work/riscv.i" -o "$work/riscv64.s" \
    2>"$work/riscv64.stderr"
test -s "$work/riscv64.s"
grep -Eq 'add a0, a0,' "$work/riscv64.s"
grep -Eq '^CORE_ASM_OPERAND_PLAN owner=2 .*machine_form=0 machine_variant=0$' \
    "$work/riscv64.stderr"
if grep -Eq '^CORE_ASM_OPERAND_PLAN owner=2 .*machine_form=[1-9]' \
        "$work/riscv64.stderr"; then
    echo 'FAIL RV64 generic structured ASM acquired a machine ID' >&2
    exit 1
fi

# Keep the other machine target baselines live while the two Linux-dense
# backends change their form-selection implementation.
"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/loong-linux-small-xchg.c" \
    -o "$work/loong.i"
"$minic" --target=loongarch64-linux -S "$work/loong.i" -o "$work/loong.s"
test -s "$work/loong.s"
printf 'int main(void) { return 42; }\n' >"$work/rv32.c"
"$minic" --target=riscv32-linux -S "$work/rv32.c" -o "$work/rv32.s"
test -s "$work/rv32.s"

# GNU native assembly, linker, runtime and original Linux exception-table
# differential on the x86 Linux futex proof (including its negative suffix).
MINIC_CC="$minic" HOST_CC="$host_cc" \
    sh "$root/tests/compiler/c0/probe-x86-futex-extable-atomic.sh"
echo 'P45_MACHINE_FORM_TRACE=PASS x86_futex=6 a64_hyp_spin=2 rv64_generic_id=0 a64_kexec_generic_id0=PASS controls=rv32,loongarch64 x86_gnu_native=PASS'
