#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-cc}}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT HUP INT TERM

"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/parser_probe_hash_ownership.c" -o "$work/positive.i"
"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/invalid_parser_probe_hash_ownership.c" -o "$work/negative.i"
"$host_cc" -c -std=gnu11 "$root/tests/compiler/c0/parser_probe_hash_ownership.c" \
    -o "$work/positive-gnu.o"
if "$host_cc" -c -std=gnu11 \
    "$root/tests/compiler/c0/invalid_parser_probe_hash_ownership.c" \
    -o "$work/negative-gnu.o" 2>"$work/negative-gnu.err"; then
    printf '%s\n' 'FAIL GNU: nonconstant static array designator accepted' >&2
    exit 1
fi

for target in x86_64-linux riscv64-linux riscv32-linux aarch64-linux loongarch64-linux; do
    "$minic_cc" --target="$target" -S "$work/positive.i" \
        -o "$work/positive-$target.s"
    if "$minic_cc" --target="$target" -S "$work/negative.i" \
        -o "$work/negative-$target.s" 2>"$work/negative-$target.err"; then
        printf '%s\n' "FAIL $target: nonconstant static array designator accepted" >&2
        exit 1
    fi
    if ! grep -F 'error:' "$work/negative-$target.err" >/dev/null; then
        printf '%s\n' "FAIL $target: nonconstant static array designator crashed" >&2
        exit 1
    fi
done

# Exercise incremental inserts and several resize boundaries in all three
# file-scope caches, first-declaration lookup, local shadowing, and GNU oracles.
python3 - "$work/name-index.c" <<'PY'
import sys
with open(sys.argv[1], "w") as out:
    for i in range(80):
        print(f"typedef int alias_{i};", file=out)
        print(f"enum {{ enum_{i} = {i} }};", file=out)
        print(f"extern int global_{i};", file=out)
        print(f"int function_{i}(alias_{i} x);", file=out)
        print(f"int global_{i} = {i};", file=out)
        print(f"int function_{i}(alias_{i} x) {{ return global_{i} + x; }}", file=out)
        print(f"int use_{i}(void) {{ return function_{i}((alias_{i})(enum_{i} - {i} + 1)); }}", file=out)
    print("int shadow(void) { typedef int alias_0; alias_0 global_0 = 7; return global_0; }", file=out)
    print("int enum_shadow(void) { int n = enum_0; { enum { enum_0 = 7 }; n += enum_0; } return n + enum_0; }", file=out)
    print("int main(void) { return (use_0() + use_79() + shadow() == 88 && enum_79 == 79 && enum_shadow() == 7) ? 0 : 1; }", file=out)
PY
"$host_cc" -std=gnu11 "$work/name-index.c" -o "$work/name-index-gnu"
"$work/name-index-gnu"
"$host_cc" -E -P -std=gnu11 -x c "$work/name-index.c" -o "$work/name-index.i"
for target in x86_64-linux riscv64-linux riscv32-linux aarch64-linux loongarch64-linux; do
    "$minic_cc" --target="$target" -S "$work/name-index.i" -o "$work/name-index-$target.s"
done
"$host_cc" -no-pie "$work/name-index-x86_64-linux.s" -o "$work/name-index-minic"
"$work/name-index-minic"
printf '%s\n' 'PASS parser name-index resize/redeclaration/shadowing: five ISA source output, GNU and MiniC x86 runtime'

printf '%s\n' \
    'PASS parser probe hash ownership: GNU positive, five ISA positive/negative, no double-free'
