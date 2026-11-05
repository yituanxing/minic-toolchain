#!/usr/bin/env bash
# GNU .file source names obey the same quoted escape grammar as .ascii.
# Check decoded file paths and STT_FILE names, not only a valid line table.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"
for tool in as readelf; do
    command -v "$tool" >/dev/null || { echo "missing GNU tool: $tool" >&2; exit 1; }
done
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

cat >"$work/quoted.s" <<'ASM'
.file "compilation\040unit.c"
.file 1 "a\040b.c"
.file 2 "C:\\src\\unit.c"
.file 3 "/build\040root/src\040dir/file\x2E_unit.c"
.file 4 "dir\040four/unit\040four.c"
.file 5 "quote\"mark.c"
.text
.globl quoted_debug_names
.type quoted_debug_names,@function
quoted_debug_names:
.loc 1 10 0
    nop
.loc 2 11 0
    nop
.loc 3 12 0
    nop
.loc 4 13 0
    nop
.loc 5 14 0
    ret
.size quoted_debug_names,.-quoted_debug_names
ASM

"$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/quoted.s"
as --64 -o "$work/gnu.o" "$work/quoted.s"
for object in mini gnu; do
    readelf -W --debug-dump=rawline "$work/$object.o" >"$work/$object.line"
    readelf -W -s "$work/$object.o" >"$work/$object.symbols"
    for item in 'a b.c' 'C:\src\unit.c' 'file._unit.c' 'unit four.c' 'quote"mark.c' \
                '/build root/src dir' 'dir four'; do
        if ! grep -Fq -- "$item" "$work/$object.line"; then
            echo "MINIAS_DWARF_FILE_ESCAPES=FAIL object=$object missing=$item" >&2
            exit 1
        fi
    done
    if ! grep -Fq -- 'compilation unit.c' "$work/$object.symbols"; then
        echo "MINIAS_DWARF_FILE_ESCAPES=FAIL object=$object unnumbered-file-symbol" >&2
        exit 1
    fi
    readelf -W --debug-dump=decodedline "$work/$object.o" >"$work/$object.rows"
    for line in 10 11 12 13 14; do
        if ! grep -Eq "[[:space:]]$line[[:space:]]" "$work/$object.rows"; then
            echo "MINIAS_DWARF_FILE_ESCAPES=FAIL object=$object row=$line" >&2
            exit 1
        fi
    done
done
echo 'MINIAS_DWARF_FILE_ESCAPES=PASS octal=1 hex=1 backslash=1 quoted_quote=1 split_directory=1 unnumbered_STT_FILE=1 line_rows=5 GNU_oracle=2'
