#!/usr/bin/env bash
# Dynamic ?: / && / || expression merges must not leak path-specific local
# constants into a later `if (x)` guard or drop mandatory side effects.
set -Eeuo pipefail
mini=${1:?MiniC compiler path}
test -x "$mini"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
source_file=tests/compiler/c0/core_expr_branch_local_fact_merge.c
host_cc=${HOST_CC:-gcc}

"$host_cc" -E -P -std=gnu11 -x c "$source_file" -o "$work/input.i"
"$host_cc" -O0 -std=gnu11 -no-pie -x c "$work/input.i" -o "$work/gnu"

for target in x86_64 riscv32 riscv64 aarch64 loongarch64; do
    "$mini" --target="$target-linux" -S "$work/input.i" \
        -o "$work/$target.s"
    test -s "$work/$target.s"
done
"$host_cc" -no-pie -x assembler "$work/x86_64.s" -o "$work/minic"

for name in gnu minic; do
    status=0
    "$work/$name" >"$work/$name.stdout" 2>"$work/$name.stderr" || status=$?
    if [[ "$status" != 42 ]]; then
        printf 'CORE_EXPR_BRANCH_FACTS=FAIL runner=%s status=%s\n' "$name" "$status" >&2
        exit 1
    fi
done
cmp "$work/gnu.stdout" "$work/minic.stdout"
cmp "$work/gnu.stderr" "$work/minic.stderr"
echo 'CORE_EXPR_BRANCH_FACTS=PASS cases=25 source_isa=5 x86_GNU_native=1'
