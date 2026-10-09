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
(( n>=1000 && n<=3000 )) || { echo "COHORT_ERROR full_C_objects=$n; refuse partial-kernel verdict"; exit 2; }
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
# Preserve a COMPLETE immutable GCC output tree, not only the selected .o
# files. Linux Kbuild mutates generated headers, vDSO offsets, .cmd metadata,
# built-in archives and kallsyms during intermediate target builds. Restoring
# only 384 .o files leaves those generated dependencies contaminated.
gold_snapshot="$ev/gnu-out-pristine"
test ! -e "$gold_snapshot"
cp -a --reflink=auto "$out" "$gold_snapshot"
echo "COHORT_GOLDEN_SNAPSHOT=PASS bytes=$(du -sb "$gold_snapshot" | cut -f1)"
# Cheap fail-fast before expensive MiniC compilation: verify that restoring
# the fixture into the current runner can make its own GCC Image without
# recompiling any certified reference object.
echo "COHORT_GNU_PREFLIGHT=START"
make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
  -j4 Image >"$ev/gcc-preflight.log" 2>&1 || {
    echo "COHORT_GNU_PREFLIGHT=FAIL stage=kbuild"
    tail -n 80 "$ev/gcc-preflight.log"
    exit 5
  }
awk '$2 != "init/version-timestamp.o" {print}' \
  "$prov/gcc-objects.sha256" >"$ev/gcc-objects-stable.sha256"
if ! (cd "$out"; sha256sum --quiet -c "$ev/gcc-objects-stable.sha256"); then
  echo "COHORT_GNU_PREFLIGHT=FAIL stage=reference_objects_changed"
  grep -E '(^|[[:space:]])(CC|AS|SYNC|VDSOSYM)[[:space:]]+' "$ev/gcc-preflight.log" | tail -n 36 || true
  exit 6
fi
echo "COHORT_GNU_PREFLIGHT=PASS objects=$n"
# Return to the exact certified Image/.cmd/generated-header state before
# touching MiniC compilation inputs. This also avoids inherited timestamps.
rm -rf -- "$out"
cp -a --reflink=auto "$gold_snapshot" "$out"
# Candidate .o cache identity is independent of CI harness-only commits.
# It depends on exact Linux/GCC golden evidence, target list, the MiniC
# executable produced by the checked-in profile, and GNU assembler identity.
# This permits reusing the *same* verified 384 .o after a CI harness fix.
compiler_sig=$(sha256sum "$minic" | cut -d' ' -f1)
assembler_sig=$(sha256sum /usr/bin/riscv64-linux-gnu-as | cut -d' ' -f1)
gold_sig=$(sha256sum "$prov/gcc-objects.sha256" | cut -d' ' -f1)
list_sig=$(sha256sum "$objects_file" | cut -d' ' -f1)
cache_contract=$(printf '%s\n' "$actual_cfg" "$gnu_image_sha" \
  "$compiler_sig" "$assembler_sig" "$gold_sig" "$list_sig" \
  | sha256sum | cut -d' ' -f1)
cached=0
if [[ -e "$ev/minic-manifest.sha256" || -e "$ev/minic-identity.txt" ]]; then
  test -s "$ev/minic-manifest.sha256" && test -s "$ev/minic-identity.txt" || {
    echo "COHORT_CANDIDATE_CACHE=ERROR incomplete manifest"; exit 7;
  }
  [[ $(cat "$ev/minic-identity.txt") == "$cache_contract" ]] || {
    echo "COHORT_CANDIDATE_CACHE=ERROR identity mismatch"; exit 7;
  }
  # Verify every expected MiniC object and ensure there is no partial pool.
  test "$(wc -l <"$ev/minic-manifest.sha256")" -eq "$n"
  (cd "$ev"; sha256sum --quiet -c minic-manifest.sha256) || {
    echo "COHORT_CANDIDATE_CACHE=ERROR object hash mismatch"; exit 7;
  }
  for target in "${objects[@]}"; do
    test -s "$ev/minic/$target" || { echo "COHORT_CANDIDATE_CACHE=ERROR missing $target"; exit 7; }
    grep -Fqx "minic/$target" <(awk '{print $2}' "$ev/minic-manifest.sha256") || {
      echo "COHORT_CANDIDATE_CACHE=ERROR unexpected candidate list"; exit 7;
    }
  done
  cached=1
  echo "COHORT_CANDIDATE_CACHE=HIT objects=$n"
