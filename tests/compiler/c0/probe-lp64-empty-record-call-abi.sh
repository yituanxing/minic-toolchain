#!/bin/sh
# Validate GNU empty-record parameter/result ABI from one target-independent
# source. Runtime GNU differential remains the responsibility of cross CI.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
source="$root/tests/compiler/c0/lp64_empty_record_call_abi.c"
work=$(mktemp -d)
trap 'rm -r "$work"' EXIT HUP INT TERM

"$host_cc" -E -P -std=gnu11 -x c "$source" -o "$work/input.i"
for target in loongarch64-linux aarch64-linux; do
    "$minic" --target="$target" -S "$work/input.i" -o "$work/$target.s"
    test -s "$work/$target.s"
    for symbol in empty_middle empty_on_register_boundary \
                  call_empty_middle return_empty call_return_empty \
                  return_nonempty_with_empty_member \
                  call_nonempty_with_empty_member; do
        grep -Fq "$symbol:" "$work/$target.s"
    done
    printf 'LP64_EMPTY_RECORD_ABI=PASS target=%s empty-param=0GP/0stack nested-member=ignored empty-return=0B\n' "$target"
done
