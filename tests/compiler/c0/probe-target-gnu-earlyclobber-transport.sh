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
    MINIC_GNU_ASM_CONSTRAINT_TRACE=1 "$minic" --target="$target-linux" \
        -S "$tmp/$target.i" -o "$tmp/$target.s" 2>"$tmp/$target.log"
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
    echo "TARGET_GNU_EARLY_SOURCE=$target preserved=$actual"
done

# RISC-V has no target-specific qualifier: RV64's existing generic `+&r`
# allocator remains a distinct path. RV32 must still successfully compile
# generic C and reject unsupported specialized early-clobber allocations.
cc -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/gnu_inline_asm_readwrite_earlyclobber.c" \
    -o "$tmp/riscv.i"
"$minic" --target=riscv64-linux -S "$tmp/riscv.i" -o "$tmp/riscv64.s"
grep -Eq 'add a0, a0,' "$tmp/riscv64.s"
rv32_early='fail_closed'
if "$minic" --target=riscv32-linux -S "$tmp/riscv.i" \
        -o "$tmp/rv32-early.s" >/dev/null 2>&1; then
    # A future RV32 backend may legitimately implement this form; in that
    # case it must produce an actual target assembly artifact.
    test -s "$tmp/rv32-early.s"
    grep -Eq 'add a0, a0,' "$tmp/rv32-early.s"
    rv32_early='accepted'
else
    test ! -e "$tmp/rv32-early.s"
fi
printf 'int main(void) { return 42; }\n' > "$tmp/riscv32.c"
"$minic" --target=riscv32-linux -S "$tmp/riscv32.c" -o "$tmp/riscv32.s"
test -s "$tmp/riscv32.s"

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
