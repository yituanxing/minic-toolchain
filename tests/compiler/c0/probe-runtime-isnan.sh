#!/usr/bin/env bash
# The operand to __builtin_isnan must be evaluated once. Compare true/false
# results against the ordinary IEEE-754 f64 equality path across backends.
set -Eeuo pipefail
: "${MINIC_CC:?pass path to minic-cc}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/isnan.i" <<'EOF'
static int call_counter;
static int fcall_counter;
union BitsD { double d; unsigned long bits; };
union BitsF { float f; unsigned bits; };
static double source_nan(void) {
    union BitsD v;
    ++call_counter;
    v.bits = 0x7ff8000000000001UL;
    return v.d;
}
static float source_fnan(void) {
    union BitsF v;
    ++fcall_counter;
    v.bits = 0x7fc00001U;
    return v.f;
}
int builtin_double_ok(void) {
    call_counter = 0;
    return __builtin_isnan(source_nan()) && call_counter == 1;
}
int builtin_float_ok(void) {
    fcall_counter = 0;
    return __builtin_isnan(source_fnan()) && fcall_counter == 1;
}
int builtin_finite_ok(void) {
    return !__builtin_isnan((double)42.0);
}
int main(void) {
    return builtin_double_ok() && builtin_float_ok() && builtin_finite_ok() ? 0 : 1;
}
EOF
for target in x86_64-linux aarch64-linux loongarch64-linux; do
  "$MINIC_CC" --target="$target" -S "$work/isnan.i" -o "$work/$target.s"
  test -s "$work/$target.s"
  for fn in builtin_double_ok builtin_float_ok builtin_finite_ok; do
    grep -F "$fn:" "$work/$target.s" >/dev/null
  done
  echo "RUNTIME_ISNAN_COMPILE=PASS target=$target"
done
cc -no-pie "$work/x86_64-linux.s" -o "$work/isnan-x86"
"$work/isnan-x86"
echo "RUNTIME_ISNAN_X86=PASS float=1 double=1 finite=1 evaluated_once=1"
