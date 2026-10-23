#!/bin/sh
# apt-free fast Frontend + Core + RV64 Backend preflight.
# Full original RV64 glibc/GNU/QEMU comparison runs separately in T1.
set -eu
: "${MINIC_CC:?set MINIC_CC}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat >"$work/header.i" <<'SRC'
typedef _Float32 rv_float32;
typedef _Float64 rv_float64;
typedef _Float32x rv_float32x;
typedef _Float64x rv_float64x;
typedef _Float128 rv_float128;
extern _Float32 strtof32(const char *, char **);
extern _Float64 strtof64(const char *, char **);
extern _Float128 strtof128(const char *, char **);
int rv64_extended_header_sizes(void) {
    return sizeof(rv_float32) == 4U &&
           sizeof(rv_float64) == 8U &&
           sizeof(rv_float32x) == 8U &&
           sizeof(rv_float64x) == 16U &&
           sizeof(rv_float128) == 16U ? 42 : 1;
}
SRC
if ! "$MINIC_CC" --target=riscv64-linux -S "$work/header.i" \
  -o "$work/output.s" 2>"$work/minic.err"; then
  echo "RV64_FLOAT_DECL_NATIVE=FAIL stage=minic" >&2
  sed -n '1,25p' "$work/minic.err" >&2
  exit 1
fi
test -s "$work/output.s"
grep -q 'rv64_extended_header_sizes' "$work/output.s"
echo 'RV64_FLOAT_DECL_NATIVE=PASS five_distinct_extended_types=1 binary128_size_layout=1 glibc_declarations=3 cross_ABI_not_claimed=1'
