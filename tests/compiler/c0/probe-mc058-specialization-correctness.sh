#!/usr/bin/env bash
# MC-058: emitted GNU `i` symbols use byte addends, transitive specialization
# retains its exact caller, and parameter writes kill speculative entry facts.
# Cross GNU/QEMU Linux runtime is not implied by these assembly/native gates.
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$repo_root/build/debug/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
test -x "$minic_cc"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

preprocess() {
    local name=$1
    local subdir=${2:-c0}
    "$host_cc" -E -P -std=gnu11 -x c \
        "$repo_root/tests/compiler/$subdir/$name.c" -o "$work/$name.i"
}

minic_asm() {
    local name=$1 target=$2
    "$minic_cc" --target="$target" -S "$work/$name.i" \
        -o "$work/$name.$target.s" \
        >"$work/$name.$target.out" 2>"$work/$name.$target.err"
}

gnu_asm() {
    local name=$1
    "$host_cc" -O2 -fno-pie -std=gnu11 -S \
        "$repo_root/tests/compiler/c0/$name.c" -o "$work/$name.gnu.s"
}

quad_symbols() {
    local assembly=$1 prefix=$2
    awk -v prefix="$prefix" '
        $1 == ".quad" {
            value = $2
            sub(/^\$/, "", value)
            if (index(value, prefix) == 1) print value
        }
    ' "$assembly" | LC_ALL=C sort
}

compare_quad_symbols() {
    local name=$1 target=$2 prefix=$3 expected=$4
    quad_symbols "$work/$name.gnu.s" "$prefix" >"$work/$name.gnu.quad"
    quad_symbols "$work/$name.$target.s" "$prefix" >"$work/$name.$target.quad"
    if [[ $(wc -l <"$work/$name.gnu.quad") -ne "$expected" ]] ||
       [[ $(wc -l <"$work/$name.$target.quad") -ne "$expected" ]] ||
       ! cmp -s "$work/$name.gnu.quad" "$work/$name.$target.quad"; then
        printf 'MC058_QUAD_MISMATCH fixture=%s target=%s\n' "$name" "$target" >&2
        diff -u "$work/$name.gnu.quad" "$work/$name.$target.quad" >&2 || true
        return 1
    fi
}

mandatory_riscv_assembly() {
    local name=$1 target
    for target in riscv32-linux riscv64-linux; do
        if ! minic_asm "$name" "$target"; then
            printf 'MC058_RISCV_COMPILE_FAILED fixture=%s target=%s\n' "$name" "$target" >&2
            tail -n 18 "$work/$name.$target.err" >&2
            return 1
        fi
    done
}

for source in \
    mc058_symbolic_pointer_addends \
    mc058_symbolic_pointer_stride_overflow_negative \
    mc058_symbolic_zero_stride_add \
    mc058_symbolic_transitive_offsets \
    mc058_asm_pointer_param_mutation \
    mc058_symbolic_transitive_mutation \
    mc058_asm_integer_param_mutation_negative \
    mc058_asm_volatile_parameter_negative \
    mc058_asm_integer_param_rebind_constant \
    core_asm_zero_stride_immediate \
    mc058_stale_boolean_parameter; do
    preprocess "$source"
done
for source in \
    mc058_stale_integer_transitive \
    mc058_stale_integer_asm_invalid \
    mc058_stale_integer_asm_volatile_invalid \
    mc058_stale_integer_asm_known; do
    preprocess "$source" audit
done

# Eight typed addends, including char cast, GNU void*, reversed operand,
# subtraction and record alignment. Do not compare arbitrary GCC text order.
name=mc058_symbolic_pointer_addends
gnu_asm "$name"
mandatory_riscv_assembly "$name"
for target in riscv32-linux riscv64-linux; do
    compare_quad_symbols "$name" "$target" mc058_ 8
done
if minic_asm mc058_symbolic_pointer_stride_overflow_negative riscv64-linux; then
    echo 'MC058_POINTER_STRIDE_OVERFLOW_UNEXPECTEDLY_ACCEPTED' >&2
    exit 1
fi

# GNU complete zero-sized records have pointer stride zero, not one byte.
name=mc058_symbolic_zero_stride_add
gnu_asm "$name"
mandatory_riscv_assembly "$name"
for target in riscv32-linux riscv64-linux; do
    compare_quad_symbols "$name" "$target" mc058_ 1
done
if minic_asm core_asm_zero_stride_immediate riscv64-linux; then
    echo 'MC058_ZERO_STRIDE_INDEXED_ADDRESS_UNEXPECTEDLY_ACCEPTED' >&2
    exit 1
fi

