#!/usr/bin/env bash
set -Eeuo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
cc=${HOST_CC:-gcc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { echo "CORE_INDIRECT_LOCAL_FACT_ESCAPE=FAIL $*" >&2; exit 1; }

test -x "$minic" || fail 'MiniC binary missing'
src="$root/tests/compiler/c0/core_indirect_local_fact_escape.c"
"$cc" -std=gnu11 -O0 -E -P "$src" -o "$work/input.i" \
    || fail 'GNU preprocessing'
"$cc" -std=gnu11 -O0 -no-pie -x c "$work/input.i" -o "$work/gnu" \
    || fail 'GNU reference compilation'
"$minic" --target=x86_64-linux -S "$work/input.i" -o "$work/minic.s" \
    || fail 'MiniC compilation'
"$cc" -no-pie -x assembler "$work/minic.s" -o "$work/minic" \
    || fail 'GNU assembly/link'
"$work/gnu" || fail 'GNU reference runtime'
"$work/minic" || fail 'MiniC runtime differs from GNU reference'
echo 'CORE_INDIRECT_LOCAL_FACT_ESCAPE=PASS GNU_C_and_MiniC_GNU_AS_LD_runtime'
