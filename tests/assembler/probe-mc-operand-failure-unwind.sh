#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$repo_root"
minias=${MINIAS:-"$repo_root/build/release/bin/minic-as"}
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT

# Exercise real four-target parser error exits in addition to the standalone
# direct union-ownership unit test.  A parser rejection is required to exit
# cleanly; a signal (segfault, abort or double free) is never an expected
# negative-test success.  The failures occur in distinct ISA operand shapes.
reject() {
    local target=$1 case_name=$2 source=$3 diagnostic=$4 status
    printf '%s\n' "$source" > "$scratch/$case_name.s"
    set +e
    "$minias" --target="$target" -o "$scratch/$case_name.o" \
        "$scratch/$case_name.s" > "$scratch/$case_name.log" 2>&1
    status=$?
    set -e
    if (( status == 0 || status >= 128 )); then
        printf 'MCInst failure unwinder got bad status target=%s case=%s status=%d\n' \
            "$target" "$case_name" "$status" >&2
        sed -n '1,12p' "$scratch/$case_name.log" >&2
        return 1
    fi
    if ! grep -Fq "$diagnostic" "$scratch/$case_name.log"; then
        printf 'MCInst failure unwinder expected ISA diagnostic target=%s case=%s marker=%s\n' \
            "$target" "$case_name" "$diagnostic" >&2
        sed -n '1,12p' "$scratch/$case_name.log" >&2
        return 1
    fi
    if ! grep -Fq 'line=2' "$scratch/$case_name.log"; then
        printf 'MCInst failure unwinder lost lexical site target=%s case=%s\n' \
            "$target" "$case_name" >&2
        return 1
    fi
    if [[ ${MINIAS_NEGATIVE_VERBOSE:-0} == 1 ]]; then
        printf 'NEGATIVE %s %s: ' "$target" "$case_name"
        sed -n '1p' "$scratch/$case_name.log"
    fi
}

reject aarch64 a64_bad_symbolic_load $'.text\nldrb x0, [x1, #:lo12:payload]\npayload: .word 1' 'bad-aarch64-operands'
reject aarch64 a64_bad_symbol_difference $'.text\nadrp x0, payload-.\npayload: .word 1' 'bad-aarch64-operands'
reject riscv rv_bad_typed_register $'.text\nlw x32, payload\npayload: .word 1' 'invalid-riscv-mcinst'
reject riscv rv_bad_branch_register $'.text\nbeq x32, x0, payload\npayload: .word 1' 'invalid-riscv-mcinst'
reject loongarch64 la_bad_symbol_operand $'.text\nla.pcrel $a0, !!invalid_symbol\n' 'unsupported-loongarch64-operands'
reject x86_64 x64_bad_symbolic_memory $'.text\nmovq $payload, (%rax,%rcx,3)\npayload: .long 1' 'unsupported-x86_64-operand'

printf 'MINIAS_MC_OPERAND_FAILURE_UNWIND=PASS arches=4 negative=6 signal_fail_closed=1\n'
