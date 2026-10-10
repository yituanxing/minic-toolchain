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
trap 'rc=$?; echo "COHORT_UNHANDLED=FAIL rc=$rc line=$LINENO cmd=$BASH_COMMAND" >&2' ERR
mapfile -t objects < <(grep -vE '^[[:space:]]*(#|$)' "$objects_file")
n=${#objects[@]}
# The 384-object opt-in is diagnostic only, not a whole-C-universe verdict.
if ((n==384)) && [[ "${COHORT_ALLOW_DIAGNOSTIC_384:-0}" == 1 ]]; then
  cohort_scope=DIAGNOSTIC_384
elif ((n>=1000 && n<=3000)); then
  cohort_scope=FULL_C_UNIVERSE
elif [[ "${COHORT_ALLOW_SHARD:-0}" == 1 && "${COHORT_ACTION:-all}" == compile ]] && ((n>=1 && n<1000)); then
  # Shards are compile-only. They do not claim standalone Linux runtime PASS.
  # The consumer must prove the exact 7-way union and run normal GNU/QEMU.
  cohort_scope=SHARD_COMPILE_ONLY
else
  echo "COHORT_ERROR full_C_objects=$n; refuse unlabelled partial-kernel verdict"; exit 2
fi
echo "COHORT_SCOPE=$cohort_scope objects=$n"
case "${COHORT_ACTION:-all}" in
  all|compile|verify) ;;
  *) echo "COHORT_ACTION=ERROR unknown_action=${COHORT_ACTION}" >&2; exit 64 ;;
esac
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
  test -s "$out/$target" || { echo "COHORT_GOLDEN=ERROR missing object=$target"; exit 3; }
  test -s "$cmd" || { echo "COHORT_GOLDEN=ERROR missing Kbuild cmd=$cmd"; exit 3; }
  # Not all C objects map to source-tree <target>.c. Generated and renamed
  # C TUs are accepted only because the full-C manifest was derived from
  # the certified GCC target's actual source_ / savedcmd_ metadata.
  if [[ ! -s "$src/$stem.c" ]]; then
    echo "COHORT_SOURCE=NONCANONICAL target=$target use_pinned_kbuild_cmd=true"
  fi
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
  if [[ "${COHORT_ACTION:-all}" == verify ]]; then
    echo "COHORT_CANDIDATE_CACHE=ERROR verify_requires_prior_checked_candidates" >&2
    exit 7
  fi
  echo "COHORT_CANDIDATE_CACHE=MISS objects=$n"
  # Only on cache miss: one parallel Kbuild invokes exact GCC -E / MiniC -S
  # / GNU as. Pinned GNU output snapshot already restored above.
  for ((i=0;i<n;i++)); do
    rm -f "$out/${objects[i]}" "${commands[i]}"
  done
  cc_started=$(date +%s%N)
  # Compile the ENTIRE GCC-built C owner universe. Keep .i/.s ephemeral to
  # avoid filling runner disk. -k collects independent compile failures.
  compile_rc=0
  : >"$ev/mini-success-objects.txt"
  : >"$ev/mini-object-hashes.sha256"
  # Kbuild can legitimately rebuild an explicit target, even with -j1.
  # Post-assembler digest attestations prove that the FINAL candidate object
  # is a byte-for-byte MiniC output, without falsely rejecting such rebuilds.
  # Try the fast parallel build first; a Kbuild .o.d race must not force
  # 18-minute serial compiles on every future CI run.
  compile_jobs=6
  # Profile the REAL compiler phases, not merely overall CI wall time. The
  # existing linux-runtime-build-profiler-v1.sh is whole-Image oriented and
  # would rebuild unrelated Kbuild targets; here we intercept ONLY the exact
  # already-selected C-object candidate path. Probes are disposable and never
  # alter MiniC binary, input flags, cache key, or certified object bytes.
  probe_minic="$minic"
  probe_gcc=/usr/bin/riscv64-linux-gnu-gcc
  if [[ "$cohort_scope" == SHARD_COMPILE_ONLY && "${COHORT_PROFILE_STAGES:-1}" == 1 ]]; then
    probe_dir="$ev/stage-probe"
    mkdir -p "$probe_dir"
    : >"$probe_dir/stages.tsv"
    cat >"$probe_dir/measure" <<'SH_STAGE_MEASURE'
