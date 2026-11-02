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

# A definition's own variadic bit must reach Core and RV32 frame construction.
# Inspect only the prologue before the first Core block: ordinary incoming
# parameter stores inside take_fixed's block are not a varargs save area.
awk '
    $0 == "take_variadic:" { variadic = 1; saw_variadic = 1; next }
    $0 == "take_fixed:" { fixed = 1; saw_fixed = 1; next }
    variadic && /^\.Ltake_variadic_rv32_bb0:$/ {
        variadic = 0; ended_variadic = 1; next
    }
    fixed && /^\.Ltake_fixed_rv32_bb0:$/ {
        fixed = 0; ended_fixed = 1; next
    }
    variadic && /^  sw a[1-7], [0-9]+\(sp\)$/ {
        reg = substr($2, 1, 2)
        split($3, parts, "(")
        saved[reg]++
        offsets[reg] = parts[1] + 0
    }
    fixed && /^  sw a[1-7],/ { fixed_saves++ }
    END {
        if (!saw_variadic || !ended_variadic || !saw_fixed || !ended_fixed ||
            fixed_saves != 0) {
            print "RV32_VARIADIC_DEFINITION=FAIL missing prologue or fixed callee saved unnamed registers" > "/dev/stderr"
            exit 1
        }
        for (i = 1; i <= 7; ++i) {
            reg = "a" i
            if (saved[reg] != 1 || (i > 1 && offsets[reg] != offsets["a" (i - 1)] + 4)) {
                printf "RV32_VARIADIC_DEFINITION=FAIL %s saves=%d offset=%d\n", \
                    reg, saved[reg], offsets[reg] > "/dev/stderr"
                exit 1
            }
        }
    }
' "$work/abi.s"

echo 'RV32_VARIADIC_PAIR=PASS natural_align4=a1/a2 packed_align1=a1/a2 align8=a2/a3 scalar8=a2/a3'
echo 'RV32_VARIADIC_DEFINITION=PASS callee_saved=a1..a7 fixed_callee_saved=none'
