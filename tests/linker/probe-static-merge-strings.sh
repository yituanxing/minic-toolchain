#!/usr/bin/env bash
# Cross-object SHF_MERGE + SHF_STRINGS: canonical symbol & addend remapping,
# suffix folding and real GNU BFD runtime behavior.
set -Eeuo pipefail

repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
minild=${MINILD:-"$repo/build/release/bin/minic-ld"}
gnu_ld=${GNU_LD:-ld.bfd}
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-merge-strings.XXXXXX")
trap 'rm -rf -- "$work"' EXIT

for part in a b; do
    cc -c -o "$work/$part.o" \
        "$repo/tests/linker/fixtures/merge-strings-$part.S"
done
for linker in "$gnu_ld" "$minild"; do
    if [[ $linker == "$gnu_ld" ]]; then tag=gnu; else tag=mini; fi
    "$linker" -m elf_x86_64 -static -e _start \
        -o "$work/$tag.elf" "$work/a.o" "$work/b.o"
    timeout 3 "$work/$tag.elf"
    readelf -SW "$work/$tag.elf" > "$work/$tag.sections"
    "$linker" -m elf_x86_64 -static -n -e _start \
        -T "$repo/tests/linker/fixtures/merge-owner-script.lds" \
        -o "$work/$tag.script.elf" "$work/a.o" "$work/b.o"
    readelf -SW "$work/$tag.script.elf" > "$work/$tag.script.sections"
    grep -q ' .merge_one ' "$work/$tag.script.sections"
    grep -q ' .merge_two ' "$work/$tag.script.sections"
    objcopy --dump-section ".data=$work/$tag.script.data" \
        "$work/$tag.script.elf"
    python3 - "$work/$tag.script.data" "$tag" <<'PY'
import struct
import sys
from pathlib import Path

data = Path(sys.argv[1]).read_bytes()
if len(data) < 64:
    raise SystemExit(f"{sys.argv[2]}: expected both pointer arrays")
first_a = struct.unpack_from("<Q", data, 0)[0]
first_b = struct.unpack_from("<Q", data, 56)[0]
if first_a == first_b:
    raise SystemExit(f"{sys.argv[2]}: folded different script owners")
PY
done

echo "MINILD_STATIC_MERGE=PASS gnu_runtime=1 mini_runtime=1 exact_and_suffix_fold=1 wide_strings=2,4 fixed_constant=4 distinct_script_owner=2"