#!/usr/bin/env bash
set -uo pipefail
kind=${PERF_KIND:?}
executable=${PERF_REAL:?}
log=${PERF_LOG:?}
input="-"
output="-"
args=("$@")
for ((i=0;i<${#args[@]};i++)); do
  case "${args[i]}" in
    -E) [[ "$kind" != minic ]] && kind=gcc_E ;;
    -x)
      if ((i+1<${#args[@]})) && [[ "${args[i+1]}" == assembler* ]]; then
        kind=gnu_as
      fi ;;
    -o)
      if ((i+1<${#args[@]})); then
        output=${args[i+1]}
        ((i+=1))
      fi ;;
    *.c|*.i|*.s|*.S) input=${args[i]} ;;
  esac
done
bytes=0
[[ "$input" == "-" ]] || bytes=$(stat -c '%s' -- "$input" 2>/dev/null || printf 0)
started=$(date +%s%N)
"$executable" "$@"
rc=$?
ended=$(date +%s%N)
{
  flock 9
  printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$kind" "$(((ended-started)/1000000))" "$rc" "$bytes" "$input" "$output" >&9
} 9>>"$log"
exit "$rc"
SH_STAGE_MEASURE
    chmod +x "$probe_dir/measure"
    for label in minic gcc; do
      if [[ "$label" == minic ]]; then
        real_tool="$minic"
        probe_minic="$probe_dir/minic"
      else
        real_tool=/usr/bin/riscv64-linux-gnu-gcc
        probe_gcc="$probe_dir/gcc"
      fi
      cat >"$probe_dir/$label" <<SH_STAGE_LAUNCHER
#!/usr/bin/env bash
export PERF_KIND=$(printf '%q' "$label")
export PERF_REAL=$(printf '%q' "$real_tool")
export PERF_LOG=$(printf '%q' "$probe_dir/stages.tsv")
exec $(printf '%q' "$probe_dir/measure") "\$@"
SH_STAGE_LAUNCHER
      chmod +x "$probe_dir/$label"
    done
    echo "COHORT_STAGE_PROFILE=ENABLED minic=$minic gcc=/usr/bin/riscv64-linux-gnu-gcc"
  fi
  echo "COHORT_KBUILD_PARALLELISM jobs=$compile_jobs scope=$cohort_scope"
  MINIC="$probe_minic" REAL_CC="$probe_gcc" \
    MINIC_KEEP_INTERMEDIATES=0 MINIC_PRESERVE_FAILURE_INPUTS=1 \
    MINIC_KBUILD_SUCCESS_TRACE="$ev/mini-success-objects.txt" \
    MINIC_KBUILD_OBJECT_HASH_TRACE="$ev/mini-object-hashes.sha256" \
    make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
      CC="$repo/tests/external/linux/stage2_kbuild_cc.sh" -j"$compile_jobs" -k "${targets[@]}" \
      >"$ev/compile.log" 2>&1 || compile_rc=$?
  if ((compile_rc != 0)) && [[ "$cohort_scope" == SHARD_COMPILE_ONLY ]]; then
    missing_targets=()
    for target in "${objects[@]}"; do
      [[ -s "$out/$target" ]] || missing_targets+=("$target")
    done
    if (("${#missing_targets[@]}" > 0)); then
      echo "COHORT_KBUILD_RETRY=START failed_parallel_rc=$compile_rc missing=${#missing_targets[@]} jobs=1"
      for target in "${missing_targets[@]}"; do
        rm -f -- "$out/$target" "$out/$(dirname "$target")/.${target##*/}.cmd"
      done
      retry_rc=0
      MINIC="$probe_minic" REAL_CC="$probe_gcc" \
        MINIC_KEEP_INTERMEDIATES=0 MINIC_PRESERVE_FAILURE_INPUTS=1 \
        MINIC_KBUILD_SUCCESS_TRACE="$ev/mini-success-objects.txt" \
        MINIC_KBUILD_OBJECT_HASH_TRACE="$ev/mini-object-hashes.sha256" \
        make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
          CC="$repo/tests/external/linux/stage2_kbuild_cc.sh" -j1 -k "${missing_targets[@]}" \
          >"$ev/retry.log" 2>&1 || retry_rc=$?
      if ((retry_rc == 0)); then
        compile_rc=0
        echo "COHORT_KBUILD_RETRY=PASS restored=${#missing_targets[@]} original_rc_preserved_in_compile_log"
      else
        echo "COHORT_KBUILD_RETRY=FAIL rc=$retry_rc"
        tail -n 35 "$ev/retry.log"
      fi
    fi
  fi
  if [[ -s "$ev/stage-probe/stages.tsv" ]]; then
    python3 - "$ev/stage-probe/stages.tsv" <<'PY_STAGE_TOTALS'
from pathlib import Path
import sys
from collections import defaultdict
rows = defaultdict(list)
for line in Path(sys.argv[1]).read_text().splitlines():
    fields = line.split("\t")
    if len(fields) != 6:
        raise SystemExit("COHORT_STAGE_PROFILE=FAIL malformed probe evidence")
    stage, elapsed, rc, nbytes, inp, outp = fields
    rows[stage].append((int(elapsed), int(rc), int(nbytes), inp))
for stage, samples in sorted(rows.items()):
    durations = sorted(x[0] for x in samples)
    n = len(durations)
    total = sum(durations)
    slowest = max(samples, key=lambda x: x[0])
    print(f"COHORT_STAGE_TIME stage={stage} count={n} "
          f"total_ms={total} p50_ms={durations[(n-1)//2]} "
          f"p95_ms={durations[(95*n+99)//100-1]} "
          f"max_ms={slowest[0]} failed={sum(x[1]!=0 for x in samples)} "
          f"max_input_bytes={slowest[2]} slowest_input={slowest[3]}")
PY_STAGE_TOTALS
  fi
  : >"$ev/compile-blockers.txt"
  compiled=0
  for target in "${objects[@]}"; do
    if [[ -s "$out/$target" ]]; then
      cp -a "$out/$target" "$ev/minic/$target"
      ((compiled+=1))
    else
      printf '%s\n' "$target" >>"$ev/compile-blockers.txt"
    fi
  done
  cc_ended=$(date +%s%N)
  echo "COHORT_FULL_COMPILE attempted=$n compiled=$compiled failed=$((n-compiled)) rc=$compile_rc elapsed_ms=$(((cc_ended-cc_started)/1000000))"
  if ((compile_rc != 0 || compiled != n)); then
    # Preserve a bounded set of frozen failing .i inputs instead of uploading
    # potentially thousands of preprocessed Linux files. Keep the COMPLETE
    # blocker list and compile log regardless of sampling.
    printf 'object\tstate\tbytes\tsha256\n' >"$ev/failure-pool-index.tsv"
    kept=0
    kept_bytes=0
    max_keep=32
    max_keep_bytes=$((128 * 1024 * 1024))
    while IFS= read -r failed_obj; do
      [[ -n "$failed_obj" ]] || continue
      stem=${failed_obj%.o}
      failed_i="$out/$stem.minic-stage2.failed.i"
      failed_err="$out/$stem.minic-stage2.failed.stderr"
      if [[ ! -s "$failed_i" ]]; then
        printf '%s\tNO_I\t0\t-\n' "$failed_obj" >>"$ev/failure-pool-index.tsv"
        continue
      fi
      i_bytes=$(stat -c %s "$failed_i")
      i_sha=$(sha256sum "$failed_i" | cut -d' ' -f1)
      if ((kept < max_keep && kept_bytes + i_bytes <= max_keep_bytes)); then
        printf '%s\tKEPT\t%s\t%s\n' "$failed_obj" "$i_bytes" "$i_sha" >>"$ev/failure-pool-index.tsv"
        ((kept += 1))
        kept_bytes=$((kept_bytes + i_bytes))
      else
        printf '%s\tOMITTED_CAP\t%s\t%s\n' "$failed_obj" "$i_bytes" "$i_sha" >>"$ev/failure-pool-index.tsv"
        rm -f -- "$failed_i" "$failed_err"
      fi
    done <"$ev/compile-blockers.txt"
    echo "COHORT_FAILURE_POOL=BOUNDED failed=$((n-compiled)) frozen_i=$kept bytes=$kept_bytes cap_files=$max_keep cap_bytes=$max_keep_bytes index=$ev/failure-pool-index.tsv"
    echo "COHORT_FULL_COMPILE=FAIL evidence=compile.log,compile-blockers.txt,failure-pool-index.tsv"
    tail -n 75 "$ev/compile.log"
    exit 4
  fi

  # An existing .o file and matching ABI do NOT prove which C compiler made
  # it. Require one explicit post-MiniC+GNU-AS success record per selected TU.
  python3 "$repo/tools/ci/linux-runtime-minic-route-audit-v1.py" \
    --objects-file "$objects_file" \
    --success-trace "$ev/mini-success-objects.txt" \
    --object-digest-trace "$ev/mini-object-hashes.sha256" \
    --candidates "$ev/minic" || {
      echo "COHORT_COMPILE=FAIL reason=incomplete_minic_compiler_provenance"
      exit 11
    }
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
# Split candidate production from relink/QEMU. The CI workflow can save
# immutable verified MiniC objects BEFORE any link/harness failure occurs.
# A second invocation with COHORT_ACTION=verify must reuse exactly these
# candidate bytes and cannot silently recompile.
if [[ "${COHORT_ACTION:-all}" == compile ]]; then
  test -s "$ev/minic-manifest.sha256" && test -s "$ev/minic-identity.txt"
  (cd "$ev"; sha256sum --quiet -c minic-manifest.sha256)
  # Restore pristine GOLDEN Kbuild state on disk for the verify-only step.
  rm -rf -- "$out"
  cp -a --reflink=auto "$gold_snapshot" "$out"
  echo "COHORT_PRODUCE_ONLY=PASS objects=$n candidate_identity=$cache_contract"
  exit 0
fi
# Build the initramfs just once; it stays fixed throughout all QEMU trials.
BUILD_DIR="$ev/initramfs" OUTPUT_INITRAMFS="$ev/runtime-initramfs.cpio.gz" \
  RISCV_CC=riscv64-linux-gnu-gcc \
  bash "$repo/tests/external/linux/build_runtime_initramfs.sh" >"$ev/initramfs.log" 2>&1
last_selection="none"
last_candidate_delta=""
last_link_source_dir=""
trial() {
  name="$1"; count="$2"; profile="$3"; mode="${4:-prefix}"
  selection="$mode:$count"
  d="$ev/trials/$name"; mkdir -p "$d"
  if [[ "$selection" != "$last_selection" ]]; then
    # Kbuild candidate compilation may have regenerated headers, scripts,
    # vdso metadata and .cmd inputs. Restoring just the selected .o is NOT
    # sufficient (384-object logs prove a GCC alternative.o recompile).
    # Restart each distinct QEMU experiment from the WHOLE certified GNU
    # output tree. 412 MiB source fixture, copied locally using reflinks
    # when available; no GitHub cache/network round trip here.
    rm -rf -- "$out"
    cp -a --reflink=auto "$gold_snapshot" "$out"
    : >"$d/selected-objects.txt"
    for ((i=0;i<n;i++)); do
      selected=0
      case "$mode" in
        prefix) if ((i<count)); then selected=1; fi ;;
        single) if ((i==count)); then selected=1; fi ;;
        all_except) if ((i!=count)); then selected=1; fi ;;
        *) echo "COHORT_ERROR unknown_selection=$mode"; exit 2 ;;
      esac
      if ((selected)); then
        cp -a "$ev/minic/${objects[i]}" "$out/${objects[i]}"
        touch "$out/${objects[i]}"
        printf '%s\n' "${objects[i]}" >>"$d/selected-objects.txt"
      fi
    done
    # Changing a candidate vDSO object may cause GNU Kbuild to regenerate
    # headers and overwrite a selected MiniC object with GCC. Snapshot
    # CONTENT identities before linking; timestamps alone are not trusted.
    python3 "$repo/tools/ci/linux-runtime-kbuild-regeneration-guard-v1.py" \
      --mode snapshot --out "$out" --evidence "$d/generated-before.json" \
      >"$d/guard.log"
    link_started=$(date +%s%N)
    if ! make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
      -j4 Image >"$d/link.log" 2>&1; then
      echo "COHORT_LINK=FAIL name=$name count=$count"
      tail -n 70 "$d/link.log"
      printf '%s\t%s\t%s\t%s\n' "$name" "$count" LINK_FAIL - >>"$ev/results.tsv"
      TRIAL_VERDICT=LINK_FAIL
      return 0
    fi
    link_finished=$(date +%s%N)
    echo "COHORT_TIMING trial=$name stage=gnu_first_link elapsed_ms=$(((link_finished-link_started)/1000000))" 
    printf 'timing_%s_first_link_ms=%s\n' "$name" "$(((link_finished-link_started)/1000000))" >>"$ev/identity.txt"
    # The vDSO linker may produce a NEW vdso-offsets.h. The exact dependent
    # C TUs must be re-preprocessed with GNU GCC, recompiled with the SAME
    # MiniC binary and reassembled by GNU as. Never transplant an old object
    # compiled against stale offsets or loosen the generated-input guard.
    python3 "$repo/tools/ci/linux-runtime-kbuild-regeneration-guard-v1.py" \
      --mode impact --out "$out" --evidence "$d/generated-before.json" \
      --golden-out "$gold_snapshot" --selected "$d/selected-objects.txt" \
      --affected-file "$d/affected-objects.txt" | tee -a "$d/guard.log" || {
        echo "COHORT_RESULT=INCONCLUSIVE stage=header_impact name=$name"; exit 8;
      }
    last_candidate_delta=""
    if [[ -s "$d/affected-objects.txt" ]]; then
      # Start with the complete 2064-member selection only. A partially
      # selected prefix may require *GCC* rebuilds under the new headers,
      # whose hashes differ from the pristine GCC object reference. Such
      # mixed selections require their own paired dynamic baseline, and
      # must never inherit this full-selection repair.
      if [[ "$mode" != prefix || "$count" != "$n" ]]; then
        echo "COHORT_RESULT=INCONCLUSIVE stage=dynamic_header_requires_full_selection name=$name"
        exit 8
      fi
      mapfile -t affected <"$d/affected-objects.txt"
      echo "COHORT_HEADER_RECONCILE=START name=$name owners=${#affected[@]}"
      regen_started=$(date +%s%N)
      python3 "$repo/tools/ci/linux-runtime-kbuild-regeneration-guard-v1.py" \
        --mode snapshot --out "$out" --evidence "$d/generated-after-first-link.json" \
        >>"$d/guard.log"
      : >"$d/rebuild-success.txt"
      : >"$d/rebuild-hashes.sha256"
      # Replay exactly the pinned GCC Kbuild C compilation commands with
      # current regenerated header bytes, but DO NOT recurse into Kbuild:
      # a second Kbuild invocation may mutate vDSO producer inputs and
      # regenerate vdso-offsets.h yet again.
      if ! python3 "$repo/tools/ci/linux-runtime-kbuild-regeneration-guard-v1.py" \
        --mode replay --out "$out" --golden-out "$gold_snapshot" \
        --evidence "$d/generated-after-first-link.json" \
        --selected "$d/affected-objects.txt" \
        --wrapper "$repo/tests/external/linux/stage2_kbuild_cc.sh" \
        --minic "$minic" --success-trace "$d/rebuild-success.txt" \
        --digest-trace "$d/rebuild-hashes.sha256" \
        >"$d/rebuild.log" 2>&1; then
        echo "COHORT_RESULT=INCONCLUSIVE stage=dependent_minic_direct_replay name=$name"
        tail -n 75 "$d/rebuild.log"
        exit 8
      fi
      python3 "$repo/tools/ci/linux-runtime-minic-route-audit-v1.py" \
        --objects-file "$d/affected-objects.txt" \
        --success-trace "$d/rebuild-success.txt" \
        --object-digest-trace "$d/rebuild-hashes.sha256" \
        --candidates "$out" | tee -a "$d/guard.log" || {
          echo "COHORT_RESULT=INCONCLUSIVE stage=dependent_minic_provenance name=$name"
          exit 8
        }
      # Rebuilding dependents must not shift the generated vDSO header again.
      python3 "$repo/tools/ci/linux-runtime-kbuild-regeneration-guard-v1.py" \
        --mode stable --out "$out" --evidence "$d/generated-after-first-link.json" \
        >>"$d/guard.log" || {
          echo "COHORT_RESULT=INCONCLUSIVE stage=dependent_minic_header_instability"
          exit 8
        }
      mkdir -p "$d/reconciled"
      for target in "${affected[@]}"; do
        mkdir -p "$d/reconciled/$(dirname "$target")"
        cp -a -- "$out/$target" "$d/reconciled/$target"
        # Verify RISC-V architecture, ET_REL and ABI flags without assuming
        # GCC and MiniC object bytes or section layout are interchangeable.
        python3 - "$ev/gcc/$target" "$d/reconciled/$target" <<'PY_RISCV_DYNAMIC'
import struct,sys
from pathlib import Path
a,b=[Path(x).read_bytes()[:52] for x in sys.argv[1:]]
for h in (a,b):
    assert len(h)==52 and h[:6]==b"\x7fELF\x02\x01"
    assert struct.unpack_from("<H",h,16)[0]==1
    assert struct.unpack_from("<H",h,18)[0]==243
assert a[48:52]==b[48:52], "reconciled MiniC RISC-V ABI differs from GCC"
PY_RISCV_DYNAMIC
        cp -a -- "$ev/gcc/$target.cmd" "$out/$(dirname "$target")/.${target##*/}.cmd"
        touch -- "$out/$target"
      done
      last_candidate_delta="$d/reconciled"
      regen_finished=$(date +%s%N)
      echo "COHORT_TIMING trial=$name stage=minic_header_reconcile elapsed_ms=$(((regen_finished-regen_started)/1000000)) owners=${#affected[@]}"
      printf 'timing_%s_header_reconcile_ms=%s\n' "$name" "$(((regen_finished-regen_started)/1000000))" >>"$ev/identity.txt"
      second_started=$(date +%s%N)
      if ! make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
          -j4 Image >"$d/link-reconciled.log" 2>&1; then
        echo "COHORT_LINK=FAIL name=$name stage=gnu_link_after_header_reconcile"
        tail -n 65 "$d/link-reconciled.log"
        TRIAL_VERDICT=LINK_FAIL
        return 0
      fi
      python3 "$repo/tools/ci/linux-runtime-kbuild-regeneration-guard-v1.py" \
        --mode stable --out "$out" --evidence "$d/generated-after-first-link.json" \
        >>"$d/guard.log" || {
          echo "COHORT_RESULT=INCONCLUSIVE stage=second_link_header_instability"
          tail -n 40 "$d/link-reconciled.log"
          exit 8
        }
      second_finished=$(date +%s%N)
      echo "COHORT_TIMING trial=$name stage=gnu_reconciled_link elapsed_ms=$(((second_finished-second_started)/1000000))"
      printf 'timing_%s_reconciled_link_ms=%s\n' "$name" "$(((second_finished-second_started)/1000000))" >>"$ev/identity.txt"
      echo "COHORT_HEADER_RECONCILE=PASS name=$name owners=${#affected[@]} immutable_full_candidate_cache=true"
    else
      # Stable generated headers: permit replacing only the MiniC owners
      # demonstrably overwritten by a logged GNU CC step. A second link
      # then rechecks identities below.
      repair_result=$(python3 "$repo/tools/ci/linux-runtime-kbuild-regeneration-guard-v1.py" \
        --mode repair --out "$out" --evidence "$d/generated-before.json" \
        --selected "$d/selected-objects.txt" --minic "$ev/minic" \
        --link-log "$d/link.log" --golden-out "$gold_snapshot") || {
          echo "COHORT_RESULT=INCONCLUSIVE stage=stable_header_guard name=$name"
          exit 8
        }
      printf '%s\n' "$repair_result" | tee -a "$d/guard.log"
      if [[ "$repair_result" == *"COHORT_KBUILD_REPAIR=APPLIED"* ]]; then
        second_started=$(date +%s%N)
        if ! make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
          -j4 Image >"$d/link-repaired.log" 2>&1; then
          echo "COHORT_LINK=FAIL name=$name stage=guarded_second_link"
          tail -n 70 "$d/link-repaired.log"
          TRIAL_VERDICT=LINK_FAIL
          return 0
        fi
        second_finished=$(date +%s%N)
        echo "COHORT_TIMING trial=$name stage=gnu_guarded_relink elapsed_ms=$(((second_finished-second_started)/1000000))"
        echo "COHORT_LINK=GUARDED_RELINK name=$name reason=selected_minic_replaced_by_kbuild"
      fi
    fi
    last_link_source_dir="$d"
    last_selection="$selection"
  else
    # Same object selection, different runtime oracle (FAST -> FULL):
    # skip a redundant link. Image bytes and all object IDs stay fixed.
    echo "COHORT_LINK=REUSED name=$name selection=$selection" | tee "$d/link.log"
  fi
  for ((i=0;i<n;i++)); do
    target=${objects[i]}
    selected=0
    case "$mode" in
      prefix) if ((i<count)); then selected=1; fi ;;
      single) if ((i==count)); then selected=1; fi ;;
      all_except) if ((i!=count)); then selected=1; fi ;;
    esac
    if ((selected)); then
      gold="$ev/minic/$target"
      if [[ -n "$last_candidate_delta" && -f "$last_candidate_delta/$target" ]]; then
        gold="$last_candidate_delta/$target"
      fi
    else
      gold="$ev/gcc/$target"
    fi
    cmp -s "$out/$target" "$gold" || {
      echo "COHORT_IDENTITY=FAIL trial=$name target=$target"; exit 6;
    }
  done
  # Non-C objects cannot all be compared byte-for-byte with the GNU
  # golden reference: changing a verified MiniC .o legitimately changes its
  # GNU-objcopy .pi.o / EFI .stub.o derivatives and native vDSO envelope.
  # Accept ONLY a derived RISC-V ELF with matching ABI, an explicit GNU
  # transform record, and a verified selected C owner as its input. Nothing
  # else in the GCC object universe is allowed to drift.
  python3 - "$out" "$prov/gcc-objects.sha256" "$objects_file" \
    "$d/selected-objects.txt" "$last_link_source_dir" "$gold_snapshot" <<'PY_GNU_NONSELECTED'
