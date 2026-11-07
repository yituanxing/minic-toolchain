#!/usr/bin/env bash
set -Eeuo pipefail

minic="${1:?pass a built minic-cc}"
test -x "$minic"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# Compile the original GNU forms. The shared target contract carries the
# physical-register `&` independent of Core's address/value role; the trace
# proves that fact survived source qualification and target Core lowering.
# Existing machine emitters intentionally keep their legacy Core `early`
# allocation behavior until the target operand plan is migrated separately.
cases=(
    'x86_64|x86_futex_extable_atomic.c|3'
    'aarch64|aarch64_linux_hyp_spinlock_lse.c|4'
    'loongarch64|loong-linux-small-xchg.c|2'
)
for entry in "${cases[@]}"; do
    IFS='|' read -r target source minimum <<< "$entry"
    cc -E -P -std=gnu11 -x c "$root/tests/compiler/c0/$source" \
        -o "$tmp/$target.i"
    echo "TARGET_GNU_EARLY_STAGE=compile_begin target=$target"
    if ! MINIC_GNU_ASM_CONSTRAINT_TRACE=1 MINIC_ASM_OPERAND_PLAN_TRACE=1 \
            "$minic" --target="$target-linux" \
            -S "$tmp/$target.i" -o "$tmp/$target.s" 2>"$tmp/$target.log"; then
        echo "TARGET_GNU_EARLY_STAGE=compile_failed target=$target" >&2
        tail -n 55 "$tmp/$target.log" >&2 || true
        exit 1
    fi
    echo "TARGET_GNU_EARLY_STAGE=compile_success target=$target"
    test -s "$tmp/$target.s"
    actual="$(grep -c 'CORE_GNU_ASM_CONSTRAINT .* gnu_early=1 core_early=0' \
              "$tmp/$target.log" || true)"
    if (( actual < minimum )); then
        echo "FAIL $target expected >=$minimum qualified GNU early outputs, got $actual" >&2
        sed -n '1,60p' "$tmp/$target.log" >&2
        exit 1
    fi
    if grep -E 'CORE_GNU_ASM_CONSTRAINT .* gnu_early=1 core_early=1' \
            "$tmp/$target.log" >/dev/null; then
        echo "FAIL $target conflated source and Core allocator early flags" >&2
        exit 1
    fi
    if ! grep -E 'CORE_ASM_OPERAND_PLAN owner=1 source_form=[0-9a-f]{16} .* gnu_early=[1-9]' \
            "$tmp/$target.log" >/dev/null; then
        echo "FAIL $target typed source form did not reach Core" >&2
        exit 1
    fi
    echo "TARGET_GNU_EARLY_SOURCE=$target preserved=$actual"
done

# RISC-V has no target-specific qualifier: RV64's existing generic `+&r`
# allocator remains a distinct path. RV32 must still successfully compile
# generic C and reject unsupported specialized early-clobber allocations.
echo "TARGET_GNU_EARLY_STAGE=rv64_generic_preprocess_begin"
cc -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/gnu_inline_asm_readwrite_earlyclobber.c" \
    -o "$tmp/riscv.i"
echo "TARGET_GNU_EARLY_STAGE=rv64_generic_compile_begin"
if ! MINIC_ASM_OPERAND_PLAN_TRACE=1 "$minic" --target=riscv64-linux \
        -S "$tmp/riscv.i" -o "$tmp/riscv64.s" 2>"$tmp/riscv64.log"; then
    echo "TARGET_GNU_EARLY_STAGE=rv64_generic_compile_failed" >&2
    tail -n 65 "$tmp/riscv64.log" >&2 || true
    exit 1
fi
echo "TARGET_GNU_EARLY_STAGE=rv64_generic_compile_success"
if ! grep -Eq 'add (a0|x10), (a0|x10),' "$tmp/riscv64.s"; then
    echo "TARGET_GNU_EARLY_STAGE=rv64_generic_assembly_mismatch" >&2
    echo "TARGET_GNU_EARLY_DIAG=rv64_add_registers" >&2
    grep -nE '(^|[[:space:]])add[[:space:]]' "$tmp/riscv64.s" >&2 || true
    echo "TARGET_GNU_EARLY_DIAG=rv64_assembly_tail" >&2
    tail -n 95 "$tmp/riscv64.s" >&2
    exit 1
fi
if ! grep -E 'CORE_ASM_OPERAND_PLAN owner=2 source_form=[0-9a-f]{16} .* gnu_early=3 ' \
        "$tmp/riscv64.log" >/dev/null; then
    echo "TARGET_GNU_EARLY_STAGE=rv64_generic_owner_mismatch" >&2
    tail -n 65 "$tmp/riscv64.log" >&2
    exit 1
fi
echo "TARGET_GNU_EARLY_STAGE=rv64_generic_source_pass"
rv32_early='fail_closed'
echo "TARGET_GNU_EARLY_STAGE=rv32_generic_probe_begin"
if "$minic" --target=riscv32-linux -S "$tmp/riscv.i" \
        -o "$tmp/rv32-early.s" >"$tmp/rv32-early.stdout" 2>"$tmp/rv32-early.log"; then
    # A future RV32 backend may legitimately implement this form; in that
    # case it must produce an actual target assembly artifact.
    if ! test -s "$tmp/rv32-early.s"; then
        echo "TARGET_GNU_EARLY_STAGE=rv32_generic_accepted_empty_assembly" >&2
        exit 1
    fi
    if ! grep -Eq 'add (a0|x10), (a0|x10),' "$tmp/rv32-early.s"; then
        echo "TARGET_GNU_EARLY_STAGE=rv32_generic_accepted_wrong_assembly" >&2
        sed -n '1,100p' "$tmp/rv32-early.s" >&2
        exit 1
    fi
    rv32_early='accepted'
else
    if test -e "$tmp/rv32-early.s"; then
        echo "TARGET_GNU_EARLY_STAGE=rv32_generic_rejected_but_emitted" >&2
        tail -n 45 "$tmp/rv32-early.log" >&2
        exit 1
    fi
fi
echo "TARGET_GNU_EARLY_STAGE=rv32_generic_probe_success mode=$rv32_early"
printf 'int main(void) { return 42; }\n' > "$tmp/riscv32.c"
if ! "$minic" --target=riscv32-linux -S "$tmp/riscv32.c" \
        -o "$tmp/riscv32.s" 2>"$tmp/riscv32.log"; then
    echo "TARGET_GNU_EARLY_STAGE=rv32_plain_compile_failed" >&2
    tail -n 45 "$tmp/riscv32.log" >&2
    exit 1
fi
if ! test -s "$tmp/riscv32.s"; then
    echo "TARGET_GNU_EARLY_STAGE=rv32_plain_empty_assembly" >&2
    exit 1
fi
echo "TARGET_GNU_EARLY_STAGE=rv32_generic_pass"

# Native GNU/MiniC differential and the exception-table/atomic correctness
# gate use the real x86 Linux futex fixture (not only a synthetic asm).
if [[ "${MINIC_SOURCE_ONLY:-0}" != 1 ]]; then
    MINIC_CC="$minic" sh "$root/tests/compiler/c0/probe-x86-futex-extable-atomic.sh"
else
    cc -std=gnu11 -S -x c \
        "$root/tests/compiler/c0/x86_futex_extable_atomic.c" \
        -o "$tmp/gnu-x86.s"
    test -s "$tmp/gnu-x86.s"
fi
echo "TARGET_GNU_EARLY_CONSTRAINT=PASS targets=5 qualified=3 generic_rv64=1 rv32=$rv32_early"
