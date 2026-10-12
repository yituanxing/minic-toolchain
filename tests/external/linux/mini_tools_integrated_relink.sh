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
# Changing their executable paths forces re-archive and post-processing.
# Most previously compiled C objects are reused; Kbuild can legitimately
# regenerate a small bounded set of GCC->MiniAS objects after .cmd changes.
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
added_as=$(( after_as - before_as ))
# Real Linux Kbuild recompiles 88 extra *native* assembler invocations
# when AR and OBJCOPY .cmd identities change. This is ~4% of the original
# 2,197 input cohort, not a full recompile. Fail if it grows materially.
if test "$added_as" -lt 0 || test "$added_as" -gt 128; then
  echo "NATIVE_INTEGRATED_RELINK=FAIL reason=excessive_native_recompile baseline=$before_as current=$after_as additional=$added_as limit=128"
  exit 1
fi
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
printf 'NATIVE_INTEGRATED_RELINK=PASS initial_native_as=%s additional_native_as=%s ar_calls=%s objcopy_calls=%s elapsed_s=%s sha256=%s\n' \
  "$before_as" "$added_as" "$ar_calls" "$oc_calls" "$elapsed" "$(cut -d' ' -f1 "$stage/Image.sha256")"
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

# Stage III (explicit opt-in only): keep the *same* MiniAS .o / MiniAR .a /
# MiniObjcopy tree just certified by QEMU above. Replace GNU LD only at the
# three late final vmlinux/kallsyms boundaries; all other LD invocations
# remain GNU. No tool failure is silently delegated at native boundaries.
if [[ "${MINI_TOOLS_NATIVE_LD:-false}" == "true" ]]; then
  four="$work/native-four"
  mkdir -p "$four"
  test -x "$bins/minic-ld"
  cp "$stage/Image.sha256" "$four/triple-native.Image.sha256"
  : >"$four/minild.trace"
  cat >"$four/ld-final" <<'SH'
#!/usr/bin/env bash
set -Eeuo pipefail
: "${MINILD:?}"
: "${MINILD_TRACE:?}"
args=("$@")
output=
for ((i=0; i<${#args[@]}; ++i)); do
  if [[ "${args[i]}" == -o && $((i+1)) -lt ${#args[@]} ]]; then
    output="${args[i+1]}"
    break
  fi
done
case "$output" in
  .tmp_vmlinux.kallsyms1|.tmp_vmlinux.kallsyms2|vmlinux)
    { printf 'minild'; printf ' %q' "$@"; printf '\n'; } >>"$MINILD_TRACE"
    exec "$MINILD" "$@"
    ;;
  *)
    { printf 'gnu-ld'; printf ' %q' "$@"; printf '\n'; } >>"$MINILD_TRACE"
    exec /usr/bin/riscv64-linux-gnu-ld "$@"
    ;;
esac
SH
  chmod +x "$four/ld-final"
  export MINILD="$bins/minic-ld"
  export MINILD_TRACE="$four/minild.trace"
  before_four_as=$(grep -c '^pass ' "$MINIAS_AS_TRACE" || true)
  before_four_ar=$(wc -l <"$stage/miniar.trace")
  before_four_oc=$(wc -l <"$stage/miniobjcopy.trace")
  echo "NATIVE_FOUR_RELINK_BEGIN=PASS triple=QEMU-P0P1 gcc=GNU native_as=$before_four_as native_ar=$before_four_ar native_objcopy=$before_four_oc"
  start_four=$(date +%s)
  if ! make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
    CC="riscv64-linux-gnu-gcc -B$bindir/" \
    AS="$bindir/riscv64-linux-gnu-as" \
    AR="$stage/native-ar" \
    OBJCOPY="$stage/native-objcopy" \
    LD="$four/ld-final" \
    -j4 Image >"$four/kbuild.log" 2>&1; then
    echo "NATIVE_FOUR_RELINK=FAIL reason=kbuild"
    tail -n 140 "$four/kbuild.log" >&2
    tail -n 20 "$four/minild.trace" >&2
    exit 1
  fi
  elapsed_four=$(( $(date +%s) - start_four ))
  native_ld=$(grep -c '^minild ' "$four/minild.trace" || true)
  native_as_added=$(( $(grep -c '^pass ' "$MINIAS_AS_TRACE" || true) - before_four_as ))
  native_ar_added=$(( $(wc -l <"$stage/miniar.trace") - before_four_ar ))
  native_oc_added=$(( $(wc -l <"$stage/miniobjcopy.trace") - before_four_oc ))
  test "$native_ld" -eq 3 || {
    echo "NATIVE_FOUR_RELINK=FAIL native_ld=$native_ld expected=3" >&2
    exit 1
  }
  test -s "$out/vmlinux"
  test -s "$out/arch/riscv/boot/Image"
  sha256sum "$out/arch/riscv/boot/Image" >"$four/Image.sha256"
  printf 'NATIVE_FOUR_IMAGE=PASS native_as_added=%s native_ar_added=%s native_objcopy_added=%s native_ld=3 elapsed_s=%s sha256=%s\n' \
    "$native_as_added" "$native_ar_added" "$native_oc_added" "$elapsed_four" "$(cut -d' ' -f1 "$four/Image.sha256")"

  LINUX_IMAGE="$out/arch/riscv/boot/Image" \
  INITRAMFS="$work/runtime-initramfs.cpio.gz" \
  BUILD_DIR="$four/qemu-p0" \
  LINUX_RELEASE=6.6.143 \
  QEMU_SYSTEM_RISCV64=qemu-system-riscv64 \
  QEMU_TIMEOUT_SECONDS=90 \
    bash tests/external/linux/runtime_boot.sh
  LINUX_IMAGE="$out/arch/riscv/boot/Image" \
  INITRAMFS="$work/runtime-initramfs.cpio.gz" \
  BUILD_DIR="$four/qemu-p1" \
  LINUX_RELEASE=6.6.143 \
  LINUX_RUNTIME_PROFILE=p1 \
  QEMU_SYSTEM_RISCV64=qemu-system-riscv64 \
  QEMU_TIMEOUT_SECONDS=90 \
    bash tests/external/linux/runtime_boot.sh
  python3 tests/external/linux/runtime_v2_log_check.py \
    --profile p1 --log "$four/qemu-p1/rdinit-init.log"
  echo "NATIVE_FOUR_LINUX_RUNTIME=PASS components=MiniAS,MiniAR,MiniLD-final,MiniObjcopy gcc=GNU p0=2/2 p1=12/12"
fi