from pathlib import Path
import hashlib, re, struct, sys

out,manifest,universe,selected,link_dir,golden=map(Path,sys.argv[1:])
all_c={x.strip() for x in universe.read_text().splitlines()
       if x.strip() and not x.lstrip().startswith("#")}
chosen={x.strip() for x in selected.read_text().splitlines()
        if x.strip() and not x.lstrip().startswith("#")}
if not chosen <= all_c:
    raise SystemExit("COHORT_GNU_CONTAMINATION=FAIL selected_not_subset_of_full_C")
linked="\n".join((link_dir/name).read_text()
                 for name in ("link.log","link-reconciled.log","link-repaired.log")
                 if (link_dir/name).is_file())
generated_links={
    "init/version-timestamp.o",
    "vmlinux.o",
    ".vmlinux.export.o",
}
def transient_link(name):
    return (name in generated_links or
            re.fullmatch(r"\.tmp_vmlinux\.kallsyms[1-3]\.o",name) is not None or
            name==".tmp_vmlinux.btf.o")
def emitted(step,name):
    return re.search(r"(?m)^\s+"+step+r"\s+"+re.escape(name)+r"\s*$",linked) is not None
def abi_same(name):
    original=(golden/name).read_bytes()[:52]
    derived=(out/name).read_bytes()[:52]
    for head in (original,derived):
        if (len(head)!=52 or head[:6]!=b"\x7fELF\x02\x01" or
            struct.unpack_from("<H",head,16)[0]!=1 or
            struct.unpack_from("<H",head,18)[0]!=243):
            raise SystemExit("COHORT_GNU_DERIVED_ABI=FAIL object="+name)
    if original[48:52]!=derived[48:52]:
        raise SystemExit("COHORT_GNU_DERIVED_ABI=FAIL object="+name)
