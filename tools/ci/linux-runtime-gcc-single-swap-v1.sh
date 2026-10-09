#!/usr/bin/env bash
# One deliberately bounded GNU-GCC baseline -> MiniC single-object -> GNU Image/QEMU experiment.
set -Eeuo pipefail
if [[ $# -ne 5 ]]; then
  echo "usage: $0 <linux-src> <gcc-out> <minic-exe> <baseline-provenance> <evidence-dir>" >&2
  exit 64
fi
src=$(realpath "$1")
out=$(realpath "$2")
minic=$(realpath "$3")
provenance=$(realpath "$4")
ev=$(realpath -m "$5")
repo=$(cd "$(dirname "$0")/../.." && pwd)
wrapper="$repo/tests/external/linux/stage2_kbuild_cc.sh"
target=lib/idr.o
stem=lib/idr
expected_cfg=$(sed -n 's/^config_sha256=//p' "$provenance/gcc-baseline.txt")
actual_cfg=$(sha256sum "$out/.config" | cut -d' ' -f1)
[[ -n "$expected_cfg" && "$expected_cfg" == "$actual_cfg" ]] || {
  echo "GNU_SINGLE=ERROR config mismatch expected=$expected_cfg actual=$actual_cfg" >&2; exit 2;
}
test -s "$out/$target"
test -s "$out/lib/.idr.o.cmd"
test -s "$out/arch/riscv/boot/Image"
test -x "$minic"
test -d "$src"
mkdir -p "$ev"
expected_object=$(awk -v t="$target" '$2 == t { print $1; exit }' "$provenance/gcc-objects.sha256")
gcc_sha=$(sha256sum "$out/$target" | cut -d' ' -f1)
[[ -n "$expected_object" && "$gcc_sha" == "$expected_object" ]] || {
  echo "GNU_SINGLE=ERROR gcc original object not certified" >&2; exit 3;
}
echo "GNU_SINGLE_BASELINE=PASS target=$target config=$actual_cfg gcc_sha=$gcc_sha"
cp -a "$out/$target" "$ev/gcc.o"
cp -a "$out/lib/.idr.o.cmd" "$ev/gcc.o.cmd"
baseline_image_sha=$(sha256sum "$out/arch/riscv/boot/Image" | cut -d' ' -f1)
printf 'baseline_image_sha256=%s\n' "$baseline_image_sha" >"$ev/identity.txt"

# Need GCC's preprocessed .i from the real Kbuild command. It is not safe to
# invent flags; the repository's real wrapper preserves the exact invocation.
# Restore the pinned GCC object and .cmd even if MiniC's compilation fails.
restore() {
  cp -a "$ev/gcc.o" "$out/$target" || true
  cp -a "$ev/gcc.o.cmd" "$out/lib/.idr.o.cmd" || true
}
trap restore EXIT
rm -f "$out/$target" "$out/lib/.idr.o.cmd" "$out/$stem.minic-stage2.i" "$out/$stem.minic-stage2.s"
MINIC="$minic" REAL_CC=/usr/bin/riscv64-linux-gnu-gcc MINIC_KEEP_INTERMEDIATES=1 \
  MINIC_KBUILD_TRACE="$ev/one-tu.trace" \
  make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
    CC="$wrapper" -j1 V=1 "$target" >"$ev/compile.log" 2>&1 || {
    echo "GNU_SINGLE=FAIL stage=minic-kbuild-targe target=$target" >&2
    tail -n 100 "$ev/compile.log" >&2
    exit 4
  }
test -s "$out/$stem.minic-stage2.i"
test -s "$out/$stem.minic-stage2.s"
test -s "$out/$target"
cp -a "$out/$stem.minic-stage2.i" "$ev/idr.i"
cp -a "$out/$stem.minic-stage2.s" "$ev/idr.minic.s"
cp -a "$out/$target" "$ev/minic.o"
grep -F 'minic input=' "$ev/one-tu.trace" >/dev/null
grep -F 'assemble input=' "$ev/one-tu.trace" >/dev/null
minic_sha=$(sha256sum "$ev/minic.o" | cut -d' ' -f1)
echo "GNU_SINGLE_MINIC=PASS target=$target gcc_sha=$gcc_sha minic_sha=$minic_sha"
printf 'minic_object_sha256=%s\ninput_sha256=%s\n' "$minic_sha" "$(sha256sum "$ev/idr.i" | cut -d' ' -f1)" >>"$ev/identity.txt"
# The Kbuild wrapper's .cmd is for MiniC; restore the GNU .cmd so a normal
# GNU incremental link can consume the MiniC object without recompiling it.
cp -a "$ev/gcc.o.cmd" "$out/lib/.idr.o.cmd"
# Explicitly newer than preexisting thin archives so GNU Kbuild must relink.
touch "$out/$target"
make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
  -j2 V=1 Image >"$ev/link.log" 2>&1 || {
    echo "GNU_SINGLE=FAIL stage=gnu-relink" >&2
    tail -n 100 "$ev/link.log" >&2
    exit 5
  }
actual_minic_sha=$(sha256sum "$out/$target" | cut -d' ' -f1)
[[ "$minic_sha" == "$actual_minic_sha" ]] || {
  echo "GNU_SINGLE=ERROR GNU Kbuild overwrote MiniC target with GCC; cannot interpret runtime" >&2
  exit 6
}
new_image_sha=$(sha256sum "$out/arch/riscv/boot/Image" | cut -d' ' -f1)
[[ "$new_image_sha" != "$baseline_image_sha" ]] || {
  echo "GNU_SINGLE=ERROR image unchanged after object substitution" >&2
  exit 7
}
printf 'mixed_image_sha256=%s\n' "$new_image_sha" >>"$ev/identity.txt"
echo "GNU_SINGLE_RELINK=PASS original=$baseline_image_sha mixed=$new_image_sha"
cp -a "$out/arch/riscv/boot/Image" "$ev/mixed.Image"
BUILD_DIR="$ev/initramfs" \
  OUTPUT_INITRAMFS="$ev/runtime-initramfs.cpio.gz" RISCV_CC=riscv64-linux-gnu-gcc \
  bash "$repo/tests/external/linux/build_runtime_initramfs.sh" >"$ev/initramfs.log" 2>&1
LINUX_IMAGE="$ev/mixed.Image" INITRAMFS="$ev/runtime-initramfs.cpio.gz" \
  BUILD_DIR="$ev/qemu" LINUX_RELEASE=6.6.143 QEMU_TIMEOUT_SECONDS=90 \
  bash "$repo/tests/external/linux/runtime_boot.sh" >"$ev/qemu.result.log" 2>&1 || {
    echo "GNU_SINGLE_RUNTIME=FAIL target=$target" >&2
    tail -n 120 "$ev/qemu.result.log" >&2
    exit 8
  }
echo "GNU_SINGLE_RUNTIME=PASS target=$target"
