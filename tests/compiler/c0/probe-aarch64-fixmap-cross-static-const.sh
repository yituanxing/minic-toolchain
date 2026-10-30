#!/bin/sh
# Original Linux fixed-index BUILD_BUG_ON semantics, without GNU C fallback.
# This deliberately exercises a plain static wrapper around always_inline:
# GCC -O2 can prove the caller's enum constant; MiniC must do the same.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mini=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host=${HOST_CC:-gcc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
source_file="$root/tests/compiler/c0/aarch64_fixmap_cross_static_const.c"

"$host" -E -P -std=gnu11 "$source_file" -o "$work/original.gnu.i"
"$host" -O2 -S -std=gnu11 "$source_file" -o "$work/gnu.s"
if grep -Fq '__compiletime_assert_fixmap' "$work/gnu.s"; then
    echo 'AARCH64_FIXMAP_STATIC_CONST=FAIL GNU -O2 oracle retained BUILD_BUG_ON' >&2
    exit 1
fi
"$mini" --target=aarch64-linux -S "$work/original.gnu.i" -o "$work/minic.s"
grep -Fq '  bl original_set_fixmap' "$work/minic.s"
if grep -Fq '__compiletime_assert_fixmap' "$work/minic.s"; then
    echo 'AARCH64_FIXMAP_STATIC_CONST=FAIL MiniC lost cross-static constant' >&2
    exit 1
fi
echo 'AARCH64_FIXMAP_STATIC_CONST=PASS GNU_O2=1 MiniC_AArch64=1 static_wrapper=1 enum=1 always_inline=1 no_GNU_C_fallback=1'
