#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cc=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
work=${BUILD_DIR:-"$root/build/release"}/tests/aarch64-gnu-inline-lazy-negatives
mkdir -p "$work"

# An abbreviated/spoofed arm_neon.h header must NOT be authenticated merely
# because its pragma marker and extern-inline syntax look like GCC's.
"$cc" --target=aarch64-linux -S \
    "$root/tests/compiler/c0/aarch64_gnu_inline_lazy_compatible_typedef.c" \
    -o "$work/compatible-typedef.s"
for name in unused_header used_unsupported used_indirect plain_invalid unbalanced \
    bad_spoof after_header_invalid after_header_inline_invalid \
    conflicting_typedef after_pop_spoof; do
    source="$root/tests/compiler/c0/aarch64_gnu_inline_lazy_$name.c"
    if "$cc" --target=aarch64-linux -S "$source" -o "$work/$name.s" \
        >"$work/$name.stdout" 2>"$work/$name.stderr"; then
        echo "FAIL GNU header-only lazy body accepted invalid case=$name" >&2
        exit 1
    fi
done
if "$cc" --target=x86_64-linux -S \
    "$root/tests/compiler/c0/aarch64_gnu_inline_lazy_unused_header.c" \
    -o "$work/foreign.s" >"$work/foreign.stdout" 2>"$work/foreign.stderr"; then
    echo 'FAIL GNU AArch64 header-only lazy policy leaked to foreign target' >&2
    exit 1
fi
echo 'PASS AArch64 GNU header inline lazy parse: abbreviated marker spoof rejected, used and indirect fail, compatible/invalid typedef separated, scope and foreign ISA enforced'
