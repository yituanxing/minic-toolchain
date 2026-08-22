#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-core-required-no-fallback

mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/core_required_no_fallback.c" \
    -o "$work/core_required_no_fallback.i"

"$minic" -S "$work/core_required_no_fallback.i" -o "$work/legacy.s"
MINIC_CORE_IR=strict "$minic" -S "$work/core_required_no_fallback.i" \
    -o "$work/shadow-strict.s"
if MINIC_CORE_CODEGEN=basic-v0 "$minic" -S "$work/core_required_no_fallback.i" \
    -o "$work/core-basic-v0.s" >"$work/core.stdout" 2>"$work/core.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/core-required-no-fallback: Core-owned target gap silently fell back' >&2
    exit 1
fi
grep -F "Core-owned function 'core_owned_call9' cannot be emitted by RV64 basic-v0" \
    "$work/core.stderr" >/dev/null
printf '%s\n' 'PASS compiler/c0/core-required-no-fallback lower-ok=1 target-gap=fail-closed legacy-default=1'
