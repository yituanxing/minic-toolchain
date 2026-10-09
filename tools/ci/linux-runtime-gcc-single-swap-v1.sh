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
target=${GNU_SINGLE_TARGET:-lib/idr.o}
case "$target" in
  lib/idr.o|lib/xarray.o|kernel/sched/core.o|kernel/fork.o|mm/memory.o|kernel/locking/spinlock.o|arch/riscv/mm/init.o|arch/riscv/kernel/setup.o|arch/riscv/kernel/irq.o|kernel/time/timekeeping.o|lib/string.o|mm/page_alloc.o|kernel/rcu/tree.o|arch/riscv/kernel/process.o) ;;
  *) echo "GNU_SINGLE=ERROR unreviewed target=$target" >&2; exit 64 ;;
esac
stem=${target%.o}
leaf=${stem##*/}
object_cmd="$out/$(dirname "$target")/.$leaf.o.cmd"
expected_cfg=$(sed -n 's/^config_sha256=//p' "$provenance/gcc-baseline.txt")
actual_cfg=$(sha256sum "$out/.config" | cut -d' ' -f1)
[[ -n "$expected_cfg" && "$expected_cfg" == "$actual_cfg" ]] || {
  echo "GNU_SINGLE=ERROR config mismatch expected=$expected_cfg actual=$actual_cfg" >&2; exit 2;
}
test -s "$out/$target"
test -s "$object_cmd"
test -s "$out/arch/riscv/boot/Image"
test -x "$minic"
test -d "$src"
mkdir -p "$ev"
trial_started=$(date +%s%N)
expected_object=$(awk -v t="$target" '$2 == t { print $1; exit }' "$provenance/gcc-objects.sha256")
gcc_sha=$(sha256sum "$out/$target" | cut -d' ' -f1)
[[ -n "$expected_object" && "$gcc_sha" == "$expected_object" ]] || {
  echo "GNU_SINGLE=ERROR gcc original object not certified" >&2; exit 3;
}
echo "GNU_SINGLE_BASELINE=PASS target=$target config=$actual_cfg gcc_sha=$gcc_sha"
cp -a "$out/$target" "$ev/gcc.o"
cp -a "$object_cmd" "$ev/gcc.o.cmd"
baseline_image_sha=$(sha256sum "$out/arch/riscv/boot/Image" | cut -d' ' -f1)
printf 'baseline_image_sha256=%s\n' "$baseline_image_sha" >"$ev/identity.txt"

# Reuse GCC preprocessed input iff every producer identity still matches.
# The first trial obtains the genuine .i and assembler flags from Kbuild;
# subsequent MiniC fixes need only MiniC -S, GNU as, GNU link and QEMU.
pp_contract=$(printf '%s\n' \
  "$actual_cfg" "$gcc_sha" \
  "$(sha256sum "$ev/gcc.o.cmd" | cut -d' ' -f1)" \
  "$(sha256sum "$src/$stem.c" | cut -d' ' -f1)" \
  "$(sha256sum /usr/bin/riscv64-linux-gnu-gcc | cut -d' ' -f1)" \
  | sha256sum | cut -d' ' -f1)
restore() {
  cp -a "$ev/gcc.o" "$out/$target" || true
  cp -a "$ev/gcc.o.cmd" "$object_cmd" || true
}
trap restore EXIT
have_pp=0
for path in "$ev/$leaf.i" "$ev/asm-args.txt" "$ev/pp-contract.txt" "$ev/pp-files.sha256"; do
  if [[ -e "$path" ]]; then have_pp=1; fi
done
if (( have_pp )); then
  for path in "$ev/$leaf.i" "$ev/asm-args.txt" "$ev/pp-contract.txt" "$ev/pp-files.sha256"; do
    test -s "$path" || { echo "GNU_SINGLE=ERROR incomplete cached PP: $path" >&2; exit 4; }
  done
  [[ $(cat "$ev/pp-contract.txt") == "$pp_contract" ]] || {
    echo "GNU_SINGLE=ERROR cached .i producer identity differs" >&2; exit 4;
  }
  (cd "$ev"; sha256sum -c pp-files.sha256 >/dev/null)
  echo "GNU_SINGLE_PP_CACHE=HIT"
  rm -f "$out/$target"
  CORE_FAST_TRACE=1 "$minic" -S "$ev/$leaf.i" -o "$ev/$leaf.minic.s" \
    >"$ev/compile.log" 2>&1 || { echo 'GNU_SINGLE=FAIL stage=minic-replay' >&2; exit 4; }
  asm_args=()
  while IFS= read -r arg; do
    case "$arg" in
      -march=*|-mabi=*|-mcmodel=*|-mstrict-align|-mno-strict-align|-mno-save-restore|-mno-relax|-Wa,*) asm_args+=("$arg");;
      *) echo "GNU_SINGLE=ERROR invalid cached GNU-as flag=$arg" >&2; exit 4;;
    esac
  done <"$ev/asm-args.txt"
  test "$(wc -l <"$ev/asm-args.txt")" -ge 1
  /usr/bin/riscv64-linux-gnu-gcc "${asm_args[@]}" -x assembler -c \
    "$ev/$leaf.minic.s" -o "$out/$target" >"$ev/assemble.log" 2>&1
