#!/bin/sh
# A GNU↔MiniC runtime oracle for arbitrary-precision mixed signedness.
set -eu
: "${MINIC_CC:?set MINIC_CC to the MiniC C driver}"
cc=${CC:-cc}
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
w=$(mktemp -d)
trap 'rm -rf "$w"' EXIT HUP INT TERM
source="$root/tests/compiler/c0/mixed-overflow-u64-i32.c"
harness="$root/tests/compiler/c0/mixed-overflow-u64-i32-harness.c"
"$cc" -std=c11 -O2 -c "$source" -o "$w/gnu.o"
"$cc" -E -P -std=c11 -DMIXED_FORWARD=minic_mixed_forward \
    -DMIXED_REVERSE=minic_mixed_reverse "$source" -o "$w/mini.i"
for target in x86_64 aarch64 loongarch64; do
    "$MINIC_CC" --target="$target-linux" -S "$w/mini.i" -o "$w/$target.s"
    test -s "$w/$target.s"
done
cp "$w/x86_64.s" "$w/mini.s"
"$cc" -c "$w/mini.s" -o "$w/mini.o"
"$cc" -std=c11 -O2 "$harness" "$w/gnu.o" "$w/mini.o" -o "$w/oracle"
set +e
"$w/oracle"
result=$?
set -e
if [ "$result" -ne 42 ]; then
    echo "MIXED_OVERFLOW=FAIL result=$result" >&2
    exit 1
fi
"$cc" -E -P -std=c11 -DMIXED_UNSUPPORTED=1 "$source" -o "$w/unsupported.i"
for target in x86_64 aarch64 loongarch64; do
    if "$MINIC_CC" --target="$target-linux" -S "$w/unsupported.i" \
        -o "$w/unsupported-$target.s" >"$w/rejected.out" 2>"$w/rejected.err"; then
        echo "MIXED_OVERFLOW=FAIL unsound signed64 operand admitted target=$target" >&2
        exit 1
    fi
done
echo 'MIXED_OVERFLOW=PASS targets=3 10000x2 GNU_native_x86 differential signed32+unsigned64→signed32 fail_closed=3'
