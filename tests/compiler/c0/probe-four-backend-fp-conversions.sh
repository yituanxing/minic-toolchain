#!/bin/sh
# Original C unsigned 64-bit FP conversions and single-precision literals.
# Compare target GNU and MiniC runtime outputs and return status under QEMU.
set -eu
: "${MINIC_CC:?set MINIC_CC to minic-cc}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
# A .i file is already preprocessed: the MiniC raw-C path intentionally
# does not rerun C comment stripping. Keep oracle explanation in shell code.
# Exercise binary32/64 arithmetic, IEEE unordered comparisons and numeric casts.
cat >"$work/conversions.i" <<'SRC'
static unsigned long d_to_u64(double x) { return (unsigned long)x; }
static float one_half(void) { return 1.5f; }
static float minus_zero(void) { return -0.0f; }
static double half(void) { return 0.5; }
static int fp_machine_family(void) {
    volatile float f = 1.25f, g = 2.5f;
    volatile double a = 5.5, b = 1.25;
    volatile double zero = 0.0;
    double unordered = zero / zero;
    double promoted = f;
    float rounded = (float)a;
    if (f + g != 3.75f || f - g != -1.25f ||
        f * g != 3.125f || g / f != 2.0f) return 21;
    if (a + b != 6.75 || a - b != 4.25 ||
        a * b != 6.875 || a / b < 4.39 || a / b > 4.41) return 22;
    if (a < b || a <= b || !(a == a) || !(b < a) || !(b <= a))
        return 23;
    if (unordered == unordered || unordered < a || unordered <= a ||
        a < unordered || a <= unordered) return 24;
    if (promoted != 1.25 || rounded != 5.5f ||
        (double)(float)1.5 != 1.5) return 25;
    if ((double)(int)-42 != -42.0 ||
        (int)(double)-42.75 != -42) return 26;
    return 0;
}
int main(void) {
    volatile double a = 9223372036854775808.0;
    volatile double b = 9223372036854779904.0;
    volatile double c = 18446744073709547520.0;
    volatile double d = 42.75;
    volatile double e = 0.0;
    if (d_to_u64(a) != 9223372036854775808UL) return 11;
    if (d_to_u64(b) != 9223372036854779904UL) return 12;
    if (d_to_u64(c) != 18446744073709547520UL) return 13;
    if (d_to_u64(d) != 42UL || d_to_u64(e) != 0UL) return 14;
    if (one_half() != 1.5f || half() != 0.5) return 15;
    if (!(minus_zero() == 0.0f)) return 16;
    { int fp_error = fp_machine_family(); if (fp_error != 0) return fp_error; }
    return 42;
}
SRC
for target in aarch64 loongarch64; do
    case "$target" in
        aarch64) gcc=aarch64-linux-gnu-gcc; qemu=qemu-aarch64 ;;
        loongarch64) gcc=loongarch64-linux-gnu-gcc-13; qemu=qemu-loongarch64 ;;
    esac
    "$gcc" -static -x c "$work/conversions.i" -o "$work/$target.gnu"
    if ! MINIC_M1_TRACE=1 "$MINIC_CC" --target="$target-linux" -S \
            "$work/conversions.i" -o "$work/$target.s" \
            >"$work/$target.compile.out" 2>"$work/$target.compile.err"; then
        echo "FP_CONVERSIONS=FAIL target=$target stage=MiniC" >&2
        sed -n '1,25p' "$work/$target.compile.err" >&2
        exit 1
    fi
    "$gcc" -static -x assembler "$work/$target.s" -o "$work/$target.mini"
    for cc in gnu mini; do
        set +e
        timeout 45s "$qemu" "$work/$target.$cc" >"$work/$target.$cc.out" 2>"$work/$target.$cc.err"
        status=$?
        set -e
        if [ "$status" -ne 42 ]; then
            echo "FP_CONVERSIONS=FAIL target=$target compiler=$cc stage=QEMU status=$status" >&2
            sed -n '1,20p' "$work/$target.$cc.err" >&2
            exit 1
        fi
    done
    cmp "$work/$target.gnu.out" "$work/$target.mini.out"
    cmp "$work/$target.gnu.err" "$work/$target.mini.err"
    echo "FP_CONVERSIONS=PASS target=$target unsigned64=1 below_and_above_2^63=1 float_literal=1 ieee_arith=1 unordered_compare=1 GNU_QEMU_diff=1"
done
echo "FP_CONVERSIONS_ACCEPT=PASS targets=2"