def derivation(name):
    if name.startswith("arch/riscv/kernel/pi/") and name.endswith(".pi.o"):
        owner=name[:-5]+".o"
        if owner in chosen and emitted("OBJCOPY",name):
            return owner
    if name.startswith("drivers/firmware/efi/libstub/") and name.endswith(".stub.o"):
        owner=name[:-7]+".o"
        if owner in chosen and emitted("STUBCPY",name):
            return owner
    if name=="arch/riscv/purgatory/kexec-purgatory.o":
        owners={"arch/riscv/purgatory/purgatory.o",
                "arch/riscv/purgatory/ctype.o",
                "arch/riscv/purgatory/sha256.o",
                "arch/riscv/purgatory/string.o"}
        if (owners <= chosen and
            emitted("LD","arch/riscv/purgatory/purgatory.ro") and
            emitted("LD","arch/riscv/purgatory/purgatory.chk") and
            emitted("AS",name)):
            return "purgatory:" + ",".join(sorted(owners))
    if name=="arch/riscv/kernel/vdso/vdso.o":
        owners={"arch/riscv/kernel/vdso/hwprobe.o",
                "arch/riscv/kernel/vdso/vgettimeofday.o"}
        if (owners <= chosen and
            emitted("VDSOLD","arch/riscv/kernel/vdso/vdso.so.dbg") and
            emitted("OBJCOPY","arch/riscv/kernel/vdso/vdso.so") and
            emitted("AS",name)):
            return "native-vdso:" + ",".join(sorted(owners))
    return None
