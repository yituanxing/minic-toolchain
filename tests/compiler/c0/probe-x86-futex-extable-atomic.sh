#!/bin/sh
set -eu
: "${MINIC_CC:?set MINIC_CC to the built minic-cc}"
host_cc=${HOST_CC:-cc}
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
source="$root/tests/compiler/c0/x86_futex_extable_atomic.c"

"$host_cc" -E -P -x c "$source" -o "$work/source.i"
"$MINIC_CC" --target=x86_64-linux -S "$work/source.i" -o "$work/minic.s"
"$host_cc" -c -x assembler "$work/minic.s" -o "$work/minic.o"
"$host_cc" -no-pie -x assembler "$work/minic.s" -o "$work/minic.bin"
"$host_cc" -std=gnu11 -O0 -no-pie "$source" -o "$work/gnu.bin"
for name in minic gnu; do
    set +e
    "$work/$name.bin"
    status=$?
    set -e
    if [ "$status" -ne 42 ]; then
        echo "FAIL: $name GNU futex atomic runtime status=$status" >&2
        exit 1
    fi
done

readelf -SW "$work/minic.o" | grep -F '__ex_table' >/dev/null
readelf -rW "$work/minic.o" | grep -F '.rela__ex_table' >/dev/null
readelf -x __ex_table "$work/minic.o" | grep -F '1108f2ff' >/dev/null
if [ "$(grep -cF '.long (17 | ((-14) << 16)) + (8 << 8)' "$work/minic.s")" -ne 9 ]; then
    echo 'FAIL: futex GNU 9 exception sites not preserved' >&2
    exit 1
fi
echo 'PASS: x86 futex 6 atomic variants, 9 fault sites, EFAULT register 8, GNU/MiniC native exit=42'

# A new instruction with the same extable macro must remain rejected.
sed 's/xaddl %0, %2/xaddq %0, %2/' "$work/source.i" >"$work/bad.i"
if "$MINIC_CC" --target=x86_64-linux -S "$work/bad.i" \
    -o "$work/bad.s" >"$work/bad.stdout" 2>"$work/bad.stderr"; then
    echo 'FAIL: xaddq with 32-bit futex operands was accepted' >&2
    exit 1
fi
echo 'PASS: unknown futex GNU instruction suffix fail-closed'
