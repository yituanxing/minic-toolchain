#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-external-array-declarator-routing

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -fsyntax-only -std=gnu11 -Werror -Wno-pedantic -x c \
  "$root/tests/compiler/c0/external_array_declarator_routing.c"
"$host_cc" -E -P -std=gnu11 -x c \
  "$root/tests/compiler/c0/external_array_declarator_routing.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"
for symbol in cpu_ops empty_zero_page purgatory_sha256_digest purgatory_sha_regions initialized_map completed_tentative completed_definition; do
  grep -F "$symbol:" "$work/output.s" >/dev/null
done
grep -F '.data..ro_after_init' "$work/output.s" >/dev/null
grep -F '.bss..page_aligned' "$work/output.s" >/dev/null
grep -F '.kexec-purgatory' "$work/output.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/external_array_declarator_routing complete-array=generic-object suffix-attrs=1 multidim=1 initialized=1'