derived_evidence=[]
for line in manifest.read_text().splitlines():
    digest,name=line.strip().split(None,1)
    if name in all_c or transient_link(name):
        continue
    p=out/name
    if p.is_file() and hashlib.sha256(p.read_bytes()).hexdigest()==digest:
        continue
    owner=derivation(name)
    if owner is None or not p.is_file():
        raise SystemExit("COHORT_GNU_CONTAMINATION=FAIL object="+name)
    # All leaf C owners have already passed exact MiniC SHA256 validation.
    # Check that the generated object is still a valid RISC-V ET_REL with
    # golden-compatible ABI flags; record which explicit owner explains it.
    abi_same(name)
    derived_evidence.append((name,owner))
    print(f"COHORT_GNU_DERIVED=PASS object={name} owner={owner}")
print(f"COHORT_GNU_UNSELECTED=PASS derived={len(derived_evidence)} "
      f"pinned_elf_and_transform_identity=true")
PY_GNU_NONSELECTED
  image_sha=$(sha256sum "$out/arch/riscv/boot/Image" | cut -d' ' -f1)
  cp -a "$out/arch/riscv/boot/Image" "$d/Image"
  candidate_image_bytes=$(stat -c '%s' "$d/Image")
  golden_image_bytes=$(stat -c '%s' "$gold_snapshot/arch/riscv/boot/Image")
  echo "COHORT_IMAGE_BYTES trial=$name candidate=$candidate_image_bytes golden=$golden_image_bytes ratio=$(python3 -c 'import sys;print(round(int(sys.argv[1])/max(1,int(sys.argv[2])),3))' "$candidate_image_bytes" "$golden_image_bytes")"
  # The image may be too large for QEMU's pinned RISC-V -initrd placement
  # even if the guest has enough total DRAM. Save *measured* ELF section
  # sizes for later MiniC codegen-bloat analysis; never guess from Image alone.
  if [[ -s "$out/vmlinux" ]]; then
    /usr/bin/riscv64-linux-gnu-size -A "$out/vmlinux" >"$d/vmlinux-sections.txt" 2>&1 || true
    python3 - "$d/vmlinux-sections.txt" <<'PY_VMLINUX_SIZE'
