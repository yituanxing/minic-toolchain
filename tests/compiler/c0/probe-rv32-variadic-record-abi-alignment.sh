#!/bin/sh
# Inspect strict RV32I/ILP32 assembler ABI placements. Runtime execution
# requires a separately provisioned RV32 cross toolchain and QEMU runner.
set -eu
: "${MINIC_CC:?MINIC_CC path required}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

${CC:-cc} -E -P -x c "$root/tests/compiler/c0/rv32_variadic_record_abi_alignment.c" \
    -o "$work/abi.i"
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
    -S "$work/abi.i" -o "$work/abi.s"

for item in natural:1 packed:1 aligned:2 wide_scalar:2; do
    suffix=${item%%:*}
    begin=${item#*:}
    awk -v name="call_$suffix" -v begin="$begin" '
        $0 == name ":" { within = 1; found = 1; next }
        within && index($0, ".size " name ",") == 1 { within = 0; done = 1 }
        within && $0 ~ /^  mv a[0-7], t[0-6]$/ {
            split($2, parts, ",")
            reg = parts[1]
            if (reg == "a1") a1++
            if (reg == "a2") a2++
            if (reg == "a3") a3++
            if (reg == "a4") a4++
        }
        within && $0 == "  call accept_pair" { call++ }
        END {
            # Valid pair always consumes a2 and a3 for 8-byte alignment;
            # for size-8 align-1/4, the pair uses a1 and a2, tail a3.
            if (!found || !done || call != 1 || a2 == 0 || a3 == 0 ||
                (begin == 1 && (a1 == 0 || a4 != 0)) ||
                (begin == 2 && (a1 != 0 || a4 == 0))) {
                printf "RV32_VARIADIC_PAIR=FAIL %s expected_begin=a%d a1=%d a2=%d a3=%d a4=%d\n", \
                    name, begin, a1, a2, a3, a4 > "/dev/stderr"
                exit 1
            }
        }
    ' "$work/abi.s"
done

echo 'RV32_VARIADIC_PAIR=PASS natural_align4=a1/a2 packed_align1=a1/a2 align8=a2/a3 scalar8=a2/a3'
