#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-target-pragma
mkdir -p "$work"

"$minic" --target=aarch64-linux -S \
    "$root/tests/compiler/c0/gnu_pragma_target_aarch64.c" \
    -o "$work/aarch64.s"
test -s "$work/aarch64.s"
grep -F '.globl main' "$work/aarch64.s" >/dev/null

# Exact AArch64 arm_neon.h private marker from a GNU-preprocessed TU. It
# requires an active target(+nothing+simd) snapshot and grants no extra
# instruction capability; adjacent/foreign spellings stay fail-closed.
"$minic" --target=aarch64-linux -S \
    "$root/tests/compiler/c0/gnu_pragma_neon_header_aarch64.i" \
    -o "$work/neon_header_marker.s"
test -s "$work/neon_header_marker.s"
grep -F '.globl main' "$work/neon_header_marker.s" >/dev/null

expect_failure() {
    target=$1
    name=$2
    message=$3
    case "$name" in
        *.i) source="$root/tests/compiler/c0/$name" ;;
        *) source="$root/tests/compiler/c0/$name.c" ;;
    esac
    if "$minic" --target="$target" -S \
        "$source" -o "$work/$name.s" \
        >"$work/$name.stdout" 2>"$work/$name.stderr"; then
        printf '%s\n' "FAIL $name: incorrectly admitted unsupported target pragma" >&2
        exit 1
    fi
    if ! grep -F "$message" "$work/$name.stderr" >/dev/null; then
        printf '%s\n' "FAIL $name: missing expected diagnostic" >&2
        sed -n '1,25p' "$work/$name.stderr" >&2
        exit 1
    fi
}

expect_failure aarch64-linux invalid_gnu_pragma_target_feature \
    'unsupported GCC target option for selected target'
expect_failure x86_64-linux invalid_gnu_pragma_target_cross_target \
    'unsupported GCC target option for selected target'
expect_failure aarch64-linux invalid_gnu_pragma_target_pop \
    'GCC pop_options has no matching push_options'
expect_failure aarch64-linux invalid_gnu_pragma_target_syntax \
    'unsupported GCC target option syntax'
expect_failure aarch64-linux invalid_gnu_pragma_neon_header_spoof.i \
    'unsupported GCC pragma directive'
expect_failure aarch64-linux invalid_gnu_pragma_neon_header_unselected.i \
    'unsupported GCC pragma directive'
expect_failure aarch64-linux invalid_gnu_pragma_neon_header_popped.i \
    'unsupported GCC pragma directive'
expect_failure aarch64-linux invalid_gnu_pragma_neon_header_trailing.i \
    'unsupported GCC pragma directive'
expect_failure x86_64-linux invalid_gnu_pragma_neon_header_foreign.i \
    'unsupported GCC pragma directive'

printf '%s\n' 'PASS compiler/c0 GCC target pragma AArch64=baseline-only stack=push/pop internal_arm_neon_header=exact_target_qualified malformed=reject foreign=reject unknown-features=reject; full_NEON_header=NOT_CERTIFIED'