from pathlib import Path
import sys
rows=[]
for line in Path(sys.argv[1]).read_text().splitlines():
    fields=line.split()
    if len(fields)==3:
        try:
            size=int(fields[1])
        except ValueError:
            continue
        if size>0:
            rows.append((size, fields[0]))
for size,name in sorted(rows,reverse=True)[:12]:
    print(f"COHORT_VMLINUX_LARGEST_SECTION name={name} bytes={size}")
PY_VMLINUX_SIZE
    # Preserve source symbol names to isolate codegen amplification without
    # recompiling any C input. Never treat ET_REL file bytes as text size.
    /usr/bin/riscv64-linux-gnu-nm -S --size-sort "$out/vmlinux" \
      >"$d/vmlinux-symbol-sizes.txt" 2>&1 || true
    python3 - "$d/vmlinux-symbol-sizes.txt" <<'PY_LARGEST_SYMBOLS'
import re,sys
from pathlib import Path
rows=[]
for line in Path(sys.argv[1]).read_text(errors="replace").splitlines():
    m=re.match(r"^[0-9a-fA-F]+\s+([0-9a-fA-F]+)\s+([A-Za-z])\s+(.+)$",line)
    if m:
        rows.append((int(m.group(1),16),m.group(2),m.group(3)))
for size,kind,name in sorted(rows,reverse=True)[:24]:
    print(f"COHORT_VMLINUX_LARGEST_SYMBOL bytes={size} kind={kind} name={name}")
