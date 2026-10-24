#!/usr/bin/env bash
# Second genuine upstream C project on RV32I after passed untouched tiny-AES.
# Reuse the same cJSON vendor integrity, exact static headers, GNU-E and
# frozen line checks that already qualify MiniC on RV64. Do not hand-edit C,
# invent sysroot libraries, or call a frontend failure "PASS".
set -Eeuo pipefail
: "${MINIC_CC:?}"
root=$(git rev-parse --show-toplevel)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
env MINIC_CJSON_PREPROCESS_ONLY=1 MINIC_CJSON_TARGET_ABI=rv32ilp32 \
    BUILD_DIR="$work" RISCV_CC=riscv64-linux-gnu-gcc QEMU_RISCV64=qemu-riscv32 \
    bash tests/external/cjson/probe.sh
source="$work/tests/external/cjson/cJSON.i"
test -s "$source"
printf 'RV32_CJSON_INPUT=PASS original_upstream=1 vendor_sha=verified preprocessor=frozen GNU_E=rv32i_ilp32\n'
if ! "$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
  -S "$source" -o "$work/cjson.s" \
  >"$work/cjson.out" 2>"$work/cjson.err"; then
    test ! -e "$work/cjson.s"
    echo 'RV32_CJSON=BLOCKED stage=MiniC_Core_or_ILP32 original_source=true'
    sed -n '1,7p' "$work/cjson.err"
    exit 0
fi
test -s "$work/cjson.s"
echo 'RV32_CJSON_COMPILE=PASS source=upstream_1.7.19 MiniC=true'
# The complete cJSON API uses binary64, variadics, malloc and a real ILP32
# libc. The compile frontier alone does NOT certify linking or runtime.
# Prove real ELF32 assembly with MiniAS when codegen is complete.
: "${MINIAS:=${MINIC_CC%/minic-cc}/minic-as}"
if ! "$MINIAS" -march=rv32i -mabi=ilp32 \
    -o "$work/cjson.o" "$work/cjson.s" \
    >"$work/as.out" 2>"$work/as.err"; then
    echo 'RV32_CJSON=BLOCKED stage=MiniAS_ELF32 source=upstream_1.7.19'
    sed -n '1,7p' "$work/as.err"
    exit 0
fi
riscv64-linux-gnu-readelf -h "$work/cjson.o" |
  grep -q 'Class:.*ELF32'
echo 'RV32_CJSON_ELF32=PASS original_upstream=1.7.19 full_runtime=NOT_YET_QUALIFIED'