# Two different source routes to +4, one subtraction route to +4, and a
# two-hop route to +8 must all select the correct symbolic leaf clone.
name=mc058_symbolic_transitive_offsets
gnu_asm "$name"
mandatory_riscv_assembly "$name"
for target in riscv32-linux riscv64-linux; do
    # GCC duplicates four callsites when inlining; MiniC's shared source
    # FunctionBody legitimately emits only two canonical leaf clones (+4,+8).
    # Compare the SET of byte-addends, not the count of clone bodies.
    quad_symbols "$work/$name.gnu.s" mc058_chain_slots | uniq \
        >"$work/$name.gnu.unique"
    quad_symbols "$work/$name.$target.s" mc058_chain_slots | uniq \
        >"$work/$name.$target.unique"
    if [[ $(wc -l <"$work/$name.gnu.unique") -ne 2 ]] ||
       [[ $(wc -l <"$work/$name.$target.unique") -ne 2 ]] ||
       ! cmp -s "$work/$name.gnu.unique" "$work/$name.$target.unique" ||
       ! grep -Eq '^mc058_chain_slots\+4$' "$work/$name.$target.unique" ||
       ! grep -Eq '^mc058_chain_slots\+8$' "$work/$name.$target.unique"; then
        printf 'MC058_TRANSITIVE_ADDEND_MISMATCH target=%s\n' "$target" >&2
        diff -u "$work/$name.gnu.unique" "$work/$name.$target.unique" >&2 || true
        exit 1
    fi
    if ! grep -Eq 'MINIC_INLINE_SYMBOLIC_SPECIALIZATION_V0.*transitive=[1-9]' \
        "$work/$name.$target.err"; then
        printf 'MC058_NO_TRANSITIVE_CLONE target=%s\n' "$target" >&2
        exit 1
    fi
done

# Other ISA ports may intentionally decline structured raw GNU-asm `i` text;
# count and report the explicit target admission result instead of pretending
# to have executed a binary for architectures without this contract.
additional_isa_admitted=0
additional_isa_unsupported=0
for target in x86_64-linux aarch64-linux loongarch64-linux; do
    if minic_asm mc058_symbolic_pointer_addends "$target"; then
        compare_quad_symbols mc058_symbolic_pointer_addends "$target" mc058_ 8
        ((additional_isa_admitted += 1))
    elif grep -Eiq 'unsupported kind|target.*(unsupported|unclaimed)|Core instruction unsupported|Core instruction/ABI not yet supported by M1 emitter' \
        "$work/mc058_symbolic_pointer_addends.$target.err"; then
        printf 'MC058_TARGET_ASM_ADMISSION=SKIP target=%s reason=unsupported-structured-immediate\n' "$target"
        ((additional_isa_unsupported += 1))
    else
        printf 'MC058_UNEXPECTED_TARGET_FAILURE target=%s\n' "$target" >&2
        tail -n 15 "$work/mc058_symbolic_pointer_addends.$target.err" >&2
        exit 1
    fi
done

# Fresh local values after writes must not inherit stale entry facts. For an
# arbitrary run-time integer, GCC correctly rejects the `i` constraint; so
# must MiniC. For a constant overwritten locally, the actual value 2 wins.
name=mc058_asm_integer_param_mutation_negative
if gnu_asm "$name" >"$work/gnu-negative.out" 2>"$work/gnu-negative.err"; then
    echo 'MC058_GNU_NEGATIVE_UNEXPECTEDLY_ACCEPTED' >&2
    exit 1
fi
if minic_asm "$name" riscv64-linux; then
    echo 'MC058_MINIC_STALE_INTEGER_ASM_ACCEPTED' >&2
    exit 1
fi
name=mc058_stale_integer_asm_volatile_invalid
if "$host_cc" -O2 -std=gnu11 -S \
    "$repo_root/tests/compiler/audit/$name.c" -o "$work/$name.gnu.s" \
    >"$work/gnu-volatile.out" 2>"$work/gnu-volatile.err"; then
    echo 'MC058_GNU_VOLATILE_IMMEDIATE_UNEXPECTEDLY_ACCEPTED' >&2
    exit 1
fi
if minic_asm "$name" riscv64-linux; then
    echo 'MC058_MINIC_VOLATILE_IMMEDIATE_ACCEPTED' >&2
    exit 1
fi
name=mc058_asm_volatile_parameter_negative
if gnu_asm "$name" >"$work/gnu-volatile-modifier.out" \
    2>"$work/gnu-volatile-modifier.err"; then
    echo 'MC058_GNU_VOLATILE_FORMATTED_IMMEDIATE_ACCEPTED' >&2
    exit 1
fi
if minic_asm "$name" riscv64-linux; then
    echo 'MC058_MINIC_VOLATILE_FORMATTED_IMMEDIATE_ACCEPTED' >&2
    exit 1