PY_LARGEST_SYMBOLS
  fi
  python3 - "$ev/gcc" "$ev/minic" "$objects_file" <<'PY_LARGEST_TUS'
from pathlib import Path
import sys
a,b,manifest=map(Path,sys.argv[1:])
rows=[]
for n in manifest.read_text().splitlines():
    n=n.strip()
    if not n or n.startswith("#"):
        continue
    ga=(a/n).stat().st_size
    mi=(b/n).stat().st_size
    rows.append((mi-ga,mi,ga,n))
for delta,mi,ga,name in sorted(rows,reverse=True)[:22]:
    print(f"COHORT_LARGEST_TU_DELTA name={name} gcc_bytes={ga} minic_bytes={mi} extra_bytes={delta} ratio={mi/max(1,ga):.3f}")
print(f"COHORT_TU_BYTES_SUM gcc={sum(r[2] for r in rows)} minic={sum(r[1] for r in rows)} count={len(rows)}")
PY_LARGEST_TUS
  verdict=INCONCLUSIVE
  if [[ "$mode" != prefix || "$count" -gt 0 ]] && [[ "$image_sha" == "$gnu_image_sha" ]]; then
    echo "COHORT_IMAGE=UNCHANGED name=$name"
  else
    qemu_started=$(date +%s%N)
    set +e
    LINUX_IMAGE="$d/Image" INITRAMFS="$ev/runtime-initramfs.cpio.gz" \
      BUILD_DIR="$d/qemu" LINUX_RELEASE=6.6.143 \
      LINUX_RUNTIME_PROFILE="$profile" QEMU_TIMEOUT_SECONDS=45 \
      bash "$repo/tests/external/linux/runtime_boot.sh" >"$d/runtime.log" 2>&1
    rc=$?
    set -e
    qemu_finished=$(date +%s%N)
    echo "COHORT_TIMING trial=$name stage=qemu_$profile elapsed_ms=$(((qemu_finished-qemu_started)/1000000)) rc=$rc"
    printf 'timing_%s_qemu_%s_ms=%s\n' "$name" "$profile" "$(((qemu_finished-qemu_started)/1000000))" >>"$ev/identity.txt"
    if ((rc==0)); then verdict=PASS
    elif grep -REqi 'Linux version 6\.6\.143|Kernel panic|Oops:|Unable to handle|BUG:' "$d/qemu" 2>/dev/null; then verdict=FAIL
    fi
    # A very large (but correctly linked) kernel Image can overlap the initrd
    # at QEMU's ROM loader address BEFORE the guest executes one instruction.
    # This is neither a compiler runtime FAIL nor proof that boot succeeds.
    # Probe the *same immutable* Image WITHOUT initrd to expose the earliest
    # real guest frontier. Keep the normal full initramfs oracle unchanged.
    if grep -Fq 'Some ROM regions are overlapping' "$d/runtime.log" && \
       grep -Fq 'runtime-initramfs.cpio.gz' "$d/runtime.log"; then
      echo "COHORT_QEMU_LOAD_OVERLAP=CONFIRMED trial=$name image_bytes=$candidate_image_bytes initrd_rom_overlap=true"
      early_started=$(date +%s%N)
      early_rc=0
      python3 "$repo/tools/ci/linux-runtime-qmp-pc-sampler-v1.py" --self-test || {
        echo "COHORT_QMP_EARLY=FAIL sampler_selftest"
        exit 8
      }
      python3 "$repo/tools/ci/linux-runtime-qmp-pc-sampler-v1.py" \
        --image "$d/Image" --output-dir "$d" --timeout-seconds 25 \
        >"$d/qmp-probe.log" 2>&1 || early_rc=$?
      cat "$d/qmp-probe.log" >>"$d/runtime.log"
      grep -E 'COHORT_QMP_EARLY' "$d/qmp-probe.log" || true
      # Resolve the actual repeatedly observed guest PC using the linked
      # SAME-TRIAL vmlinux symbols (not guessed GCC offsets). The 2064
      # object cache remains byte-identical throughout this diagnostic.
      if [[ -s "$d/qemu-registers.jsonl" && -s "$out/vmlinux" ]]; then
        python3 - "$d/qemu-registers.jsonl" "$out/vmlinux" <<'PY_EARLY_SYMBOLS' | tee "$d/qmp-symbols.log"
import bisect,json,subprocess,sys
from pathlib import Path
samples=Path(sys.argv[1])
vmlinux=Path(sys.argv[2])
nm=subprocess.run(["/usr/bin/riscv64-linux-gnu-nm","-n","--defined-only",str(vmlinux)],
                  text=True,capture_output=True,check=False)
if nm.returncode:
    print(f"COHORT_QMP_SYMBOL=INCONCLUSIVE nm_rc={nm.returncode}")
    raise SystemExit(0)
