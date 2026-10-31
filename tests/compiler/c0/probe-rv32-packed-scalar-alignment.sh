#!/bin/sh
set -eu
: "${MINIC_CC:?MINIC_CC path required}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
source_file="$root/tests/compiler/c0/rv32_packed_scalar_alignment.c"
${CC:-cc} -E -P -x c "$source_file" -o "$work/packed.i"
${CC:-cc} -E -P -x c -DMINIC_PROBE_NO_MAIN "$source_file" \
    -o "$work/packed-cross.i"

# Independent GNU native and MiniC x86 semantics (including binary64).
${CC:-cc} -std=gnu11 -O0 -Wall -Wextra -Werror "$source_file" -o "$work/gcc"
"$work/gcc"
"$MINIC_CC" --target=x86_64-linux -S "$work/packed.i" -o "$work/native.s"
${CC:-cc} -no-pie "$work/native.s" -o "$work/minic"
"$work/minic"

# Every target must accept the same preprocessed C source.  RV32I/ILP32
# emits only Base-I instructions; the GNU assembler still owns libcalls.
for target in riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
    case "$target" in
        riscv32-linux) set -- -march=rv32i -mabi=ilp32 ;;
        riscv64-linux) set -- -march=rv64i -mabi=lp64 ;;
        *) set -- ;;
    esac
    "$MINIC_CC" --target="$target" "$@" -S "$work/packed-cross.i" \
        -o "$work/$target.s"
done

# Every unproven typed pointer must have a bytewise fallback, including
# the high/low halves of long long and double, and both signed and unsigned
# narrow scalar accesses.  A direct native load is only reachable after its
# exact runtime alignment guard.  Record copies are a separate Core opcode.
for item in 'packed_read_short:lbu:2' 'packed_read_word:lbu:4' \
            'packed_read_wide:lbu:8' 'packed_read_double:lbu:8' \
            'packed_write_short:sb:2' 'packed_write_word:sb:4' \
            'packed_write_wide:sb:8' 'packed_write_double:sb:8'; do
    symbol=${item%%:*}
    remainder=${item#*:}
    opcode=${remainder%%:*}
    bytes=${remainder##*:}
    awk -v name="$symbol" -v op="$opcode" -v expected="$bytes" '
        $0 == name ":" { inside=1; found=1; next }
        inside && index($0, ".size " name ",") == 1 { done=1; inside=0 }
        inside && $0 ~ ("^  " op " ") { bytewise++ }
        inside && $0 ~ /^  andi t3, t0, [13]$/ { guard++ }
        END { if (!found || !done || bytewise < expected || !guard) {
            printf "RV32_PACKED_SCALAR=FAIL %s bytewise=%d guard=%d\n", name, bytewise, guard > "/dev/stderr"
            exit 1
        }}
    ' "$work/riscv32-linux.s"
done

# A Core-owned local with natural alignment must retain direct scalar memory
# instructions, avoiding a needless runtime guard on its field access.
awk '
    $0 == "natural_local_fast_path:" { inside=1; found=1; next }
    inside && index($0, ".size natural_local_fast_path,") == 1 { done=1; inside=0 }
    inside && $0 ~ /^  andi t3, t0, [13]$/ { guard++ }
    inside && $0 ~ /^  (lw|sw) / { native++ }
    END { if (!found || !done || guard || !native) {
        printf "RV32_NATURAL_ALIGN_FAST=FAIL native=%d guard=%d\n", native, guard > "/dev/stderr"
        exit 1
    }}
' "$work/riscv32-linux.s"

echo 'RV32_PACKED_SCALAR_ALIGNMENT=PASS gcc-native minic-x86 five-ISA RV32I-guarded-bytewise'
