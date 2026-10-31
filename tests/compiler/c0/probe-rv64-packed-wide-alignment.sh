#!/bin/sh
set -eu
: "${MINIC_CC:?MINIC_CC path required}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
source_file="$root/tests/compiler/c0/rv64_packed_wide_alignment.c"
${CC:-cc} -std=gnu11 -Werror -fsyntax-only "$source_file"
${CC:-cc} -E -P -x c "$source_file" -o "$work/wide.i"
"$MINIC_CC" --target=riscv64-linux -march=rv64i -mabi=lp64 \
    -S "$work/wide.i" -o "$work/wide.s"
for item in packed_wide_read:lbu packed_wide_write:sb \
            packed_long_double_read:lbu packed_long_double_write:sb; do
    name=${item%%:*}
    opcode=${item#*:}
    awk -v name="$name" -v op="$opcode" '
        $0 == name ":" { inside=1; found=1; next }
        inside && index($0, ".size " name ",") == 1 { done=1; inside=0 }
        inside && $0 ~ ("^  " op " ") { bytes++ }
        inside && /\.Lminic_typed_rv64_memory_.*_native/ { guards++ }
        END { if (!found || !done || bytes < 16 || guards < 2) {
            printf "RV64_PACKED_WIDE=FAIL %s bytes=%d guards=%d\n", name, bytes, guards > "/dev/stderr"
            exit 1
        }}
    ' "$work/wide.s"
done
echo 'RV64_PACKED_WIDE_ALIGNMENT=PASS int128 long-double 2x8byte guarded-bytewise'
