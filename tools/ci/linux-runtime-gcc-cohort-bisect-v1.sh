#!/usr/bin/env bash
# One GitHub runner: one MiniC build + batched Kbuild objects, multiple
# authenticated GCC/MiniC object overlays + GNU incremental link + QEMU.
set -Eeuo pipefail
if (( $# != 6 )); then
  echo "usage: $0 <src> <out> <minic> <gold-provenance> <objects-file> <evidence-dir>" >&2
  exit 64
fi
src=$(realpath "$1"); out=$(realpath "$2"); minic=$(realpath "$3")
prov=$(realpath "$4"); objects_file=$(realpath "$5"); ev=$(realpath -m "$6")
repo=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$ev/gcc" "$ev/minic" "$ev/trials"
exec > >(tee "$ev/cohort.log") 2>&1
mapfile -t objects < <(grep -vE '^[[:space:]]*(#|$)' "$objects_file")
n=${#objects[@]}
(( n>=16 && n<=64 )) || { echo "COHORT_ERROR objects=$n"; exit 2; }
expected_cfg=$(sed -n 's/^config_sha256=//p' "$prov/gcc-baseline.txt")
actual_cfg=$(sha256sum "$out/.config" | cut -d' ' -f1)
[[ "$expected_cfg" == "$actual_cfg" && -n "$expected_cfg" ]] || {
  echo "COHORT_ERROR golden config mismatch"; exit 2;
}
test -s "$out/arch/riscv/boot/Image"
test -x "$minic"
python3 - "$objects_file" <<'PY_INPUT'
from pathlib import Path, PurePosixPath
import sys
entries=[l.strip() for l in Path(sys.argv[1]).read_text().splitlines()
         if l.strip() and not l.lstrip().startswith("#")]
assert len(entries)==len(set(entries)), "duplicate object paths"
for item in entries:
    p=PurePosixPath(item)
    assert not p.is_absolute() and ".." not in p.parts and len(p.parts)>1 and item.endswith(".o"), item
PY_INPUT
: >"$ev/results.tsv"
printf 'trial\tcount\tverdict\timage_sha\n' >>"$ev/results.tsv"
gnu_image_sha=$(sha256sum "$out/arch/riscv/boot/Image" | cut -d' ' -f1)
printf 'config=%s\ngnu_image=%s\nminic=%s\n' "$actual_cfg" "$gnu_image_sha" \
  "$(sha256sum "$minic" | cut -d' ' -f1)" >"$ev/identity.txt"
targets=(); commands=()
for target in "${objects[@]}"; do
  stem=${target%.o}; leaf=${stem##*/}
  cmd="$out/$(dirname "$target")/.$leaf.o.cmd"
  test -s "$out/$target"; test -s "$cmd"; test -s "$src/$stem.c"
  reference=$(awk -v t="$target" '$2==t {print $1;exit}' "$prov/gcc-objects.sha256")
  observed=$(sha256sum "$out/$target" | cut -d' ' -f1)
  [[ -n "$reference" && "$reference" == "$observed" ]] || {
    echo "COHORT_ERROR untrusted golden object $target"; exit 3;
  }
  mkdir -p "$ev/gcc/$(dirname "$target")" "$ev/minic/$(dirname "$target")"
  cp -a "$out/$target" "$ev/gcc/$target"
  cp -a "$cmd" "$ev/gcc/$target.cmd"
  targets+=("$target"); commands+=("$cmd")
done
restore_gcc() {
  for ((i=0;i<n;i++)); do
    cp -a "$ev/gcc/${objects[i]}" "$out/${objects[i]}" || true
    cp -a "$ev/gcc/${objects[i]}.cmd" "${commands[i]}" || true
  done
}
trap restore_gcc EXIT
echo "COHORT_GOLDEN=PASS objects=$n"
# Remove just the selected objects and let ONE parallel Kbuild invocation
# reproduce the exact GCC preprocessing flags and GNU assembler semantics.
for ((i=0;i<n;i++)); do
  rm -f "$out/${objects[i]}" "${commands[i]}"
done
cc_started=$(date +%s%N)
if ! MINIC="$minic" REAL_CC=/usr/bin/riscv64-linux-gnu-gcc \
  MINIC_KEEP_INTERMEDIATES=1 \
  make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
    CC="$repo/tests/external/linux/stage2_kbuild_cc.sh" -j4 "${targets[@]}" \
    >"$ev/compile.log" 2>&1; then
  echo "COHORT_COMPILE=FAIL"; tail -n 80 "$ev/compile.log"; exit 4
fi
for target in "${objects[@]}"; do
  test -s "$out/$target" || { echo "COHORT_COMPILE=FAIL target=$target"; exit 4; }
  stem=${target%.o}
  test -s "$out/$stem.minic-stage2.i" || { echo "COHORT_INPUT=FAIL target=$target"; exit 4; }
  cp -a "$out/$target" "$ev/minic/$target"
done
python3 - "$ev/gcc" "$ev/minic" "$objects_file" <<'PY_ABI'
from pathlib import Path
import struct,sys
a,b,p=map(Path,sys.argv[1:])
names=[x.strip() for x in p.read_text().splitlines() if x.strip() and not x.lstrip().startswith("#")]
for name in names:
    x=(a/name).read_bytes()[:52];y=(b/name).read_bytes()[:52]
    for t in (x,y):
        assert t[:6]==b"\x7fELF\x02\x01" and len(t)==52, name
        assert struct.unpack_from("<H",t,16)[0]==1, name
        assert struct.unpack_from("<H",t,18)[0]==243, name
    assert x[48:52]==y[48:52], "RISC-V ABI mismatch: "+name
print("COHORT_ABI=PASS objects="+str(len(names)))
PY_ABI
cc_ended=$(date +%s%N)
echo "COHORT_COMPILE=PASS objects=$n elapsed_ms=$(((cc_ended-cc_started)/1000000))"
# Build the initramfs just once; it stays fixed throughout all QEMU trials.
BUILD_DIR="$ev/initramfs" OUTPUT_INITRAMFS="$ev/runtime-initramfs.cpio.gz" \
  RISCV_CC=riscv64-linux-gnu-gcc \
  bash "$repo/tests/external/linux/build_runtime_initramfs.sh" >"$ev/initramfs.log" 2>&1
trial() {
  name="$1"; count="$2"; profile="$3"
  d="$ev/trials/$name"; mkdir -p "$d"
  restore_gcc
  for ((i=0;i<count;i++)); do
    cp -a "$ev/minic/${objects[i]}" "$out/${objects[i]}"
  done
  # Force affected thin archives to refresh, without C recompilation.
  for target in "${objects[@]}"; do touch "$out/$target"; done
  make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
    -j4 Image >"$d/link.log" 2>&1 || {
      echo "COHORT_LINK=FAIL name=$name"; tail -n 70 "$d/link.log"; exit 5;
    }
  for ((i=0;i<n;i++)); do
    target=${objects[i]}
    if ((i<count)); then gold="$ev/minic/$target"; else gold="$ev/gcc/$target"; fi
    cmp -s "$out/$target" "$gold" || {
      echo "COHORT_IDENTITY=FAIL trial=$name target=$target"; exit 6;
    }
  done
  image_sha=$(sha256sum "$out/arch/riscv/boot/Image" | cut -d' ' -f1)
  cp -a "$out/arch/riscv/boot/Image" "$d/Image"
  verdict=INCONCLUSIVE
  if ((count>0)) && [[ "$image_sha" == "$gnu_image_sha" ]]; then
    echo "COHORT_IMAGE=UNCHANGED name=$name"
  else
    set +e
    LINUX_IMAGE="$d/Image" INITRAMFS="$ev/runtime-initramfs.cpio.gz" \
      BUILD_DIR="$d/qemu" LINUX_RELEASE=6.6.143 \
      LINUX_RUNTIME_PROFILE="$profile" QEMU_TIMEOUT_SECONDS=45 \
      bash "$repo/tests/external/linux/runtime_boot.sh" >"$d/runtime.log" 2>&1
    rc=$?
    set -e
    if ((rc==0)); then verdict=PASS
    elif grep -REqi 'Linux version 6\.6\.143|Kernel panic|Oops:|Unable to handle|BUG:' "$d/qemu" 2>/dev/null; then verdict=FAIL
    fi
  fi
  echo "COHORT_TRIAL name=$name count=$count verdict=$verdict image=$image_sha"
  printf '%s\t%s\t%s\t%s\n' "$name" "$count" "$verdict" "$image_sha" >>"$ev/results.tsv"
  TRIAL_VERDICT="$verdict"
}
trial known14 14 fast
case "$TRIAL_VERDICT" in
  FAIL) lo=0; hi=14;;
  PASS)
    trial expanded32 "$n" fast
    case "$TRIAL_VERDICT" in
      PASS)
        trial expanded32_full "$n" full
        [[ "$TRIAL_VERDICT" == PASS ]] || {
          echo "COHORT_RESULT=$TRIAL_VERDICT at=expanded32_full"; exit 8;
        }
        echo "COHORT_RESULT=PASS combined=$n same_config=true"
        exit 0 ;;
      FAIL) lo=14; hi="$n";;
      *) echo "COHORT_RESULT=INCONCLUSIVE at=expanded32"; exit 8;;
    esac ;;
  *) echo "COHORT_RESULT=INCONCLUSIVE at=known14"; exit 8;;
esac
# Bisection only after a proven PASS/FAIL bracket in the SAME fixture.
while ((hi-lo>1)); do
  mid=$(((lo+hi)/2))
  trial "prefix_$mid" "$mid" fast
  case "$TRIAL_VERDICT" in
    PASS) lo="$mid";;
    FAIL) hi="$mid";;
    *) echo "COHORT_RESULT=INCONCLUSIVE at=prefix_$mid"; exit 8;;
  esac
done
candidate=${objects[hi-1]}
printf 'pass_prefix=%s\nfail_prefix=%s\ncandidate=%s\n' "$lo" "$hi" "$candidate" \
  | tee "$ev/frontier.txt"
echo "COHORT_RESULT=RUNTIME_FAIL first_boundary=$candidate single_causality=UNPROVEN"
exit 9
