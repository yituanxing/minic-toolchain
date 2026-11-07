#!/usr/bin/env bash
# P41: retain bounded-OR value facts without resurrecting stale inline-entry
# constants after unknown CFG joins. This is a correctness probe, not a claim
# of five-ISA Linux image execution.
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$repo_root/build/debug/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
test -x "$minic_cc"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

preprocess() {
    local name=$1
    "$host_cc" -E -P -std=gnu11 \
        "$repo_root/tests/compiler/c0/$name.c" -o "$work/$name.i"
}

minic_asm() {
    local name=$1 target=$2
    "$minic_cc" --target="$target" -S "$work/$name.i" \
        -o "$work/$name.$target.s" \
        >"$work/$name.$target.out" 2>"$work/$name.$target.err"
}

negative=p41_bounded_or_conditional_negative
positive=p41_bounded_or_live_immediate
runtime=p41_bounded_or_controlflow_runtime
asm_output=p41_asm_output_stale_known_negative
for name in "$negative" "$positive" "$runtime" "$asm_output"; do
    preprocess "$name"
done

# Unmodified P40 accepted a conditional `x |= 2` as an immutable entry-zero
# parameter: GCC correctly rejected the dynamic `i`, MiniC wrongly emitted
# `.byte 0`. Require the *same failure reason* on every Core target: dynamic
# inline `i` not resolved, not an unrelated machine emitter rejection.
if "$host_cc" -O2 -std=gnu11 -S \
    "$repo_root/tests/compiler/c0/$negative.c" \
    -o "$work/$negative.gnu.s" \
    >"$work/$negative.gnu.out" 2>"$work/$negative.gnu.err"; then
    echo 'P41_GCC_DYNAMIC_IMMEDIATE_UNEXPECTEDLY_ACCEPTED' >&2
    exit 1
fi
if ! grep -Eiq 'impossible constraint|constraint.*(impossible|inconsistent)' \
    "$work/$negative.gnu.err"; then
    echo 'P41_GCC_FAILED_FOR_UNEXPECTED_REASON' >&2
    tail -n 18 "$work/$negative.gnu.err" >&2
    exit 1
fi
for target in riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux; do
    if minic_asm "$negative" "$target"; then
        printf 'P41_DYNAMIC_IMMEDIATE_WRONGCODE target=%s\n' "$target" >&2
        exit 1
    fi
    if ! grep -Eq 'constraint=i.*immediate_resolved=0' \
        "$work/$negative.$target.err"; then
        printf 'P41_DYNAMIC_IMMEDIATE_WRONG_REJECTION target=%s\n' "$target" >&2
        tail -n 18 "$work/$negative.$target.err" >&2
        exit 1
    fi
done

# Independent write barrier: a GNU register output invalidates a scalar's
# previous constant. Preserve this existing rejection while fixing OR's
# bounded-bit exceptional write path.
if "$host_cc" -O2 -std=gnu11 -S \
    "$repo_root/tests/compiler/c0/$asm_output.c" \
    -o "$work/$asm_output.gnu.s" \
    >"$work/$asm_output.gnu.out" 2>"$work/$asm_output.gnu.err"; then
    echo 'P41_GCC_ASM_OUTPUT_STALE_IMMEDIATE_ACCEPTED' >&2
    exit 1
fi
for target in riscv32-linux riscv64-linux; do
    if minic_asm "$asm_output" "$target" ||
       ! grep -Eq 'constraint=i.*immediate_resolved=0' \
           "$work/$asm_output.$target.err"; then
        printf 'P41_ASM_OUTPUT_STALE_IMMEDIATE_ACCEPTED target=%s\n' "$target" >&2
        exit 1
    fi
done

# A straight-line `x |= 2` still has a valid *new* immediate 2. Preserve the
# bounded-bit optimization and compare GNU's and MiniC's actual output, not
# just compilation success. Structured inline-asm admission differs by ISA.
"$host_cc" -O2 -std=gnu11 -S \
    "$repo_root/tests/compiler/c0/$positive.c" \
    -o "$work/$positive.gnu.s"
if [[ $(grep -Ec '^[[:space:]]*\.byte[[:space:]]+2[[:space:]]*$' \
          "$work/$positive.gnu.s") -ne 2 ]]; then
    echo 'P41_GCC_LIVE_IMMEDIATE_BASELINE_CHANGED' >&2
    exit 1
fi
for target in riscv32-linux riscv64-linux; do
    if ! minic_asm "$positive" "$target"; then
        printf 'P41_LIVE_IMMEDIATE_REJECTED target=%s\n' "$target" >&2
        tail -n 18 "$work/$positive.$target.err" >&2
        exit 1
    fi
    if [[ $(grep -Ec '^[[:space:]]*\.byte[[:space:]]+2[[:space:]]*$' \
              "$work/$positive.$target.s") -ne 2 ]]; then
        printf 'P41_LIVE_IMMEDIATE_MISMATCH target=%s\n' "$target" >&2
        exit 1
    fi
done
for target in x86_64-linux aarch64-linux loongarch64-linux; do
    if minic_asm "$positive" "$target"; then
        if [[ $(grep -Ec '^[[:space:]]*\.byte[[:space:]]+2[[:space:]]*$' \
                  "$work/$positive.$target.s") -ne 2 ]]; then
            printf 'P41_LIVE_IMMEDIATE_MISMATCH target=%s\n' "$target" >&2
            exit 1
        fi
    elif grep -Eiq 'Core instruction unsupported|Core instruction/ABI not yet supported|unsupported kind' \
        "$work/$positive.$target.err"; then
        printf 'P41_STRUCTURED_ASM_ADMISSION=SKIP target=%s\n' "$target"
    else
        printf 'P41_LIVE_IMMEDIATE_UNEXPECTED_FAILURE target=%s\n' "$target" >&2
        tail -n 18 "$work/$positive.$target.err" >&2
        exit 1
    fi
done

# The non-ASM control flow itself must remain executable across all five
# compiler targets. Native x86 tests both zero and multiple OR loop backedges.
"$host_cc" -O2 -std=gnu11 \
    "$repo_root/tests/compiler/c0/$runtime.c" -o "$work/$runtime.gnu"
for target in riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux; do
    if ! minic_asm "$runtime" "$target"; then
        printf 'P41_RUNTIME_COMPILE_FAILED target=%s\n' "$target" >&2
        tail -n 18 "$work/$runtime.$target.err" >&2
        exit 1
    fi
done
"$host_cc" -no-pie "$work/$runtime.x86_64-linux.s" -o "$work/$runtime.minic"
for arg_count in 0 1 3; do
    args=()
    for ((i=0; i<arg_count; ++i)); do args+=(x); done
    "$work/$runtime.gnu" "${args[@]}"
    "$work/$runtime.minic" "${args[@]}"
done

echo 'P41_BOUNDED_OR_SPECIALIZATION=PASS'
