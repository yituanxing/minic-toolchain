#!/usr/bin/env bash
set -Eeuo pipefail

if (( $# != 1 )); then
  echo 'usage: probe_riscv_runtime_ra_leaf.sh /path/to/minic-cc' >&2
  exit 64
fi
repo=$(cd "$(dirname "$0")/../../.." && pwd)
compiler=$1
if [[ $compiler != /* ]]; then compiler=$(cd "$(dirname "$compiler")" && pwd)/$(basename "$compiler"); fi
stage=$(mktemp -d "$repo/build/riscv-runtime-ra-test.XXXXXXXX")
trap 'rm -r -- "$stage"' EXIT
source_file="$repo/tests/compiler/mir-migration/riscv_runtime_ra_leaf.c"

for march in rv64i rv64im rv64imafd; do
  asm="$stage/$march.s"
  "$compiler" --target=riscv64-linux "-march=$march" -mabi=lp64 \
    -S "$source_file" -o "$asm"
  test -s "$asm"
  for helper in __muldi3 __divdi3 __umoddi3 __adddf3 __ledf2 \
    __floatdidf __fixdfdi __extendsfdf2 __truncdfsf2; do
    if ! grep -Fq "call $helper" "$asm"; then
      echo "RV_RUNTIME_RA=FAIL missing helper=$helper march=$march" >&2
      exit 1
    fi
  done
  saves=$(grep -Ec '^[[:space:]]+sd[[:space:]]+(x1|ra),' "$asm" || true)
  restores=$(grep -Ec '^[[:space:]]+ld[[:space:]]+(x1|ra),' "$asm" || true)
  if (( saves < 9 || restores < 9 )); then
    echo "RV_RUNTIME_RA=FAIL missing save/restore march=$march saves=$saves restores=$restores" >&2
    exit 1
  fi
  if grep -Eq '^[[:space:]]*(mul|mulh|div|divu|rem|remu|fadd\.[sd]|fdiv\.[sd]|fmul\.[sd])[[:space:]]' "$asm"; then
    echo "RV_RUNTIME_RA=FAIL non-base-I instruction march=$march" >&2
    exit 1
  fi
done
echo 'RV_RUNTIME_RA=PASS RV64I=1 RV64IM=1 RV64IMAFD_LP64=1 implicit_helpers=9 saved_ra_functions=9 selected_isa=I'
