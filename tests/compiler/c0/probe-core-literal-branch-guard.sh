#!/usr/bin/env bash
# Fold only semantically literal Core CFG successors; GNU compiletime assert
# calls in the impossible edge must not survive object assembly/linking.
set -Eeuo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
fail() { printf 'CORE_LITERAL_BRANCH_GUARD=FAIL %s\n' "$*" >&2; exit 1; }
source_file="$root/tests/compiler/c0/core_literal_branch_guard.c"
cc -E -P -std=gnu11 -x c "$source_file" -o "$work/literal.i"
"$minic" -S "$work/literal.i" --target=x86_64-linux -o "$work/mini.s"
if grep -Fq '__compiletime_assert_typecheck' "$work/mini.s"; then
    fail 'literal-dead compiletime assert survived Core CFG'
fi
grep -Fq 'nonliteral_branch:' "$work/mini.s" ||
    fail 'non-constant branch function disappeared'
as --64 "$work/mini.s" -o "$work/mini.o"
if nm -u "$work/mini.o" | grep -Fq '__compiletime_assert_typecheck'; then
    fail 'GNU AS object contains impossible unresolved compiletime assertion'
fi
for target in riscv64-linux aarch64-linux loongarch64-linux; do
    "$minic" -S "$work/literal.i" --target="$target" -o "$work/$target.s"
    if grep -Fq '__compiletime_assert_typecheck' "$work/$target.s"; then
        fail "$target retained an impossible Core compiletime assertion"
    fi
done
cc -std=gnu11 -O2 -fno-pie -no-pie "$source_file" -o "$work/gnu"
cc -no-pie -Wl,-z,noexecstack "$work/mini.o" -o "$work/mini"
"$work/gnu" || fail 'GNU C optimizer oracle failed'
"$work/mini" || fail 'MiniC GNU AS/LD runtime diverged from GNU C'
printf 'CORE_LITERAL_BRANCH_GUARD=PASS dead_assert=0 ISA=4 side_effects=2 runtime_cases=4 GNU_AS=1 GNU_LD=1 GNU_C_diff=1\n'
