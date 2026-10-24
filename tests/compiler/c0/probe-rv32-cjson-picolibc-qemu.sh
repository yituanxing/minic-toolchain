#!/usr/bin/env bash
# Unchanged cJSON 1.7.19: first complete RV32I/ILP32 QEMU acceptance.
# External pinned Picolibc supplies genuine malloc, strtod, sprintf,
# sscanf, fabs, string functions; the only shim is Linux brk/exit syscalls.
# No GNU fallback for any cJSON C function in the MiniC executable.
set -Eeuo pipefail
: "${MINIC_CC:?}"
: "${MINIAS:=${MINIC_CC%/minic-cc}/minic-as}"
: "${BARE_CC:=riscv64-unknown-elf-gcc}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
command -v "$BARE_CC"
command -v "$RISCV_CC"
command -v qemu-riscv32
test -x "$MINIAS"
root=$(git rev-parse --show-toplevel)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# Preserve the exact existing source hash, minimal-header and line-identity
# gates used by RV64: the upstream source and compiler input are untouched.
MINIC_CJSON_PREPROCESS_ONLY=1 MINIC_CJSON_TARGET_ABI=rv32ilp32 \
  BUILD_DIR="$work" RISCV_CC="$RISCV_CC" QEMU_RISCV64=qemu-riscv32 \
  bash "$root/tests/external/cjson/probe.sh"
source="$work/tests/external/cjson/cJSON.i"
test -s "$source"
target=( -march=rv32i -mabi=ilp32 )
libc=( -specs=picolibc.specs )
fixture="$root/tests/external/cjson/runtime_harness.c"
vendor="$root/tests/vendor/cjson/upstream"
startup="$root/tests/compiler/c0/rv32-linux-picolibc-start.S"
bridge="$root/tests/compiler/c0/rv32-linux-picolibc-syscalls.c"

"$MINIC_CC" --target=riscv32-linux "${target[@]}" -S "$source" -o "$work/cjson.s"
# Two assembler implementations independently consume the same MiniC .s.
"$RISCV_CC" "${target[@]}" -c "$work/cjson.s" -o "$work/cjson-gnu-as.o"
"$MINIAS" "${target[@]}" -o "$work/cjson-minias.o" "$work/cjson.s"
"$BARE_CC" "${target[@]}" "${libc[@]}" -O0 -ffreestanding -fno-builtin \
  -x c -c "$source" -o "$work/cjson-reference.o"
"$BARE_CC" "${target[@]}" "${libc[@]}" -O0 -ffreestanding -fno-builtin \
  -I"$vendor" -c "$fixture" -o "$work/harness.o"
"$BARE_CC" "${target[@]}" "${libc[@]}" -O0 -ffreestanding -fno-builtin \
  -c "$bridge" -o "$work/linux-syscalls.o"
"$BARE_CC" "${target[@]}" "${libc[@]}" -c "$startup" -o "$work/start.o"

for mode in reference minic-gnu-as minic-minias; do
  case "$mode" in
    reference)      obj="$work/cjson-reference.o" ;;
    minic-gnu-as)   obj="$work/cjson-gnu-as.o" ;;
    minic-minias)   obj="$work/cjson-minias.o" ;;
  esac
  # Picolibc is an upstream ABI-matched C library, not a collection of
  # test-only stubs or host LP64 glibc fallbacks.
  "$BARE_CC" "${target[@]}" "${libc[@]}" -nostartfiles \
    -Wl,-e,_start -Wl,--no-undefined \
    "$work/start.o" "$work/linux-syscalls.o" "$work/harness.o" \
    "$obj" -lm -o "$work/$mode.elf"
  riscv64-linux-gnu-readelf -h "$work/$mode.elf" |
    grep -q 'Class:.*ELF32'
  if ! timeout 60s qemu-riscv32 "$work/$mode.elf" \
      >"$work/$mode.out" 2>"$work/$mode.err"; then
    rc=$?
    echo "RV32_CJSON_RUNTIME=FAIL mode=$mode stage=qemu" >&2
    head -n 15 "$work/$mode.err" >&2 || :
    exit 1
  fi
  echo "RV32_CJSON_RUNTIME_MODE=PASS mode=$mode QEMU=1 upstream_cJSON_1_7_19=1"
done
cmp -s "$work/reference.out" "$work/minic-gnu-as.out"
cmp -s "$work/reference.out" "$work/minic-minias.out"
cmp -s "$work/reference.err" "$work/minic-gnu-as.err"
cmp -s "$work/reference.err" "$work/minic-minias.err"
echo 'RV32_CJSON_RUNTIME=PASS original_upstream=1 MiniC_all_functions=1 GNU_C_reference=1 Picolibc_libc=1 GNU_AS=1 MiniAS=1 ELF32=1 QEMU=1 full_harness=1'
