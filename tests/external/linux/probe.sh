#!/bin/sh
set -eu

export LC_ALL=C
export LANG=C

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${BUILD_DIR:-"$root/build/linux-discovery"}
minic=${MINIC:-"$root/build/linux-compiler/bin/minic"}
cross_compile=${CROSS_COMPILE:-riscv64-linux-gnu-}
version=6.6.143
archive=${LINUX_ARCHIVE_CACHE:-"$work/linux-$version.tar.xz"}
src="$work/linux-$version"
out="$work/kbuild"
sha256=dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932
url="https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-$version.tar.xz"

rm -rf "$work"
mkdir -p "$work"
mkdir -p "$(dirname -- "$archive")"

archive_valid=false
if test -s "$archive" && printf '%s  %s\n' "$sha256" "$archive" | sha256sum -c - >/dev/null 2>&1; then
    archive_valid=true
    printf '%s\n' "LINUX_ARCHIVE_CACHE hit path=$archive"
fi
if test "$archive_valid" != true; then
    rm -f "$archive" "$archive.tmp"
    curl -fsSL "$url" -o "$archive.tmp"
    mv "$archive.tmp" "$archive"
    printf '%s\n' "LINUX_ARCHIVE_CACHE fill path=$archive"
fi
printf '%s  %s\n' "$sha256" "$archive" | sha256sum -c -
tar -xJf "$archive" -C "$work"

make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE="$cross_compile" defconfig \
    >"$work/defconfig.log" 2>&1
make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE="$cross_compile" -j4 V=1 init/main.o \
    >"$work/gcc-reference.log" 2>&1
make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE="$cross_compile" V=1 init/main.i \
    >"$work/preprocess.log" 2>&1

input="$out/init/main.i"
if test ! -s "$input"; then
    printf '%s\n' 'LINUX_PROBE_ERROR Kbuild did not produce init/main.i' >&2
    tail -n 120 "$work/preprocess.log" >&2
    exit 1
fi

line_count=$(wc -l < "$input" | tr -d ' ')
byte_count=$(wc -c < "$input" | tr -d ' ')
printf '%s\n' "LINUX_INPUT release=$version source=init/main.c lines=$line_count bytes=$byte_count"
python3 - "$input" "$work/main-i-inventory.txt" <<'PY'
from collections import Counter
from pathlib import Path
import re
import sys

input_path = Path(sys.argv[1])
output_path = Path(sys.argv[2])
text = input_path.read_text(errors="replace")

builtins = Counter(re.findall(r"\b__builtin_[A-Za-z0-9_]+\b", text))

def attribute_names(source: str):
    marker = "__attribute__"
    cursor = 0
    while True:
        start = source.find(marker, cursor)
        if start < 0:
            return
        pos = start + len(marker)
        while pos < len(source) and source[pos].isspace():
            pos += 1
        if pos + 1 >= len(source) or source[pos:pos + 2] != "((":
            cursor = pos
            continue
        pos += 2
        body_start = pos
        depth = 0
        in_string = None
        escaped = False
        while pos < len(source):
            ch = source[pos]
            if in_string is not None:
                if escaped:
                    escaped = False
                elif ch == "\\":
                    escaped = True
                elif ch == in_string:
                    in_string = None
                pos += 1
                continue
            if ch in "\"'":
                in_string = ch
                pos += 1
                continue
            if ch == "(":
                depth += 1
            elif ch == ")":
                if depth == 0 and pos + 1 < len(source) and source[pos + 1] == ")":
                    body = source[body_start:pos]
                    part_start = 0
                    inner_depth = 0
                    quote = None
                    escaped_inner = False
                    parts = []
                    for index, item in enumerate(body):
                        if quote is not None:
                            if escaped_inner:
                                escaped_inner = False
                            elif item == "\\":
                                escaped_inner = True
                            elif item == quote:
                                quote = None
                            continue
                        if item in "\"'":
                            quote = item
                        elif item == "(":
                            inner_depth += 1
                        elif item == ")" and inner_depth > 0:
                            inner_depth -= 1
                        elif item == "," and inner_depth == 0:
                            parts.append(body[part_start:index])
                            part_start = index + 1
                    parts.append(body[part_start:])
                    for part in parts:
                        match = re.match(r"\s*([A-Za-z_][A-Za-z0-9_]*)", part)
                        if match:
                            yield match.group(1)
                    cursor = pos + 2
                    break
                if depth > 0:
                    depth -= 1
            pos += 1
        else:
            cursor = start + len(marker)

attributes = Counter(attribute_names(text))
features = {
    "statement_expression": len(re.findall(r"\(\s*\{", text)),
    "typeof": len(re.findall(r"\b(?:typeof|__typeof|__typeof__)\b", text)),
    "generic": len(re.findall(r"\b_Generic\b", text)),
    "inline_asm": len(re.findall(r"\b(?:asm|__asm|__asm__)\b", text)),
    "asm_goto": len(re.findall(r"\b(?:asm|__asm|__asm__)\b[^;{]*\bgoto\b", text)),
    "computed_goto": len(re.findall(r"\bgoto\s*\*", text)),
    "labels_as_values": len(re.findall(r"&&\s*[A-Za-z_]", text)),
    "int128": len(re.findall(r"\b(?:__int128|__int128__)\b", text)),
    "atomic_keyword": len(re.findall(r"\b_Atomic\b", text)),
    "complex_keyword": len(re.findall(r"\b_Complex\b", text)),
    "static_assert": len(re.findall(r"\b_Static_assert\b", text)),
    "extension_marker": len(re.findall(r"\b__extension__\b", text)),
}

with output_path.open("w") as out:
    out.write(f"total_lines={text.count(chr(10))}\n")
    out.write(f"total_bytes={len(text.encode('utf-8', errors='replace'))}\n")
    out.write("features:\n")
    for name, count in sorted(features.items()):
        out.write(f"  {name}={count}\n")
    out.write("builtins:\n")
    for name, count in sorted(builtins.items()):
        out.write(f"  {name}={count}\n")
    out.write("attributes:\n")
    for name, count in sorted(attributes.items()):
        out.write(f"  {name}={count}\n")
PY
printf '%s\n' 'LINUX_INPUT_INVENTORY_BEGIN'
cat "$work/main-i-inventory.txt"
printf '%s\n' 'LINUX_INPUT_INVENTORY_END'

set +e
"$minic" -S "$input" -o "$work/init-main.s" \
    >"$work/minic.stdout" 2>"$work/minic.stderr"
status=$?
set -e

if test "$status" -ne 0; then
    frontier_line=$(sed -n 's/.*init\/main\.i:\([0-9][0-9]*\):.*/\1/p' "$work/minic.stderr" | head -n 1)
    if test -z "$frontier_line"; then
        frontier_line=1
    fi
    start_line=$((frontier_line > 18 ? frontier_line - 18 : 1))
    end_line=$((frontier_line + 18))
    printf '%s\n' "LINUX_BLOCKER release=$version source=init/main.c line=$frontier_line minic_status=$status" >&2
    printf '%s\n' "init/main.i frontier lines=$start_line-$end_line:" >&2
    nl -ba "$input" | sed -n "${start_line},${end_line}p" >&2
    printf '%s\n' 'MiniC diagnostic:' >&2
    sed -n '1,160p' "$work/minic.stderr" >&2
    exit "$status"
fi

printf '%s\n' "PASS external/linux release=$version arch=riscv config=defconfig source=init/main.c kbuild-preprocessed=1 lines=$line_count"
