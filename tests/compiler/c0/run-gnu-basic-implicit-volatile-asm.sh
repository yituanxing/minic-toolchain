#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-basic-implicit-volatile-asm

mkdir -p "$work"

for stem in gnu_basic_implicit_volatile_asm gnu_basic_implicit_volatile_align \
            gnu_asm_qualifier_metadata; do
    "$host_cc" -std=gnu11 -Wall -Wextra -Werror -fsyntax-only \
        "$root/tests/compiler/c0/$stem.c"
    "$host_cc" -std=gnu11 -E -P -x c \
        "$root/tests/compiler/c0/$stem.c" -o "$work/$stem.i"
done

# Basic asm with *no colon sections* is volatile by GNU C definition.  Four
# non-x86 backends already admit the exact 'nop' opaque machine instruction.
for target in riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
    MINIC_CORE_IR=strict "$minic_cc" --target="$target" -S \
        "$work/gnu_basic_implicit_volatile_asm.i" -o "$work/$target-basic.s"
    grep -Eq '^[[:space:]]*nop[[:space:]]*$' "$work/$target-basic.s"
done

# x86 validates the real Linux zstd basic-asm directive, admitted by the
# machine whitelist independently of this frontend qualifier rule.
MINIC_CORE_IR=strict "$minic_cc" --target=x86_64-linux -S \
    "$work/gnu_basic_implicit_volatile_align.i" -o "$work/x86_64-linux-basic.s"
grep -Eq '^[[:space:]]*\.p2align[[:space:]]+6[[:space:]]*$' \
    "$work/x86_64-linux-basic.s"

# Extended asm's explicit colon+operand form must preserve its *nonvolatile*
# qualifier.  The batch trace observes frontend metadata before the current
# Core contract intentionally rejects these unclaimed register-only shapes.
for target in x86_64-linux riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
    if MINIC_ASM_BATCH_TRACE=1 MINIC_CORE_IR=strict "$minic_cc" \
        --target="$target" -S "$work/gnu_asm_qualifier_metadata.i" \
        -o "$work/$target-extended.s" >"$work/$target-extended.out" \
        2>"$work/$target-extended.err"; then
        echo "FAIL extended-asm metadata unexpectedly reached emitter: $target" >&2
        exit 1
    fi
    grep -F 'ASM_BATCH_PROBE function=extended_plain volatile=0' \
        "$work/$target-extended.err" >/dev/null
    grep -F 'ASM_BATCH_PROBE function=extended_explicit_volatile volatile=1' \
        "$work/$target-extended.err" >/dev/null
    grep -F 'Core IR does not yet support function' \
        "$work/$target-extended.err" >/dev/null
done

printf '%s\n' 'PASS compiler/c0/gnu-basic-implicit-volatile-asm GNU=1 five-target-positive=1 x86-p2align6=1 nop-four-targets=1 extended-nonvolatile-preserved=5 extended-explicit-volatile=5 strict-core=1'