else
  echo "COHORT_CANDIDATE_CACHE=MISS objects=$n"
  # Only on cache miss: one parallel Kbuild invokes exact GCC -E / MiniC -S
  # / GNU as. Pinned GNU output snapshot already restored above.
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
  cc_ended=$(date +%s%N)
  echo "COHORT_COMPILE=PASS objects=$n elapsed_ms=$(((cc_ended-cc_started)/1000000))"
fi
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
# Cache data only after *every* candidate passes exact ABI verification.
# A partial compilation cannot create an apparently usable object pool.
if ((cached==0)); then
  (cd "$ev"; for target in "${objects[@]}"; do
    sha256sum "minic/$target"
  done) >"$ev/minic-manifest.sha256"
  printf '%s\n' "$cache_contract" >"$ev/minic-identity.txt"
  echo "COHORT_CANDIDATE_CACHE=READY objects=$n"
else
  echo "COHORT_COMPILE=SKIPPED reason=exact-object-cache-hit objects=$n"
fi
# Build the initramfs just once; it stays fixed throughout all QEMU trials.
BUILD_DIR="$ev/initramfs" OUTPUT_INITRAMFS="$ev/runtime-initramfs.cpio.gz" \
  RISCV_CC=riscv64-linux-gnu-gcc \
  bash "$repo/tests/external/linux/build_runtime_initramfs.sh" >"$ev/initramfs.log" 2>&1
