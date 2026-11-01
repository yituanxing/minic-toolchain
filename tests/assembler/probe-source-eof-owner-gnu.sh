#!/usr/bin/env bash
# SourceGraph owns EOF site identity for synthetic local COMMON fragments;
# a nested collector must not reject directive text solely due to a short
# unused scratch argument buffer.
set -Eeuo pipefail
: "$MINIAS"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
python3 - "$work/eof.s" <<'PY'
from pathlib import Path
import sys
# Keep the long nested directive inside an unexpanded macro: this checks
# matching-only scanning, not target macro-default legality.
src = (".text\n.globl eof_site_probe\neof_site_probe:\n"
       ".macro unopened\n"
       ".macro nested p=" + ("9" * 1300) + "\n"
       ".endm\n.endm\n"
       ".rept 2\n.byte 0x19\n.endr\n"
       ".local local_eof_storage\n"
       ".comm local_eof_storage,24,16\n"
       )
Path(sys.argv[1]).write_text(src, encoding="ascii")
PY
as --64 -o "$work/gnu.o" "$work/eof.s"
MINIAS_TRACE_RELAYOUT=1 "$MINIAS" --target=x86_64 -o "$work/mini.o" \
  "$work/eof.s" 2>"$work/trace"
for kind in gnu mini; do
    objcopy -j .text -O binary "$work/$kind.o" "$work/$kind.bin"
done
cmp "$work/gnu.bin" "$work/mini.bin"
test "$(wc -c <"$work/mini.bin")" -eq 2
for kind in gnu mini; do
    readelf -sW "$work/$kind.o" |
        awk '$NF=="local_eof_storage" {
             if($3 != 24 || $5 != "LOCAL" || $7 == "UND") exit 1;
             seen=1
             } END {if(!seen)exit 1}'
done
grep -Fq 'minias-source-graph:file-reads=1' "$work/trace"
echo 'MINIAS_EOF_SOURCE_OWNER=PASS GNU_text=exact nested_long_match=1 local_common=1 file_reads=1'
