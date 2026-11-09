#!/bin/sh
# Preserve the full-width pointer-index arithmetic while replacing exact
# power-of-two scale multiplication with a LoongArch64 left shift.
set -eu
: "${MINIC_CC:?MINIC_CC path required}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
source="$root/tests/compiler/c0/loongarch64_pointer_stride_power_of_two.c"

"${CC:-cc}" -E -P -x c "$source" -o "$work/fixture.i"
"$MINIC_CC" --target=loongarch64-linux -S "$work/fixture.i" \
    -o "$work/fixture.loong.s"

awk '
    /^stride[a-z0-9_]*:$/ { name = substr($0, 1, length($0)-1); seen[name]++; next }
    /^\.size stride[a-z0-9_]*,/ { name = ""; next }
    name != "" {
        if ($0 ~ /^  li\.d \$t2, /) li[name]++
        if ($0 == "  mul.d $t1, $t1, $t2") mul[name]++
        if ($0 ~ /^  slli\.d \$t1, \$t1, /) shift[name]++
        if ($0 == "  sub.d $t0, $t0, $t1") subtract[name]++
    }
    END {
        expected["stride1"] = 0; expected["stride2"] = 1
        expected["stride4"] = 1; expected["stride8"] = 1
        expected["stride16"] = 1; expected["stride_sub8"] = 1
        expected["stride_neg8"] = 1; expected["stride_unsigned8"] = 1
        for (fn in expected) {
            if (seen[fn] != 1 || shift[fn] != expected[fn] ||
                li[fn] != 0 || mul[fn] != 0) {
                printf "LOONG_POINTER_STRIDE=FAIL fn=%s seen=%d shift=%d li=%d mul=%d\n", fn, seen[fn], shift[fn], li[fn], mul[fn] > "/dev/stderr"
                exit 1
            }
        }
        if (subtract["stride_sub8"] != 1 || seen["stride3"] != 1 ||
            seen["stride24"] != 1 || li["stride3"] != 1 ||
            li["stride24"] != 1 || mul["stride3"] != 1 ||
            mul["stride24"] != 1 || shift["stride3"] != 0 ||
            shift["stride24"] != 0) {
            print "LOONG_POINTER_STRIDE=FAIL fallback/subtract" > "/dev/stderr"
            exit 1
        }
    }
' "$work/fixture.loong.s"

"${CC:-cc}" -O2 "$source" -o "$work/gnu-runtime"
"$MINIC_CC" --target=x86_64-linux -S "$work/fixture.i" \
    -o "$work/minic-x86.s"
"${CC:-cc}" -no-pie "$work/minic-x86.s" -o "$work/minic-runtime"
"$work/gnu-runtime"
"$work/minic-runtime"
echo 'LOONG_POINTER_STRIDE=PASS pow2=1,2,4,8,16 signed/unsigned/subtract=PASS nonpow2=3,24 retain_mul=PASS GNU/MiniC runtime=PASS'