last_count=0
trial() {
  name="$1"; count="$2"; profile="$3"
  d="$ev/trials/$name"; mkdir -p "$d"
  if (( count != last_count )); then
    # Kbuild candidate compilation may have regenerated headers, scripts,
    # vdso metadata and .cmd inputs. Restoring just the selected .o is NOT
    # sufficient (384-object logs prove a GCC alternative.o recompile).
    # Restart each distinct QEMU experiment from the WHOLE certified GNU
    # output tree. 412 MiB source fixture, copied locally using reflinks
    # when available; no GitHub cache/network round trip here.
    rm -rf -- "$out"
    cp -a --reflink=auto "$gold_snapshot" "$out"
    for ((i=0;i<count;i++)); do
      cp -a "$ev/minic/${objects[i]}" "$out/${objects[i]}"
      touch "$out/${objects[i]}"
    done
    if ! make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
      -j4 Image >"$d/link.log" 2>&1; then
      echo "COHORT_LINK=FAIL name=$name count=$count"
      tail -n 70 "$d/link.log"
      printf '%s\t%s\t%s\t%s\n' "$name" "$count" LINK_FAIL - >>"$ev/results.tsv"
      TRIAL_VERDICT=LINK_FAIL
      return 0
    fi
    last_count="$count"
  else
    # Same object selection, different runtime oracle (FAST -> FULL):
    # skip a redundant link. Image bytes and all object IDs stay fixed.
    echo "COHORT_LINK=REUSED name=$name count=$count" | tee "$d/link.log"
  fi
  for ((i=0;i<n;i++)); do
    target=${objects[i]}
    if ((i<count)); then gold="$ev/minic/$target"; else gold="$ev/gcc/$target"; fi
    cmp -s "$out/$target" "$gold" || {
      echo "COHORT_IDENTITY=FAIL trial=$name target=$target"; exit 6;
    }
  done
  # Every object OUTSIDE the candidate universe must remain GCC-identical.
  # Generated init/version-timestamp.o is allowed to vary per link.
  python3 - "$out" "$prov/gcc-objects.sha256" "$objects_file" <<'PY_GNU_NONSELECTED'
from pathlib import Path
import hashlib, re, sys
out,manifest,universe=map(Path,sys.argv[1:])
excluded={x.strip() for x in universe.read_text().splitlines()
          if x.strip() and not x.lstrip().startswith("#")}
# Kbuild legitimately regenerates these *link-created* kallsyms objects
# whenever linked code/layout changes. They are not standalone GCC C owners.
# Do NOT broaden this exception to ordinary arch/, kernel/, lib/, fs/ objects.
generated_links={"init/version-timestamp.o"}
def link_generated(name):
    return name in generated_links or re.fullmatch(r"\.tmp_vmlinux\.kallsyms[1-3]\.o", name) is not None
for line in manifest.read_text().splitlines():
    digest, name=line.strip().split(None,1)
    if name in excluded or link_generated(name): continue
    p=out/name
    if not p.is_file() or hashlib.sha256(p.read_bytes()).hexdigest()!=digest:
        raise SystemExit("COHORT_GNU_CONTAMINATION=FAIL object="+name)
print("COHORT_GNU_UNSELECTED=PASS")
PY_GNU_NONSELECTED
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
    # Reuse the existing Python-era 12-syscall P1 oracle after FULL PASS.
    # It boots the identical Image and initramfs, without a third link.
    if [[ "$profile" == full && "$verdict" == PASS ]]; then
      set +e
      LINUX_IMAGE="$d/Image" INITRAMFS="$ev/runtime-initramfs.cpio.gz" \
        BUILD_DIR="$d/qemu-p1" LINUX_RELEASE=6.6.143 \
        LINUX_RUNTIME_PROFILE=p1 QEMU_TIMEOUT_SECONDS=45 \
        bash "$repo/tests/external/linux/runtime_boot.sh" >"$d/runtime-p1.log" 2>&1
      p1_rc=$?
      set -e
      if ((p1_rc==0)); then
        # Independent existing Python-era P0+P1 contract manifest. Distinguish
        # missing legacy markers (oracle/fixture mismatch) from runtime crashes.
        if python3 "$repo/tests/external/linux/runtime_v2_log_check.py" \
             --profile p1 --log "$d/qemu-p1/rdinit-init.log" \
             >"$d/runtime-p1-python.log" 2>&1; then
          echo "COHORT_P1=PASS name=$name syscalls=12 python_era_oracle=PASS"
        else
          verdict=INCONCLUSIVE
          echo "COHORT_P1=INCONCLUSIVE name=$name reason=python_era_oracle_disagrees"
          tail -n 28 "$d/runtime-p1-python.log"
        fi
      elif grep -REqi 'Linux version 6\.6\.143|Kernel panic|Oops:|Unable to handle|BUG:' "$d/qemu-p1" 2>/dev/null; then
        echo "COHORT_P1=FAIL name=$name rc=$p1_rc"
        verdict=FAIL
      else
        echo "COHORT_P1=INCONCLUSIVE name=$name rc=$p1_rc"
        verdict=INCONCLUSIVE
      fi
    fi
  fi
  echo "COHORT_TRIAL name=$name count=$count verdict=$verdict image=$image_sha"
  printf '%s\t%s\t%s\t%s\n' "$name" "$count" "$verdict" "$image_sha" >>"$ev/results.tsv"
  TRIAL_VERDICT="$verdict"
}
# FIRST test the entire MiniC-compilable C object universe. This is the
# user's actual intended failure oracle; do not waste more CI on 128, 384,
# 512 ... successful intermediate cohorts.
trial full_all "$n" fast
case "$TRIAL_VERDICT" in
  PASS)
    trial full_all_verified "$n" full
    case "$TRIAL_VERDICT" in
      PASS)
        echo "COHORT_RESULT=PASS all_minic_c_objects=$n same_config=true"
        exit 0 ;;
      FAIL|LINK_FAIL)
        echo "COHORT_RESULT=FAIL stage=full_or_p1 no_monotone_prefix_claim"
        exit 9 ;;
      *) echo "COHORT_RESULT=INCONCLUSIVE stage=full_or_p1"; exit 8 ;;
    esac ;;
  FAIL|LINK_FAIL)
    fail_kind="$TRIAL_VERDICT"
    lo=0
    hi="$n" ;;
  *) echo "COHORT_RESULT=INCONCLUSIVE at=full"; exit 8 ;;
esac
# Full FAIL (or LINK_FAIL) vs independently certified pure-GCC baseline PASS.
# Reuse the exact, already compiled candidate objects: no new C compilation
# during bisection. Prefix monotonicity must be verified, not assumed.
while ((hi-lo>1)); do
  mid=$(((lo+hi)/2))
  trial "prefix_$mid" "$mid" fast
  case "$TRIAL_VERDICT" in
    PASS) lo="$mid";;
    "$fail_kind") hi="$mid";;
    INCONCLUSIVE)
      echo "COHORT_RESULT=INCONCLUSIVE at=prefix_$mid"; exit 8 ;;
    *)
      echo "COHORT_RESULT=FAIL_MIXED_CLASSES full=$fail_kind prefix=$TRIAL_VERDICT"
      exit 8 ;;
  esac
done
candidate=${objects[hi-1]}
printf 'pass_prefix=%s\nfail_prefix=%s\ncandidate=%s\nfailure_kind=%s\n'   "$lo" "$hi" "$candidate" "$fail_kind" | tee "$ev/frontier.txt"
echo "COHORT_RESULT=$fail_kind prefix_candidate=$candidate single_causality=UNPROVEN"
exit 9
