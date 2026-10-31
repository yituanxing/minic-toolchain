#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-cc}}
nm_tool=${NM:-nm}
work=$(mktemp -d "${TMPDIR:-/tmp}/minic-x86-reachable.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

dead="$root/tests/compiler/c0/x86_reachable_dead_externals.c"
live="$root/tests/compiler/c0/x86_reachable_live.c"
driver="$root/tests/compiler/c0/x86_reachable_live_driver.c"

# GNU only preprocesses/assembles/links MiniC output. A dead C branch must not
# create undefined references which the optimized GNU C control also omits.
"$host_cc" -E -P -std=gnu11 "$dead" -o "$work/dead.i"
"$minic_cc" --target=x86_64-linux -S "$work/dead.i" -o "$work/dead.s"
"$host_cc" -c -x assembler "$work/dead.s" -o "$work/dead.minic.o"
"$host_cc" -O2 -c -std=gnu11 "$dead" -o "$work/dead.gnu.o"
"$nm_tool" -u "$work/dead.minic.o" > "$work/dead.minic.undefined"
"$nm_tool" -u "$work/dead.gnu.o" > "$work/dead.gnu.undefined"
for symbol in mesh_update __compiletime_assert_212; do
    if grep -E "[[:space:]]${symbol}$" "$work/dead.minic.undefined" || \
       grep -E "[[:space:]]${symbol}$" "$work/dead.gnu.undefined"; then
        printf 'FAIL x86 dead branch emits external %s\n' "$symbol" >&2
        exit 1
    fi
done

# The pruning must preserve executable conditional calls and an explicit
# source label reached by goto despite its enclosing `if (0)` source arm.
"$host_cc" -E -P -std=gnu11 "$live" -o "$work/live.i"
"$minic_cc" --target=x86_64-linux -S "$work/live.i" -o "$work/live.s"
"$host_cc" -c -x assembler "$work/live.s" -o "$work/live.minic.o"
"$nm_tool" -u "$work/live.minic.o" > "$work/live.undefined"
for symbol in visible_side_effect observable_condition; do
    if ! grep -Eq "[[:space:]]${symbol}$" "$work/live.undefined"; then
        printf 'FAIL x86 live external call disappeared: %s\n' "$symbol" >&2
        exit 1
    fi
done
if ! grep -q '\.Luser_' "$work/live.s"; then
    printf 'FAIL x86 live source goto label was discarded\n' >&2
    exit 1
fi
"$host_cc" -std=gnu11 -O2 -no-pie "$live" "$driver" -o "$work/gnu.elf"
"$host_cc" -no-pie "$work/live.minic.o" "$driver" -o "$work/minic.elf"
"$work/gnu.elf" > "$work/gnu.output"
"$work/minic.elf" > "$work/minic.output"
if ! cmp -s "$work/gnu.output" "$work/minic.output" || \
   ! grep -Fxq '153 54 2' "$work/minic.output"; then
    printf 'FAIL x86 GNU/MiniC reachable side-effect/source-goto differential\n' >&2
    diff -u "$work/gnu.output" "$work/minic.output" || true
    exit 1
fi
printf '%s\n' 'PASS x86 GNU AS/nm unreachable externals + live condition/source-goto runtime differential'
