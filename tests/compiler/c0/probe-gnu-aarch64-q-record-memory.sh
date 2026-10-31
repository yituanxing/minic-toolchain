#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/release"}/tests/gnu-aarch64-q-record-memory
mkdir -p "$work"

positive="$root/tests/compiler/c0/gnu_aarch64_q_record_memory.c"
invalid_rvalue="$root/tests/compiler/c0/invalid_gnu_aarch64_q_record_rvalue.c"
invalid_constraint="$root/tests/compiler/c0/invalid_gnu_aarch64_z_record_memory.c"

# The host GNU compiler is only a C syntax oracle: x86 Q is not AArch64 Q.
"$host_cc" -std=gnu11 -Wall -Wextra -Werror -fsyntax-only "$positive"
for item in positive invalid_rvalue invalid_constraint; do
    case "$item" in
        positive) source=$positive ;;
        invalid_rvalue) source=$invalid_rvalue ;;
        invalid_constraint) source=$invalid_constraint ;;
    esac
    "$host_cc" -std=gnu11 -E -P -x c "$source" -o "$work/$item.i"
done

# AArch64 Q admits a directly addressable aggregate memory input; this is
# asserted through the whole MiniC -S path, not merely its parser.
"$minic" --target=aarch64-linux -S "$work/positive.i" \
    -o "$work/aarch64-positive.s" 2>"$work/aarch64-positive.stderr"
test -s "$work/aarch64-positive.s"

# Other architectures cannot borrow AArch64's Q meaning. In particular x86
# Q is a register constraint, and an aggregate cannot be loaded as a GPR.
for isa in riscv32-linux riscv64-linux x86_64-linux loongarch64-linux; do
    if "$minic" --target="$isa" -S "$work/positive.i" \
        -o "$work/$isa-positive.s" 2>"$work/$isa-positive.stderr"; then
        printf 'FAIL: %s accepted AArch64 Q aggregate input\n' "$isa" >&2
        exit 1
    fi
    if ! grep -F 'GNU asm input operand currently requires an integer or pointer' \
        "$work/$isa-positive.stderr" >/dev/null; then
        printf 'FAIL: %s rejected aggregate Q after frontend validation\n' "$isa" >&2
        exit 1
    fi
done

# Neither a temporary record (not an addressable lvalue) nor an unrecognised
# Z constraint may obtain the aggregate-memory exception on any target.
for isa in riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux; do
    for item in invalid_rvalue invalid_constraint; do
        if "$minic" --target="$isa" -S "$work/$item.i" \
            -o "$work/$isa-$item.s" 2>"$work/$isa-$item.stderr"; then
            printf 'FAIL: %s accepted %s aggregate memory operand\n' "$isa" "$item" >&2
            exit 1
        fi
        if ! grep -F 'GNU asm input operand currently requires an integer or pointer' \
            "$work/$isa-$item.stderr" >/dev/null; then
            printf 'FAIL: %s %s failed outside expected frontend contract\n' "$isa" "$item" >&2
            exit 1
        fi
    done
done

printf '%s\n' 'PASS AArch64 Q aggregate memory -S; other four ISAs and 10 negative cases rejected at frontend'
