#!/usr/bin/env bash
# Stage II: reuse SAME GNU-GCC + native-MiniAS-built Linux object tree.
# Change only Kbuild AR and OBJCOPY command identities and re-link with GNU ld.
# Never hide native errors behind GNU fallback.
set -Eeuo pipefail
work=${MINI_TOOLS_WORK:?MINI_TOOLS_WORK is required}
bins=${MINI_TOOLS_BIN:?MINI_TOOLS_BIN is required}
src="$work/linux-6.6.143"
out="$work/out"
bindir="$work/as-override"
stage="$work/native-integration"
mkdir -p "$stage"
test -s "$out/arch/riscv/boot/Image"
test -x "$bins/minic-as"
test -x "$bins/minic-ar"
test -x "$bins/minic-objcopy"
test -x "$bindir/riscv64-linux-gnu-as"
: "${MINIAS_AS_TRACE:?}"
: "${MINIAS:?}"
: "${REAL_AS:?}"
: >"$stage/miniar.trace"
: >"$stage/miniobjcopy.trace"
cat >"$stage/native-ar" <<'SH'
#!/usr/bin/env bash
set -Eeuo pipefail
: "${MINIAR:?}"
: "${MINIAR_TRACE:?}"
{
  printf 'ar'
  printf ' %q' "$@"
  printf '\n'
} >>"$MINIAR_TRACE"
exec "$MINIAR" "$@"
SH
cat >"$stage/native-objcopy" <<'SH'
#!/usr/bin/env bash
set -Eeuo pipefail
: "${MINIOBJCOPY:?}"
: "${MINIOBJCOPY_TRACE:?}"
{
  printf 'objcopy'
  printf ' %q' "$@"
  printf '\n'
} >>"$MINIOBJCOPY_TRACE"
exec "$MINIOBJCOPY" "$@"
SH
chmod +x "$stage/native-ar" "$stage/native-objcopy"
export MINIAR="$bins/minic-ar"
export MINIAR_TRACE="$stage/miniar.trace"
export MINIOBJCOPY="$bins/minic-objcopy"
export MINIOBJCOPY_TRACE="$stage/miniobjcopy.trace"
before_as=$(grep -c '^pass ' "$MINIAS_AS_TRACE" || true)
start=$(date +%s)
echo "NATIVE_INTEGRATED_RELINK_BEGIN=PASS as_production=$before_as gcc=GNU ld=GNU"
# Kbuild remembers the full AR/OBJCOPY commands in .cmd dependencies.
# Changing their executable paths forces re-archive and post-processing,
# but retains all normal C compilation and assembly outputs from stage I.
if ! make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
  CC="riscv64-linux-gnu-gcc -B$bindir/" \
  AS="$bindir/riscv64-linux-gnu-as" \
  AR="$stage/native-ar" \
  OBJCOPY="$stage/native-objcopy" \
  LD=riscv64-linux-gnu-ld \
  -j4 Image >"$stage/kbuild.log" 2>&1; then
  echo "NATIVE_INTEGRATED_RELINK=FAIL reason=kbuild"
  tail -n 100 "$stage/kbuild.log"
  # On a genuine cross-tool ELF incompatibility capture the actual
  # MiniAS ET_REL input and GNU-as same-source oracle, not a screenshot or
  # hypothetical pseudo reproducer. The runner is still in this object tree.
  failures="$stage/failures"
  mkdir -p "$failures"
  for stem in efi-stub-helper efi-stub fdt; do
    obj="drivers/firmware/efi/libstub/$stem.o"
    native="$out/$obj"
    gnu="$work/golden-as/$obj"
    if test -s "$native"; then
      cp "$native" "$failures/$stem.mini.o"
      readelf -Wh "$native" >"$failures/$stem.mini.header.txt" || true
      readelf -WS "$native" >"$failures/$stem.mini.sections.txt" || true
      readelf -Wr "$native" >"$failures/$stem.mini.relocs.txt" || true
    fi
    if test -s "$gnu"; then
      cp "$gnu" "$failures/$stem.gnu.o"
      readelf -WS "$gnu" >"$failures/$stem.gnu.sections.txt" || true
      readelf -Wr "$gnu" >"$failures/$stem.gnu.relocs.txt" || true
    fi
    if test -s "$work/golden-as/${obj%.o}.s"; then
      cp "$work/golden-as/${obj%.o}.s" "$failures/$stem.s"
    fi
  done
  echo "NATIVE_INTEGRATED_FRONTIER=CAPTURED native_elf=$(find "$failures" -name '*.mini.o' | wc -l) gnu_elf=$(find "$failures" -name '*.gnu.o' | wc -l)"
  exit 1
fi
elapsed=$(( $(date +%s) - start ))
after_as=$(grep -c '^pass ' "$MINIAS_AS_TRACE" || true)
ar_calls=$(grep -c '^ar ' "$stage/miniar.trace" || true)
oc_calls=$(grep -c '^objcopy ' "$stage/miniobjcopy.trace" || true)
test "$after_as" -eq "$before_as" || {
  echo "NATIVE_INTEGRATED_RELINK=FAIL reason=assembler_outputs_rebuilt before=$before_as after=$after_as"
  exit 1
}
test "$ar_calls" -gt 50 || {
  echo "NATIVE_INTEGRATED_RELINK=FAIL reason=too_few_native_archiver_calls count=$ar_calls"
  exit 1
}
test "$oc_calls" -gt 0 || {
  echo "NATIVE_INTEGRATED_RELINK=FAIL reason=no_native_objcopy_calls"
  exit 1
}
image="$out/arch/riscv/boot/Image"
test -s "$image"
sha256sum "$image" >"$stage/Image.sha256"
printf 'NATIVE_INTEGRATED_RELINK=PASS as_reused=%s ar_calls=%s objcopy_calls=%s elapsed_s=%s sha256=%s\n' \
  "$after_as" "$ar_calls" "$oc_calls" "$elapsed" "$(cut -d' ' -f1 "$stage/Image.sha256")"
LINUX_IMAGE="$image" \
INITRAMFS="$work/runtime-initramfs.cpio.gz" \
BUILD_DIR="$stage/qemu-p0" \
LINUX_RELEASE=6.6.143 \
QEMU_SYSTEM_RISCV64=qemu-system-riscv64 \
QEMU_TIMEOUT_SECONDS=90 \
  bash tests/external/linux/runtime_boot.sh
LINUX_IMAGE="$image" \
INITRAMFS="$work/runtime-initramfs.cpio.gz" \
BUILD_DIR="$stage/qemu-p1" \
LINUX_RELEASE=6.6.143 \
LINUX_RUNTIME_PROFILE=p1 \
QEMU_SYSTEM_RISCV64=qemu-system-riscv64 \
QEMU_TIMEOUT_SECONDS=90 \
  bash tests/external/linux/runtime_boot.sh
python3 tests/external/linux/runtime_v2_log_check.py \
  --profile p1 --log "$stage/qemu-p1/rdinit-init.log"
echo "NATIVE_INTEGRATED_LINUX_RUNTIME=PASS components=MiniAS,MiniAR,MiniObjcopy gcc=GNU ld=GNU p0=2/2 p1=12/12"