pairs=[]
for line in nm.stdout.splitlines():
    fields=line.split(None,2)
    if len(fields)==3 and fields[1] in {"t","T","w","W"}:
        try:
            pairs.append((int(fields[0],16),fields[2]))
        except ValueError:
            continue
pairs.sort()
addresses=[x[0] for x in pairs]
last_pcs=[]
for line in samples.read_text().splitlines():
    sample=json.loads(line)
    at=sample["at_seconds"]
    regs=sample.get("registers",{})
    for register in ("pc","mepc","sepc"):
        raw=regs.get(register)
        if not raw or not addresses:
            continue
        try:
            addr=int(raw,16)
        except ValueError:
            continue
        if register=="pc":
            last_pcs.append(addr)
        pos=bisect.bisect_right(addresses,addr)-1
        if pos<0:
            print(f"COHORT_QMP_SYMBOL at_s={at} reg={register} addr=0x{addr:x} symbol=BELOW_LINKED_TEXT")
        else:
            base,symbol=pairs[pos]
            print(f"COHORT_QMP_SYMBOL at_s={at} reg={register} addr=0x{addr:x} "
                  f"symbol={symbol} offset=0x{addr-base:x}")
if len(last_pcs)>=2 and last_pcs[-1]==last_pcs[-2]:
    print(f"COHORT_QMP_STABLE_PC=OBSERVED samples={len(last_pcs)} addr=0x{last_pcs[-1]:x}")
PY_EARLY_SYMBOLS
        cat "$d/qmp-symbols.log" >>"$d/runtime.log"
      fi
      early_finished=$(date +%s%N)
      echo "COHORT_TIMING trial=$name stage=qemu_early_noinitrd elapsed_ms=$(((early_finished-early_started)/1000000)) rc=$early_rc"
      printf 'timing_%s_qemu_early_noinitrd_ms=%s\n' "$name" "$(((early_finished-early_started)/1000000))" >>"$ev/identity.txt"
      if grep -Eq 'Linux version 6\.6\.143|Kernel panic|Oops:|Unable to handle|BUG:' "$d/qemu-early-noinitrd.log"; then
        echo "COHORT_KERNEL_EARLY_FRONTIER=GUEST_REACHED trial=$name"
      elif grep -Eqi 'OpenSBI|Platform Name|Firmware Base' "$d/qemu-early-noinitrd.log"; then
        echo "COHORT_KERNEL_EARLY_FRONTIER=FIRMWARE_ONLY trial=$name"
      else
        echo "COHORT_KERNEL_EARLY_FRONTIER=NO_FIRMWARE_OR_KERNEL_BANNER trial=$name"
      fi
      # The golden kernel has already passed the complete initramfs/P1
      # oracles, but compare it AGAIN under this exact kernel-only QEMU
      # invocation: absence of a MiniC banner is meaningful only when GNU
      # reaches the same stage in the same runner and time window.
      golden_early_rc=0
      timeout --signal=TERM 15s qemu-system-riscv64 \
        -M virt -cpu max -m 512M -smp 1 -nographic -no-reboot \
        -bios default -kernel "$gold_snapshot/arch/riscv/boot/Image" \
        -append 'console=ttyS0 earlycon=sbi loglevel=8 panic=-1' \
        </dev/null >"$d/qemu-golden-noinitrd.log" 2>&1 || golden_early_rc=$?
      if grep -Fq 'Linux version 6.6.143' "$d/qemu-golden-noinitrd.log"; then
        echo "COHORT_GCC_EARLY_REFERENCE=LINUX_BANNER trial=$name"
      else
        echo "COHORT_GCC_EARLY_REFERENCE=NO_LINUX_BANNER trial=$name rc=$golden_early_rc"
      fi
      echo "COHORT_GCC_EARLY_LOG_END"
      tail -n 14 "$d/qemu-golden-noinitrd.log"
      echo "COHORT_GCC_EARLY_LOG_END"
      {
        echo "COHORT_GCC_EARLY_REFERENCE_BANNER=$(grep -Fqc 'Linux version 6.6.143' "$d/qemu-golden-noinitrd.log" || true)"
        tail -n 14 "$d/qemu-golden-noinitrd.log"
      } >>"$d/runtime.log"
      echo "COHORT_KERNEL_EARLY_LOG_END"
      tail -n 75 "$d/qemu-early-noinitrd.log"
      echo "COHORT_KERNEL_EARLY_LOG_END"
      # Persist evidence within an existing uploaded artifact path. Retain
      # INCONCLUSIVE: a kernel-only probe cannot satisfy the PID1/P1 oracle.
      cat "$d/qemu-early-noinitrd.log" >>"$d/runtime.log"
      verdict=INCONCLUSIVE
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
        echo "COHORT_RESULT=PASS candidate_objects=$n scope=$cohort_scope same_config=true"
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
# A failing prefix is a clue, not proof of a single owner. Confirm both
# directions on the same GCC baseline and existing MiniC object pool.
trial "candidate_single_$hi" "$((hi-1))" fast single
single_verdict="$TRIAL_VERDICT"
trial "candidate_all_except_$hi" "$((hi-1))" fast all_except
except_verdict="$TRIAL_VERDICT"
echo "COHORT_CAUSALITY candidate=$candidate prefix_fail_kind=$fail_kind single=$single_verdict all_except=$except_verdict"
if [[ "$single_verdict" == "$fail_kind" && "$except_verdict" == PASS ]]; then
  echo "COHORT_RESULT=SINGLE_OBJECT_SUSPECT candidate=$candidate"
else
  echo "COHORT_RESULT=INTERACTION_OR_NONMONOTONE candidate=$candidate"
fi
exit 9