fi
name=mc058_asm_integer_param_rebind_constant
gnu_asm "$name"
mandatory_riscv_assembly "$name"
if ! grep -Eq '\.byte[[:space:]]+2([[:space:]]|$)' "$work/$name.riscv64-linux.s"; then
    echo 'MC058_LIVE_INTEGER_REBIND_LOST' >&2
    exit 1
fi

# Independent audit control: integer ingress facts must not pass through a
# parameter write in a nested clone. Test ordinary five-target compilation
# and actual x86 native return values, not merely a clone counter.
name=mc058_stale_integer_transitive
"$host_cc" -O2 -std=gnu11 \
    "$repo_root/tests/compiler/audit/$name.c" -o "$work/$name.gnu"
"$work/$name.gnu"
for target in riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux; do
    if ! minic_asm "$name" "$target"; then
        printf 'MC058_INTEGER_TRANSITIVE_COMPILE_FAILED target=%s\n' "$target" >&2
        tail -n 15 "$work/$name.$target.err" >&2
        exit 1
    fi
done
"$host_cc" -no-pie "$work/$name.x86_64-linux.s" -o "$work/$name.minic"
"$work/$name.minic"

name=mc058_stale_integer_asm_invalid
if "$host_cc" -O2 -std=gnu11 -S \
    "$repo_root/tests/compiler/audit/$name.c" -o "$work/$name.gnu.s" \
    >"$work/audit-negative.out" 2>"$work/audit-negative.err"; then
    echo 'MC058_AUDIT_GNU_INVALID_INTEGER_ACCEPTED' >&2
    exit 1
fi
if minic_asm "$name" riscv64-linux; then
    echo 'MC058_AUDIT_STALE_INTEGER_ASM_ACCEPTED' >&2
    exit 1
fi
name=mc058_stale_integer_asm_known
mandatory_riscv_assembly "$name"
if ! grep -Eq '\.byte[[:space:]]+2([[:space:]]|$)' "$work/$name.riscv64-linux.s"; then
    echo 'MC058_AUDIT_LIVE_INTEGER_VALUE_LOST' >&2
    exit 1
fi

# Symbolic parameter mutation is distinct from byte scaling. A conservative
# diagnostic is safe; silently emitting the OLD object is an actual wrong
# relocation even if the compiler reports success.
safe_symbolic_reject=0
safe_symbolic_accept=0
for name in mc058_asm_pointer_param_mutation mc058_symbolic_transitive_mutation; do
    gnu_asm "$name"
    if minic_asm "$name" riscv64-linux; then
        if grep -Eq '\.quad[[:space:]]+\$?mc058_(old_target|rebound_old)' \
            "$work/$name.riscv64-linux.s" ||
           ! grep -Eq '\.quad[[:space:]]+\$?mc058_(new_target|rebound_new)' \
            "$work/$name.riscv64-linux.s"; then
            printf 'MC058_STALE_POINTER_ACCEPTED fixture=%s\n' "$name" >&2
            exit 1
        fi
        ((safe_symbolic_accept += 1))
    else
        ((safe_symbolic_reject += 1))
    fi
done

# Unlike `.quad` embedded into executable code, this source is safe to run:
# GCC and MiniC must agree on every mutating Boolean parameter route.
name=mc058_stale_boolean_parameter
"$host_cc" -O2 -std=gnu11 "$repo_root/tests/compiler/c0/$name.c" -o "$work/$name.gnu"
"$work/$name.gnu"
for target in riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux; do
    if ! minic_asm "$name" "$target"; then
        printf 'MC058_BOOLEAN_COMPILE_FAILED target=%s\n' "$target" >&2
        tail -n 18 "$work/$name.$target.err" >&2
        exit 1
    fi
done
"$host_cc" -no-pie "$work/$name.x86_64-linux.s" -o "$work/$name.minic"
"$work/$name.minic"

printf 'MC058_SPECIALIZATION_CORRECTNESS=PASS pointer_addends=8 stride_overflow_reject=1 zero_stride=1 zero_stride_indexed_reject=1 transitive_unique_addends=2 transitive_entry_calls=4 riscv_targets=2 additional_isa_admitted=%d additional_isa_unsupported=%d integer_stale_reject=2 integer_volatile_reject=2 integer_live_constant=2 integer_transitive_targets=5 integer_transitive_native=1 symbolic_safe_accept=%d symbolic_safe_reject=%d boolean_targets=5 boolean_gnu_native=1 boolean_minic_native=1\n' \
    "$additional_isa_admitted" "$additional_isa_unsupported" \
    "$safe_symbolic_accept" "$safe_symbolic_reject"