else
  echo "GNU_SINGLE_PP_CACHE=MISS"
  rm -f "$out/$target" "$object_cmd" "$out/$stem.minic-stage2.i" "$out/$stem.minic-stage2.s"
  MINIC="$minic" REAL_CC=/usr/bin/riscv64-linux-gnu-gcc MINIC_KEEP_INTERMEDIATES=1 \
    MINIC_KBUILD_TRACE="$ev/one-tu.trace" \
    make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
      CC="$wrapper" -j1 V=1 "$target" >"$ev/compile.log" 2>&1 || {
      echo "GNU_SINGLE=FAIL stage=minic-kbuild-target target=$target" >&2
      tail -n 100 "$ev/compile.log" >&2
      exit 4
    }
  test -s "$out/$stem.minic-stage2.i"
  test -s "$out/$stem.minic-stage2.s"
  test -s "$out/$target"
  cp -a "$out/$stem.minic-stage2.i" "$ev/$leaf.i"
  cp -a "$out/$stem.minic-stage2.s" "$ev/$leaf.minic.s"
  grep -F 'minic input=' "$ev/one-tu.trace" >/dev/null
  grep -F 'assemble input=' "$ev/one-tu.trace" >/dev/null
  python3 - "$ev/one-tu.trace" "$ev/asm-args.txt" <<'PY_FLAGS'
import shlex, sys
from pathlib import Path
calls = [shlex.split(line[5:]) for line in Path(sys.argv[1]).read_text().splitlines()
         if line.startswith("argv ")]
if not calls:
    raise SystemExit("Kbuild wrapper did not record GCC argv")
result = [a for a in calls[-1] if a in (
    "-mstrict-align", "-mno-strict-align", "-mno-save-restore", "-mno-relax"
) or a.startswith(("-march=", "-mabi=", "-mcmodel=", "-Wa,"))]
if not result:
    raise SystemExit("Kbuild wrapper recorded no GNU assembler flags")
Path(sys.argv[2]).write_text("\n".join(result) + "\n")
print(f"GNU_SINGLE_CAPTURED_ASM_FLAGS={len(result)}")
PY_FLAGS
  printf '%s\n' "$pp_contract" >"$ev/pp-contract.txt"
  (cd "$ev"; sha256sum "$leaf.i" asm-args.txt >pp-files.sha256)
  echo "GNU_SINGLE_PP_CACHE=PREPARED sha=$(sha256sum "$ev/$leaf.i" | cut -d' ' -f1)"
fi
test -s "$out/$target"
cp -a "$out/$target" "$ev/minic.o"
minic_sha=$(sha256sum "$ev/minic.o" | cut -d' ' -f1)
echo "GNU_SINGLE_MINIC=PASS target=$target gcc_sha=$gcc_sha minic_sha=$minic_sha"
compile_finished=$(date +%s%N)
echo "GNU_SINGLE_TIMING compile_ms=$(((compile_finished-trial_started)/1000000))" | tee "$ev/timing.txt"
printf 'minic_object_sha256=%s\ninput_sha256=%s\n' "$minic_sha" "$(sha256sum "$ev/$leaf.i" | cut -d' ' -f1)" >>"$ev/identity.txt"
# The Kbuild wrapper's .cmd is for MiniC; restore the GNU .cmd so a normal
# GNU incremental link can consume the MiniC object without recompiling it.
cp -a "$ev/gcc.o.cmd" "$object_cmd"
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
link_finished=$(date +%s%N)
echo "GNU_SINGLE_TIMING relink_ms=$(((link_finished-compile_finished)/1000000))" | tee -a "$ev/timing.txt"
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
trial_finished=$(date +%s%N)
echo "GNU_SINGLE_TIMING runtime_ms=$(((trial_finished-link_finished)/1000000)) total_ms=$(((trial_finished-trial_started)/1000000))" | tee -a "$ev/timing.txt"
