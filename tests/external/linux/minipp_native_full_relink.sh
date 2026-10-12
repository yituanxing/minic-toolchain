#!/usr/bin/env bash
# Stage IV: ALL actual kernel C objects from full Kbuild .o.cmd.
# Reuse previously certified 4-native Linux object graph; no full recompile.
set -Eeuo pipefail
work=${MINI_TOOLS_WORK:?}
bins=${MINI_TOOLS_BIN:?}
src="$work/linux-6.6.143"
out="$work/out"
stage="$work/native-pp-full"
native_stage="$work/native-integration"
four="$work/native-four"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mkdir -p "$stage"
test -s "$four/Image.sha256"
test -s "$out/vmlinux"
test -s "$out/arch/riscv/boot/Image"
test -s "$work/runtime-initramfs.cpio.gz"
test -x "$bins/minic-cpp"
test -x "$four/ld-final"
test -x "$native_stage/native-ar"
test -x "$native_stage/native-objcopy"
test -x "$work/as-override/riscv64-linux-gnu-as"
: "${MINIAS_AS_TRACE:?}"
: "${MINILD:?}"
: "${MINIAR:?}"
: "${MINIOBJCOPY:?}"
export MINIAS_GNU_ROOT="$work/golden-as"
export MINI_TOOLS_BIN="$bins"
echo "MINIPP_NATIVE_FULL_BEGIN=PASS gcc=GNU cc1_objects=all_kbuild native_pp=MiniPP as=MiniAS ar=MiniAR ld=MiniLD_final"

# The Python driver discovers *all* actual cross-GCC C-object commands,
# caches context-specific GNU predefined macros, exact-compares every .i,
# feeds MiniPP .i to GNU cc1, and demands 1:1 production evidence.
# Preserve its failure pool even if the full cohort cannot yet compile.
python3 "$root/tests/preprocessor/run_linux_native_full_production.py" \
  --out "$out" \
  --src "$src" \
  --stage "$stage" \
  --wrapper "$root/tests/preprocessor/linux_kbuild_pp_cc.sh" \
  --predefines-script "$root/tests/preprocessor/build_reference_predefines.sh" \
  --jobs "${MINIPP_NATIVE_FULL_JOBS:-6}"

# Every object must survive subsequent Kbuild without being overwritten.
before_as=$(grep -c '^pass ' "$MINIAS_AS_TRACE" || true)
: >"$stage/minild.trace"
export MINILD_TRACE="$stage/minild.trace"
start=$(date +%s)
if ! make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
  CC="riscv64-linux-gnu-gcc -B$work/as-override/" \
  AS="$work/as-override/riscv64-linux-gnu-as" \
  AR="$native_stage/native-ar" \
  OBJCOPY="$native_stage/native-objcopy" \
  LD="$four/ld-final" \
  -j4 Image >"$stage/kbuild.log" 2>&1; then
  echo "MINIPP_NATIVE_FULL_RELINK=FAIL reason=kbuild" >&2
  tail -n 100 "$stage/kbuild.log" >&2
  exit 1
fi
python3 - "$out" "$stage/production-object-identity.json" <<'PY'
import hashlib, json, pathlib, sys
out=pathlib.Path(sys.argv[1])
before=json.loads(pathlib.Path(sys.argv[2]).read_text())
for name,want in before.items():
    path=out/name
    st=path.stat()
    observed={"sha256":hashlib.sha256(path.read_bytes()).hexdigest(),
              "size":st.st_size, "mtime_ns":st.st_mtime_ns}
    if observed != want:
        raise SystemExit(f"MINIPP_NATIVE_FULL_IDENTITY=FAIL reverted_or_rebuilt={name} "
                         f"before={want} after={observed}")
print(f"MINIPP_NATIVE_FULL_IDENTITY=PASS objects={len(before)} no_gcc_pp_reversion=1")
PY
elapsed=$(( $(date +%s) - start ))
native_ld=$(grep -c '^minild ' "$stage/minild.trace" || true)
test "$native_ld" -eq 3 || {
  echo "MINIPP_NATIVE_FULL_RELINK=FAIL reason=expected_three_native_final_links actual=$native_ld" >&2
  exit 1
}
test -s "$out/arch/riscv/boot/Image"
sha256sum "$out/arch/riscv/boot/Image" >"$stage/Image.sha256"
objects=$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["expected"])' "$stage/summary.json")
printf 'MINIPP_NATIVE_FULL_IMAGE=PASS real_tus=%s native_final_ld=3 native_as_added=%s elapsed_s=%s sha256=%s\n' \
  "$objects" "$(( $(grep -c '^pass ' "$MINIAS_AS_TRACE" || true) - before_as ))" \
  "$elapsed" "$(cut -d' ' -f1 "$stage/Image.sha256")"
LINUX_IMAGE="$out/arch/riscv/boot/Image" \
INITRAMFS="$work/runtime-initramfs.cpio.gz" \
BUILD_DIR="$stage/qemu-p0" \
LINUX_RELEASE=6.6.143 \
QEMU_SYSTEM_RISCV64=qemu-system-riscv64 \
QEMU_TIMEOUT_SECONDS=90 \
  bash "$root/tests/external/linux/runtime_boot.sh"
LINUX_IMAGE="$out/arch/riscv/boot/Image" \
INITRAMFS="$work/runtime-initramfs.cpio.gz" \
BUILD_DIR="$stage/qemu-p1" \
LINUX_RELEASE=6.6.143 \
LINUX_RUNTIME_PROFILE=p1 \
QEMU_SYSTEM_RISCV64=qemu-system-riscv64 \
QEMU_TIMEOUT_SECONDS=90 \
  bash "$root/tests/external/linux/runtime_boot.sh"
python3 "$root/tests/external/linux/runtime_v2_log_check.py" \
  --profile p1 --log "$stage/qemu-p1/rdinit-init.log"
printf 'MINIPP_NATIVE_FULL_LINUX_RUNTIME=PASS pp=MiniPP full_real_tus=%s gcc=GNU as=MiniAS ar=MiniAR ld=MiniLD_final objcopy=MiniObjcopy p0=2/2 p1=12/12\n' "$objects"
