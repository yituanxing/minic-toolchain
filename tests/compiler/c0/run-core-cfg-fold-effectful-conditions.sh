#!/bin/sh
# Compare observable CFG-condition effects against native GNU C semantics.
set -eu
: "${MINIC:?Set MINIC to the absolute path of minic-cc}"
: "${CC:=cc}"

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${BUILD_DIR:-"$root/build/core-cfg-fold-effectful-conditions"}
mkdir -p "$work"

for test in core_cfg_fold_effectful_conditions core_cfg_fold_effectful_boolean_range; do
    source_file="$root/tests/compiler/c0/$test.c"
    "$CC" -std=gnu11 -O2 "$source_file" -o "$work/$test.gnu"
    "$CC" -std=gnu11 -E -P "$source_file" -o "$work/$test.i"
    "$MINIC" --target=x86_64-linux -S "$work/$test.i" -o "$work/$test.s"
    "$CC" -no-pie "$work/$test.s" -o "$work/$test.minic"
    "$work/$test.gnu"
    "$work/$test.minic"
    printf 'PASS %s (GNU/MiniC)\n' "$test"
done
