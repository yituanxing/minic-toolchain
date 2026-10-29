#!/bin/sh
set -eu
: "${MINIC_CC:?MINIC_CC path required}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
source_file="$root/tests/compiler/c0/riscv_packed_structured_asm_alignment.c"

# GNU's native packed lvalue oracle exercises both read/write and write-only
# register constraints. The source-owned ISA text is selected independently.
${CC:-cc} -std=gnu11 -O0 -Wall -Wextra -Werror \
    "$source_file" -o "$work/gnu-native"
"$work/gnu-native"
${CC:-cc} -E -P -x c -DRISCV_ASM_PROBE_TARGET "$source_file" \
    -o "$work/riscv.i"

for target in riscv32-linux riscv64-linux; do
    case "$target" in
        riscv32-linux) set -- -march=rv32i -mabi=ilp32 ;;
        riscv64-linux) set -- -march=rv64i -mabi=lp64 ;;
    esac
    "$MINIC_CC" --target="$target" "$@" -S "$work/riscv.i" \
        -o "$work/$target.s"
    for name in packed_asm_readwrite packed_asm_output packed_asm_wide; do
        awk -v name="$name" '
            $0 == name ":" { inside=1; found=1; next }
            inside && index($0, ".size " name ",") == 1 { done=1; inside=0 }
            inside && /^  lbu / { loads++ }
            inside && /^  sb / { stores++ }
            inside && /\.Lminic_typed_.*_native/ { guards++ }
            END {
                required_loads = name == "packed_asm_output" ? 0 : 4
                required_stores = 4
                if (!found || !done || loads < required_loads ||
                    stores < required_stores || guards < 1) {
                    printf "PACKED_ASM_TRANSFER=FAIL %s loads=%d stores=%d guards=%d\n", name, loads, stores, guards > "/dev/stderr"
                    exit 1
                }
            }
        ' "$work/$target.s"
    done
    awk -v target="$target" '
        $0 == "packed_asm_large_frame:" { inside=1; next }
        inside && index($0, ".size packed_asm_large_frame,") == 1 { inside=0 }
        inside && /^  li t2, / { large_offset=1 }
        inside && /MINIC_PACKED_ASM_INPUTS/ {
            marker++
            if (target == "riscv64-linux" &&
                $0 ~ /(^|[[:space:]])t[256]([[:space:]]|$)/)
                bad=1
            if (target == "riscv32-linux" &&
                $0 ~ /(^|[[:space:]])t6([[:space:]]|$)/)
                bad=1
        }
        END {
            if (marker != 1 || bad ||
                (target == "riscv64-linux" && !large_offset)) {
                printf "PACKED_ASM_LARGE_FRAME=FAIL %s marker=%d reserved=%d large=%d\n", target, marker, bad, large_offset > "/dev/stderr"
                exit 1
            }
        }
    ' "$work/$target.s"
    awk '
        /^\.Lminic_typed_.*_(native|done):$/ {
            label=$0
            sub(/:$/, "", label)
            if (seen[label]++) {
                print "PACKED_ASM_LABEL=FAIL duplicate " label > "/dev/stderr"
                exit 1
            }
            count++
        }
        END { if (!count) {
            print "PACKED_ASM_LABEL=FAIL no labels" > "/dev/stderr"
            exit 1
        }}
    ' "$work/$target.s"
done
echo 'RISCV_PACKED_STRUCTURED_ASM_ALIGNMENT=PASS GNU-native RV32I/RV64I -S guarded-ingress-egress large-frame-live-regs'
