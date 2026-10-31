#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
gcc=${CC:-gcc}
source="$root/tests/compiler/c0/gnu_linux_zoned_constant_switch.c"
driver="$root/tests/compiler/c0/gnu_linux_zoned_constant_switch_driver.c"

"$gcc" -E -P -std=gnu11 -x c "$source" -o "$work/owner.i"
"$minic" --target=x86_64-linux -S "$work/owner.i" -o "$work/mini.s"
as --64 "$work/mini.s" -o "$work/mini.o"
"$gcc" -std=gnu11 -O2 -fno-pie -fno-pic -c "$source" -o "$work/gcc.o"

# The disabled-configuration callback is absent from BOTH actual GNU ELF
# objects; the dynamic volatile control must retain its unresolved callee.
if nm -u "$work/mini.o" | grep -q 'minic_disabled_zoned_must_not_resolve'; then
    echo 'FAIL: MiniC retained a CONFIG-disabled zoned callback' >&2
    exit 1
fi
if nm -u "$work/gcc.o" | grep -q 'minic_disabled_zoned_must_not_resolve'; then
    echo 'FAIL: GCC reference disagrees with disabled-callback expectation' >&2
    exit 1
fi
nm -u "$work/mini.o" | grep -q ' U minic_dynamic_zoned_must_resolve$'
nm -u "$work/gcc.o" | grep -q ' U minic_dynamic_zoned_must_resolve$'

"$gcc" -no-pie "$work/mini.o" "$driver" -o "$work/mini-run"
"$gcc" -no-pie "$work/gcc.o" "$driver" -o "$work/gcc-run"
"$work/mini-run"
"$work/gcc-run"

# Optional exact frozen GNU Kbuild preprocessed owner from a failed CI run.
# Reuse the SAME original .i for MiniC and GCC, no Linux Kconfig edits.
if [ "${1:-}" != '' ]; then
    "$minic" --target=x86_64-linux -S "$1" -o "$work/linux-dm.s"
    as --64 "$work/linux-dm.s" -o "$work/linux-dm.o"
    "$gcc" -x cpp-output -std=gnu11 -O2 -fno-pie -fno-pic \
        -mcmodel=kernel -mno-red-zone -c "$1" -o "$work/linux-dm-gcc.o"
    if nm -u "$work/linux-dm.o" | grep -q ' U dm_zone_endio$'; then
        echo 'FAIL: MiniC retained original GNU Linux dm_zone_endio' >&2
        exit 1
    fi
    if nm -u "$work/linux-dm-gcc.o" | grep -q ' U dm_zone_endio$'; then
        echo 'FAIL: GCC retained original GNU Linux dm_zone_endio' >&2
        exit 1
    fi
fi

echo 'PASS compiler/c0/gnu-linux-zoned-constant-switch disabled=dead volatile=live getter-effect=preserved GNU-as=pass GNU-nm=differential'
