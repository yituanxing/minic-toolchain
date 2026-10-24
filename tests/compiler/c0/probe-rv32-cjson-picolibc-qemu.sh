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
upstream="$vendor/test.c"
test "$(git hash-object "$upstream")" = 986fc6eb3e77fc29cca2980647c357bf1fe1b1fe
"$BARE_CC" "${target[@]}" "${libc[@]}" -O0 -ffreestanding -fno-builtin \
  -I"$vendor" -c "$upstream" -o "$work/upstream-test.o"
"$BARE_CC" "${target[@]}" "${libc[@]}" -O0 -ffreestanding -fno-builtin \
  -c "$bridge" -o "$work/linux-syscalls.o"
"$BARE_CC" "${target[@]}" "${libc[@]}" -c "$startup" -o "$work/start.o"

# Picolibc's embedded linker script maps text at 0x80000000 and the
# data segment separately at 0x80200000, with bare-metal TLS/stack
# assumptions. GNU/Linux QEMU-user must use the Linux ELF32 layout:
# link the *same* upstream Picolibc archives with GNU's Linux linker.
# Debian's Picolibc package installs per-multilib archives outside
# the GNU bare-metal compiler's default -print-file-name search paths.
# Resolve the explicitly RV32I/ILP32 authentic distribution artifacts,
# never an accidentally ABI-incompatible RV64 or glibc archive.
find_picolibc_archive() {
  local name="$1" file
  file=$(find /usr/lib/picolibc -type f \
      -path "*/rv32i/ilp32/$name" -print -quit 2>/dev/null || :)
  if [[ ! -f "$file" ]]; then
    echo "RV32_CJSON_LIBC=FAIL missing_Picolibc_RV32I_archive=$name" >&2
    dpkg -L picolibc-riscv64-unknown-elf | grep '/rv32i/' | head -n 16 >&2 || :
    return 1
  fi
  printf '%s\n' "$file"
}
picolibc=$(find_picolibc_archive libc.a)
picomath=$(find_picolibc_archive libm.a)
baregcc=$("$BARE_CC" "${target[@]}" -print-libgcc-file-name)
for lib in "$picolibc" "$picomath" "$baregcc"; do
  test -f "$lib" || { echo "RV32_CJSON_RUNTIME=FAIL missing_Picolibc_archive=$lib" >&2; exit 1; }
done
echo 'RV32_CJSON_LIBC=READY authentic_Picolibc=1 linux_ELF32_linker=1'

for workload in full_harness upstream_test; do
  case "$workload" in
    full_harness) program="$work/harness.o" ;;
    upstream_test) program="$work/upstream-test.o" ;;
  esac
  for mode in reference minic-gnu-as minic-minias; do
    case "$mode" in
      reference)      obj="$work/cjson-reference.o" ;;
      minic-gnu-as)   obj="$work/cjson-gnu-as.o" ;;
      minic-minias)   obj="$work/cjson-minias.o" ;;
    esac
    executable="$work/$workload-$mode.elf"
    riscv64-linux-gnu-ld -m elf32lriscv -static -e _start \
      -o "$executable" \
      "$work/start.o" "$work/linux-syscalls.o" "$program" \
      "$obj" --start-group "$picomath" "$picolibc" "$baregcc" --end-group
    riscv64-linux-gnu-readelf -h "$executable" | grep -q 'Class:.*ELF32'
    set +e
    timeout 60s qemu-riscv32 "$executable" \
        >"$work/$workload-$mode.out" 2>"$work/$workload-$mode.err"
    rc=$?
    set -e
    if (( rc != 0 )); then
      echo "RV32_CJSON_RUNTIME=FAIL workload=$workload mode=$mode exit=$rc" >&2
      head -n 15 "$work/$workload-$mode.err" >&2 || :
      timeout 10s qemu-riscv32 -strace "$executable" \
        2>&1 | tail -n 30 >&2 || :
      exit 1
    fi
    echo "RV32_CJSON_RUNTIME_MODE=PASS workload=$workload mode=$mode QEMU=1"
  done
  for mode in minic-gnu-as minic-minias; do
    if ! cmp -s "$work/$workload-reference.out" "$work/$workload-$mode.out" ||
       ! cmp -s "$work/$workload-reference.err" "$work/$workload-$mode.err"; then
      echo "RV32_CJSON_RUNTIME=FAIL workload=$workload mode=$mode stage=byte_exact" >&2
      diff -u "$work/$workload-reference.out" "$work/$workload-$mode.out" | head -n 55 >&2 || :
      exit 1
    fi
  done
done
echo 'RV32_CJSON_RUNTIME=PASS original_upstream=1 MiniC_all_functions=1 GNU_C_reference=1 Picolibc_libc=1 GNU_AS=1 MiniAS=1 ELF32=1 QEMU=1 full_harness=1 upstream_test_c=1 byte_exact=1'
