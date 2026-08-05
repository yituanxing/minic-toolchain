#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
riscv_cc=${RISCV_CC:-riscv64-buildroot-linux-musl-gcc}
qemu=${QEMU_RISCV64:-qemu-riscv64}
riscv_objdump=${RISCV_OBJDUMP:-}
work=${BUILD_DIR:-"$root/build/debug"}/tests/programs-c0

if ! command -v "$riscv_cc" >/dev/null 2>&1 ||
   ! command -v "$qemu" >/dev/null 2>&1; then
    printf '%s\n' "SKIP programs/c0: set RISCV_CC and QEMU_RISCV64"
    test "${REQUIRE_RISCV_RUNTIME:-0}" != 1
    exit
fi

if test -z "$riscv_objdump"; then
    candidate=${riscv_cc%gcc}objdump
    if command -v "$candidate" >/dev/null 2>&1; then
        riscv_objdump=$candidate
    fi
fi

mkdir -p "$work"

run_elf() {
    elf=$1
    stdout_file=$2
    stderr_file=$3
    status_file=$4

    set +e
    "$qemu" "$elf" >"$stdout_file" 2>"$stderr_file"
    status=$?
    set -e
    printf '%s\n' "$status" >"$status_file"
}

write_disassembly() {
    elf=$1
    output=$2

    if test -n "$riscv_objdump"; then
        "$riscv_objdump" -dr "$elf" >"$output" 2>&1 || true
    fi
}

report_difference() {
    name=$1
    kind=$2
    gcc_file=$3
    minic_file=$4

    printf '%s\n' "FAIL programs/c0/$name: $kind differs" >&2
    diff -u "$gcc_file" "$minic_file" >&2 || true
    write_disassembly "$work/$name.gcc.elf" "$work/$name.gcc.disasm"
    write_disassembly "$work/$name.minic.elf" "$work/$name.minic.disasm"
    printf '%s\n' "Artifacts retained in $work" >&2
    exit 1
}

run_program() {
    name=$1
    source="$root/tests/programs/c0/$name.c"

    "$riscv_cc" -std=c11 -O0 -static "$source" -o "$work/$name.gcc.elf"
    "$riscv_cc" -std=c11 -E -P -x c "$source" -o "$work/$name.i"
    "$minic" -S "$work/$name.i" -o "$work/$name.minic.s"
    "$riscv_cc" -static "$work/$name.minic.s" -o "$work/$name.minic.elf"

    run_elf \
        "$work/$name.gcc.elf" \
        "$work/$name.gcc.stdout" \
        "$work/$name.gcc.stderr" \
        "$work/$name.gcc.status"
    run_elf \
        "$work/$name.minic.elf" \
        "$work/$name.minic.stdout" \
        "$work/$name.minic.stderr" \
        "$work/$name.minic.status"

    if ! cmp -s "$work/$name.gcc.status" "$work/$name.minic.status"; then
        report_difference \
            "$name" "exit status" \
            "$work/$name.gcc.status" "$work/$name.minic.status"
    fi
    if ! cmp -s "$work/$name.gcc.stdout" "$work/$name.minic.stdout"; then
        report_difference \
            "$name" "standard output" \
            "$work/$name.gcc.stdout" "$work/$name.minic.stdout"
    fi
    if ! cmp -s "$work/$name.gcc.stderr" "$work/$name.minic.stderr"; then
        report_difference \
            "$name" "standard error" \
            "$work/$name.gcc.stderr" "$work/$name.minic.stderr"
    fi

    status=$(cat "$work/$name.minic.status")
    stdout_bytes=$(wc -c <"$work/$name.minic.stdout" | tr -d ' ')
    stderr_bytes=$(wc -c <"$work/$name.minic.stderr" | tr -d ' ')
    printf '%s\n' \
        "PASS programs/c0/$name exit=$status stdout=$stdout_bytes stderr=$stderr_bytes"
}

run_program gcd
run_program fibonacci
run_program prime_count
run_program collatz
run_program block_scope
run_program standalone_block
run_program function_calls
run_program function_prototype
run_program function_parameter
run_program function_two_parameters
run_program function_four_parameters
run_program function_eight_parameters
run_program recursive_factorial
run_program mutual_recursion
