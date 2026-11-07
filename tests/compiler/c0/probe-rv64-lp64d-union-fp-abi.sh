#!/usr/bin/env bash
# MC-059: unions with FP fields use RV64 integer ABI; FP structs must reject.
# Where installed, cross-GNU AS/LD and QEMU verify the four ABI link pairings.
set -Eeuo pipefail
: "${MINIC_CC:?MINIC_CC path required}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
host_cc=${HOST_CC:-cc}
source="$root/tests/compiler/audit/mc059_rv64_lp64d_union_float_abi.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cases=(RETURN PARAM FLOAT32 CALL NESTED ARRAY FP_ONLY_UNION VARIADIC CONTROL SCALAR TRUE_FP_STRUCT)
gnu_target=0
qemu_target=0
if command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then gnu_target=1; fi
if command -v qemu-riscv64 >/dev/null 2>&1; then qemu_target=1; fi

for kind in "${cases[@]}"; do
    "$host_cc" -std=gnu11 -Werror -fsyntax-only -D"PROBE_${kind}" "$source"
    "$host_cc" -E -P -std=gnu11 -D"PROBE_${kind}" -x c "$source" \
        -o "$work/$kind.i"
    for profile in lp64 lp64d; do
        if [[ $profile == lp64 ]]; then march=rv64i; else march=rv64imafd; fi
        if (( gnu_target )); then
            riscv64-linux-gnu-gcc -march="$march" -mabi="$profile" -O0 \
                -S -D"PROBE_${kind}" "$source" \
                -o "$work/gnu-$profile-$kind.s"
        fi
        output="$work/minic-$profile-$kind.s"
        if [[ $profile == lp64d && $kind == TRUE_FP_STRUCT ]]; then
            if "$MINIC_CC" --target=riscv64-linux -march="$march" \
                -mabi="$profile" -S "$work/$kind.i" -o "$output" \
                >"$work/expected.out" 2>"$work/expected.err"; then
                echo "MC059_RV64_UNION_FP_ABI=FAIL accepted true FP struct" >&2
                exit 1
            fi
            if [[ -e $output ]] || ! grep -q 'cannot emit RISC-V function' "$work/expected.err"; then
                echo "MC059_RV64_UNION_FP_ABI=FAIL true FP struct fail-closed contract" >&2
                exit 1
            fi
        else
            "$MINIC_CC" --target=riscv64-linux -march="$march" \
                -mabi="$profile" -S "$work/$kind.i" -o "$output"
            if (( gnu_target )); then
                riscv64-linux-gnu-gcc -march="$march" -mabi="$profile" \
                    -c "$output" -o "$work/minic-$profile-$kind.o"
            fi
        fi
    done
done

runtime=skipped_no_cross_toolchain_or_qemu
if (( gnu_target && qemu_target )); then
    for unit in provider consumer; do
        cfile="$root/tests/compiler/c0/rv64_union_fp_abi_$unit.c"
        "$host_cc" -E -P -std=gnu11 "$cfile" -o "$work/$unit.i"
        riscv64-linux-gnu-gcc -march=rv64imafd -mabi=lp64d \
            -ffreestanding -fno-builtin -fno-stack-protector -O0 \
            -c "$cfile" -o "$work/gnu-$unit.o"
        "$MINIC_CC" --target=riscv64-linux -march=rv64imafd -mabi=lp64d \
            -S "$work/$unit.i" -o "$work/mini-$unit.s"
        riscv64-linux-gnu-gcc -march=rv64imafd -mabi=lp64d -c \
            "$work/mini-$unit.s" -o "$work/mini-$unit.o"
    done
    # Linux syscall 93 exits with consumer's result; no libc/CRT required.
    printf '%s\n' '.text' '.globl _start' '_start:' \
        'call main' 'li a7, 93' 'ecall' >"$work/start.S"
    riscv64-linux-gnu-gcc -march=rv64imafd -mabi=lp64d -c \
        "$work/start.S" -o "$work/start.o"
    for caller in gnu mini; do
        for callee in gnu mini; do
            riscv64-linux-gnu-gcc -march=rv64imafd -mabi=lp64d -nostdlib \
                -static -no-pie -Wl,-e,_start "$work/start.o" \
                "$work/$caller-consumer.o" "$work/$callee-provider.o" \
                -lgcc -o "$work/abi-$caller-$callee.elf"
            if ! readelf -h "$work/abi-$caller-$callee.elf" | grep -q 'Machine:.*RISC-V'; then
                echo "MC059_RV64_UNION_FP_ABI=FAIL ELF machine $caller/$callee" >&2
                exit 1
            fi
            set +e
            qemu-riscv64 "$work/abi-$caller-$callee.elf"
            code=$?
            set -e
            if [[ $code != 42 ]]; then
                echo "MC059_RV64_UNION_FP_ABI=FAIL QEMU $caller/$callee exit=$code" >&2
                exit 1
            fi
        done
    done
    runtime=GNU_GAS_ELF_QEMU_4of4_exit42
fi

echo "MC059_RV64_UNION_FP_ABI=PASS LP64=11of11 LP64D_integer_union_scalar=10of10 true_FP_struct=reject runtime=$runtime"
