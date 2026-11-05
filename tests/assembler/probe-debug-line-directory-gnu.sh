#!/usr/bin/env bash
# Exercise GNU's directory/file split, file-index references and persistent
# .loc state. Neither raw .debug_line bytes nor a filename substring alone
# establishes source-level DWARF fidelity.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"
for tool in as readelf; do
    command -v "$tool" >/dev/null || { echo "missing GNU tool: $tool" >&2; exit 1; }
done
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/line.s" <<'ASM'
.file 1 "/build/project/src/first.c"
.file 2 "/build/project/include/second.h"
.file 3 "third.c"
.file 4 "/build/project/src/fourth.c"
.text
.globl debugger_directory_probe
.type debugger_directory_probe,@function
debugger_directory_probe:
    .loc 1 10 2 is_stmt 1
    nop
    .loc 2 12 4 is_stmt 0 basic_block prologue_end discriminator 7
    nop
    .loc 3 14 8 is_stmt 0 isa 1 epilogue_begin
    nop
    .loc 4 16 1 is_stmt 1
    ret
.size debugger_directory_probe,.-debugger_directory_probe
ASM
"$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/line.s"
as --64 -o "$work/gnu.o" "$work/line.s"
readelf --debug-dump=rawline "$work/mini.o" >"$work/mini.txt"
readelf --debug-dump=rawline "$work/gnu.o" >"$work/gnu.txt"
for token in first.c second.h third.c fourth.c; do
    grep -q "$token" "$work/mini.txt"
    grep -q "$token" "$work/gnu.txt"
done
for output in "$work/mini.txt" "$work/gnu.txt"; do
    grep -Eq '^[[:space:]]*1[[:space:]]+/build/project/src$' "$output"
    grep -Eq '^[[:space:]]*2[[:space:]]+/build/project/include$' "$output"
    grep -Eq '^[[:space:]]*1[[:space:]]+1[[:space:]]+0[[:space:]]+0[[:space:]]+first\.c$' "$output"
    grep -Eq '^[[:space:]]*2[[:space:]]+2[[:space:]]+0[[:space:]]+0[[:space:]]+second\.h$' "$output"
    grep -Eq '^[[:space:]]*3[[:space:]]+0[[:space:]]+0[[:space:]]+0[[:space:]]+third\.c$' "$output"
    grep -Eq '^[[:space:]]*4[[:space:]]+1[[:space:]]+0[[:space:]]+0[[:space:]]+fourth\.c$' "$output"
done
if [[ "${MINIAS_DEBUG_DIRECTORY_DIAGNOSTIC:-0}" == 1 ]]; then
    echo 'GNU .debug_line table:'
    sed -n '1,90p' "$work/gnu.txt"
    echo 'MiniAS .debug_line table:'
    sed -n '1,90p' "$work/mini.txt"
fi
echo 'MINIAS_DEBUG_LINE_DIRECTORY=PASS GNU_file_names=4 mini_file_names=4 unique_directories=2'
