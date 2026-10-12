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
if [[ -e "$gold_snapshot" ]]; then
  # A compile-only invocation deliberately keeps its immutable golden copy
  # for the next verify-only invocation on the SAME CI runner. Reconstruct
  # the snapshot from the restored, identity-checked GCC output instead of
  # treating that legitimate reuse as a second producer collision.
  if [[ "${COHORT_ACTION:-all}" != verify ]]; then
    echo "COHORT_GOLDEN=ERROR stale_snapshot_during_nonverify" >&2
    exit 3
  fi
  test -s "$out/arch/riscv/boot/Image"
  [[ $(sha256sum "$out/arch/riscv/boot/Image" | cut -d' ' -f1) == "$gnu_image_sha" ]] || {
    echo "COHORT_GOLDEN=ERROR verify_image_not_certified_reference" >&2
    exit 3
  }
  rm -rf -- "$gold_snapshot"
  echo "COHORT_GOLDEN_SNAPSHOT_RECONSTRUCT=PASS verify_only=1"
fi
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
  if ((n==1)) && [[ "${objects[0]}" == kernel/dma/direct.o ]]; then
    export MINIC_DMA_ZERO_TRACE=1
    export MINIC_DMA_IF_TRACE=1
  elif ((n==1)) && [[ "${objects[0]}" == mm/shmem.o ]]; then
    export MINIC_SHMEM_IF_TRACE=1
  fi
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
  keep_dma_inputs=0
  if ((n == 1)) && [[ "${objects[0]}" == kernel/dma/direct.o || "${objects[0]}" == mm/shmem.o ]]; then
    keep_dma_inputs=1
  fi
  MINIC="$probe_minic" REAL_CC="$probe_gcc" \
    MINIC_KEEP_INTERMEDIATES="$keep_dma_inputs" MINIC_PRESERVE_FAILURE_INPUTS=1 \
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
        MINIC_KEEP_INTERMEDIATES="$keep_dma_inputs" MINIC_PRESERVE_FAILURE_INPUTS=1 \
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
  if ((n==1)) && [[ "${objects[0]}" == kernel/dma/direct.o ]]; then
    zero_trace="$out/kernel/dma/direct.minic-stage2.minic.stderr"
    if [[ -s "$zero_trace" ]]; then
      cp -a "$zero_trace" "$ev/dma-zero-facts.log"
      grep 'DMA_ZERO_PROOF' "$zero_trace" | tail -n 64 || true
      grep 'DMA_IF_TRACE' "$zero_trace" | tail -n 160 || true
    fi
    dma_input="$out/kernel/dma/direct.minic-stage2.i"
    if [[ -s "$dma_input" ]]; then
      # Human-readable exact preprocessed context around the DMA branch,
      # without uploading the large source to every normal run.
      python3 - "$dma_input" "$ev/dma-kernel-source.txt" <<'PY_DMA_CONTEXT'
from pathlib import Path
import sys
lines=Path(sys.argv[1]).read_text(errors="replace").splitlines()
needle="arch_dma_set_uncached("
positions=[i for i,x in enumerate(lines) if needle in x]
regions=set()
for k in positions:
    for i in range(max(0,k-100),min(len(lines),k+25)):
        regions.add(i)
with Path(sys.argv[2]).open("w") as out:
    for i in sorted(regions):
        out.write(f"{i+1}: {lines[i]}\\n")
print("DMA_PREPROCESSED_REF_CONTEXT=" + str(len(positions)) +
      " lines=" + str(len(regions)))
PY_DMA_CONTEXT
      tail -n 130 "$ev/dma-kernel-source.txt"
    fi
  fi
fi
# One authenticated Linux shmem diagnostic, not a second owner producer.
# Preserve GNU -E input and current MiniC -S for exact failing assertion
# names, even when an immutable .o was restored from the candidate cache.
if ((n == 1)) && [[ "${objects[0]}" == mm/shmem.o ]]; then
  shmem_i="$out/mm/shmem.minic-stage2.i"
  shmem_s="$out/mm/shmem.minic-stage2.s"
  if ((cached == 1)); then
    # No full cohort recompilation: explicitly rebuild ONLY this source.
    rm -f "$out/mm/shmem.o" "$out/mm/.shmem.o.cmd"
    MINIC="$minic" REAL_CC=/usr/bin/riscv64-linux-gnu-gcc \
      MINIC_KEEP_INTERMEDIATES=1 MINIC_PRESERVE_FAILURE_INPUTS=1 \
      make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
        CC="$repo/tests/external/linux/stage2_kbuild_cc.sh" -j1 mm/shmem.o \
        >"$ev/shmem-diagnostic-replay.log" 2>&1 || {
          echo "SHMEM_SOURCE_REPLAY=FAIL compile_error"
          tail -n 70 "$ev/shmem-diagnostic-replay.log"
          exit 4
        }
    test -s "$out/mm/shmem.o" || {
      echo "SHMEM_SOURCE_REPLAY=FAIL empty_object"; exit 4;
    }
    if ! cmp -s "$out/mm/shmem.o" "$ev/minic/mm/shmem.o"; then
      echo "SHMEM_SOURCE_REPLAY=FAIL candidate_object_bytes_mismatch"
      exit 4
    fi
    echo "SHMEM_SOURCE_REPLAY=PASS exact_candidate_bytes_unchanged=1"
  fi
  shmem_stderr="$out/mm/shmem.minic-stage2.minic.stderr"
  if [[ -s "$shmem_stderr" ]]; then
    # Keep chronological interleaving of known facts and CFG entry/merge.
    grep -E 'SHMEM_(IF_TRACE|IF_FLOW|FACT_EVENT)' "$shmem_stderr" | tail -n 180 || true
  fi
  test -s "$shmem_i" && test -s "$shmem_s"
  python3 - "$shmem_i" "$shmem_s" "$ev/shmem-assert-context.txt" <<'PY_SHMEM_ASSERT'
from pathlib import Path
import re,sys
pre=Path(sys.argv[1]).read_text(errors="replace").splitlines()
asm=Path(sys.argv[2]).read_text(errors="replace").splitlines()
out=[]
regex=re.compile(r"__compiletime_assert_(?:395|396|397|398)\b")
for title,lines in (("GNU_PREPROCESSED",pre),("MINIC_ASSEMBLY",asm)):
    match_indices=[i for i,s in enumerate(lines) if regex.search(s)]
    out.append(f"SHMEM_TARGET_CAPTURE stage={title} matches={len(match_indices)}")
    if title == "MINIC_ASSEMBLY":
        # Locate the containing emitted function, not merely the reloc target.
        for n in match_indices[:10]:
            prev_types=[
                (k,lines[k][:250]) for k in range(max(0,n-3000),n)
                if (".type" in lines[k] and "@function" in lines[k]) or (lines[k].endswith(":") and lines[k][:1].isalpha())
            ]
            out.append(f"SHMEM_ASM_OWNER assert_line={n+1} preceding_labels={prev_types[-7:]}")
    else:
        for n in match_indices[:4]:
            owners=[(k+1,lines[k][:250]) for k in range(max(0,n-300),n)
                    if "shmem_alloc_hugefolio" in lines[k] or
                       "shmem_alloc_and_acct_folio" in lines[k] or
                       "shmem_alloc_folio(" in lines[k]]
            out.append(f"SHMEM_SOURCE_OWNER assert_line={n+1} nearby={owners[-12:]}")
    for n in match_indices[:18]:
        symbols=sorted(set(regex.findall(lines[n])))
        out.append(f"SHMEM_TARGET_CAPTURE stage={title} center={n+1} symbols={','.join(symbols)}")
        for k in range(max(0,n-4),min(len(lines),n+5)):
            out.append(f" {k+1}: {lines[k][:1200]}")
Path(sys.argv[3]).write_text("\n".join(out)+"\n")
print("\n".join(out)[:36000])
PY_SHMEM_ASSERT
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
# Before overlaying 2064 objects, capture just the verified GNU
# preprocessor input for the known earlycon runtime regression and the
# SAME MiniC binary's assembly. This does not recompile any candidate or
# change the golden Kbuild fixture. Evidence goes into the EXISTING artifact.
if [[ "$cohort_scope" == FULL_C_UNIVERSE ]]; then
  earlycon_capture="$ev/trials/full_all/earlycon-source"
  if ! python3 "$repo/tools/ci/linux-runtime-kbuild-regeneration-guard-v1.py" \
       --mode capture --out "$out" --golden-out "$gold_snapshot" \
       --minic "$minic" --evidence "$earlycon_capture"; then
    echo "COHORT_EARLYCON_CAPTURE=INCONCLUSIVE no_candidate_or_oracle_changed=true"
  fi
fi
# Keep the source capture itself independently testable and fast: a
# developer-tagged receiver may authenticate cached 2064 objects and collect
# the ONE exact GCC .i / MiniC .s without a new GNU Image or QEMU run.
# This is diagnostic-only; it is NOT a Linux boot / P1 certificate.
if [[ "${COHORT_CAPTURE_ONLY:-0}" == 1 ]]; then
  test -s "$ev/trials/full_all/earlycon-source/earlycon-exact.i"
  test -s "$ev/trials/full_all/earlycon-source/earlycon-minic.s"
  echo "COHORT_CAPTURE_ONLY=PASS candidates=$n reused=true runtime_oracle=NOT_RUN"
  exit 0
fi
# One-object regression isolation: keep 2063 *old* authenticated MiniC
# candidates, recompile ONLY earlycon.o with the current fixed compiler.
# This is not a full-current-compiler Linux certification.
if [[ "${COHORT_EARLYCON_HOTFIX:-0}" == 1 ]]; then
  [[ "$cohort_scope" == FULL_C_UNIVERSE && "$n" == 2064 && "${COHORT_ACTION:-all}" == verify ]] || {
    echo "EARLYCON_HOTFIX=ERROR require_frozen_full_2064_and_verify" >&2; exit 8;
  }
  hotfix="$ev/earlycon-hotfix"
  target=drivers/tty/serial/earlycon.o
  mkdir -p "$hotfix"
  printf '%s\n' "$target" >"$hotfix/selected.txt"
  # The cache's immutable 2064-entry SHA manifest was verified above.
  # Save the old candidate and independently attest the new one.
  cp -a "$ev/minic/$target" "$hotfix/earlycon-before.o"
  old_sha=$(sha256sum "$hotfix/earlycon-before.o" | cut -d' ' -f1)
  cc_sha=$(sha256sum "$(dirname "$minic")/minic-cc" | cut -d' ' -f1)
  echo "EARLYCON_HOTFIX=START old_2063=frozen previous_earlycon=$old_sha new_minic_cc=$cc_sha"
  python3 "$repo/tools/ci/linux-runtime-kbuild-regeneration-guard-v1.py" \
    --mode replay --out "$out" --golden-out "$gold_snapshot" \
    --evidence "$hotfix/replay.log" --selected "$hotfix/selected.txt" \
    --wrapper "$repo/tests/external/linux/stage2_kbuild_cc.sh" \
    --minic "$minic" --success-trace "$hotfix/success.txt" \
    --digest-trace "$hotfix/digests.txt" | tee "$hotfix/replay.log"
  python3 "$repo/tools/ci/linux-runtime-minic-route-audit-v1.py" \
    --objects-file "$hotfix/selected.txt" \
    --success-trace "$hotfix/success.txt" \
    --object-digest-trace "$hotfix/digests.txt" \
    --candidates "$out" | tee "$hotfix/route-audit.log"
  python3 - "$ev/gcc/$target" "$ev/minic/$target" "$out/$target" <<'PY_HOTFIX_ABI'
import struct, sys
from pathlib import Path
heads=[Path(p).read_bytes()[:52] for p in sys.argv[1:]]
for h in heads:
    assert len(h)==52 and h[:6]==b"\x7fELF\x02\x01", "non-RV64 ELF"
    assert struct.unpack_from("<H",h,16)[0]==1, "not ET_REL"
    assert struct.unpack_from("<H",h,18)[0]==243, "not RISC-V"
assert len({h[48:52] for h in heads})==1, "RISC-V ABI flags mismatch"
print("EARLYCON_HOTFIX_ABI=PASS gcc_old_minic_new_minic")
PY_HOTFIX_ABI
  new_sha=$(sha256sum "$out/$target" | cut -d' ' -f1)
  [[ "$old_sha" != "$new_sha" ]] || {
    echo "EARLYCON_HOTFIX=FAIL new_object_identical_to_bad_old_candidate" >&2; exit 8;
  }
  cp -a "$out/$target" "$ev/minic/$target"
  printf 'old_earlycon_sha256=%s\nnew_earlycon_sha256=%s\nnew_minic_cc_sha256=%s\n' \
    "$old_sha" "$new_sha" "$cc_sha" >"$hotfix/provenance.txt"
  # Verify all 2063 unrelated candidate objects against the frozen manifest.
  python3 - "$ev/minic-manifest.sha256" "$ev" "$target" "$new_sha" <<'PY_HOTFIX_INTEGRITY'
import hashlib, sys
from pathlib import Path
manifest, root, changed, current = Path(sys.argv[1]),Path(sys.argv[2]),sys.argv[3],sys.argv[4]
count=0
for line in manifest.read_text().splitlines():
    digest, name = line.split(None,1)
    path=root/name
    observed=hashlib.sha256(path.read_bytes()).hexdigest()
    if name == "minic/"+changed:
        assert observed==current and observed!=digest, "earlycon replacement missing"
    else:
        assert observed==digest, "unrelated MiniC candidate mutated: "+name
        count+=1
assert count==2063, f"expected 2063 frozen neighbors, found {count}"
print(f"EARLYCON_HOTFIX_FROZEN_NEIGHBORS=PASS objects={count} new_object=1")
PY_HOTFIX_INTEGRITY
  # All subsequent GNU links must start from the immutable exact GNU tree.
  rm -rf -- "$out"
  cp -a --reflink=auto "$gold_snapshot" "$out"
  echo "EARLYCON_HOTFIX=READY scope=2063_old_minic_plus_1_fixed_earlycon no_full_certificate=true"
  if [[ "${COHORT_PATCH_SUPER_V1:-0}" == 1 ]]; then
    # Follow the GCC-only sget_fc/alloc_super positive control with a true
    # MiniC owner repair. The other 2062 C TUs are untouched and hash checked.
    super_target=fs/super.o
    super_dir="$hotfix/super-repaired"
    mkdir -p "$super_dir"
    printf '%s\n' "$super_target" >"$super_dir/selected.txt"
    old_super_sha=$(sha256sum "$ev/minic/$super_target" | cut -d' ' -f1)
    echo "MINIC_SUPER_REPAIR=START target=$super_target old_sha=$old_super_sha same_compiler=true"
    python3 "$repo/tools/ci/linux-runtime-kbuild-regeneration-guard-v1.py" \
      --mode replay --out "$out" --golden-out "$gold_snapshot" \
      --evidence "$super_dir/replay.log" --selected "$super_dir/selected.txt" \
      --wrapper "$repo/tests/external/linux/stage2_kbuild_cc.sh" \
      --minic "$minic" --success-trace "$super_dir/success.txt" \
      --digest-trace "$super_dir/digests.txt" | tee "$super_dir/replay.log"
    python3 "$repo/tools/ci/linux-runtime-minic-route-audit-v1.py" \
      --objects-file "$super_dir/selected.txt" \
      --success-trace "$super_dir/success.txt" \
      --object-digest-trace "$super_dir/digests.txt" \
      --candidates "$out" | tee "$super_dir/route-audit.log"
    python3 - "$ev/gcc/$super_target" "$ev/minic/$super_target" "$out/$super_target" <<'PY_SUPER_ABI'
import struct,sys
from pathlib import Path
headers=[Path(p).read_bytes()[:52] for p in sys.argv[1:]]
for h in headers:
    assert len(h)==52 and h[:6]==b"\x7fELF\x02\x01"
    assert struct.unpack_from("<H",h,16)[0]==1
    assert struct.unpack_from("<H",h,18)[0]==243
assert len({h[48:52] for h in headers})==1, "fs/super.o ABI flags differ"
print("MINIC_SUPER_REPAIR_ABI=PASS gcc_old_new")
PY_SUPER_ABI
    new_super_sha=$(sha256sum "$out/$super_target" | cut -d' ' -f1)
    cp -a "$out/$super_target" "$ev/minic/$super_target"
    printf 'owner=%s\nold_sha256=%s\nnew_sha256=%s\ncompiler_sha256=%s\n' \
      "$super_target" "$old_super_sha" "$new_super_sha" "$cc_sha" >"$super_dir/provenance.txt"
    python3 - "$ev/minic-manifest.sha256" "$ev" "$target" "$new_sha" "$super_target" "$new_super_sha" <<'PY_PAIR_INTEGRITY'
from pathlib import Path
import hashlib,sys
manifest,root=map(Path,sys.argv[1:3])
new={"minic/"+sys.argv[3]:sys.argv[4],"minic/"+sys.argv[5]:sys.argv[6]}
unchanged=0
for line in manifest.read_text().splitlines():
    digest,name=line.split(None,1)
    observed=hashlib.sha256((root/name).read_bytes()).hexdigest()
    if name in new:
        assert observed==new[name], "repaired object missing "+name
    else:
        assert observed==digest, "frozen C owner modified "+name
        unchanged+=1
assert unchanged==2062, f"expected 2062 frozen neighbors, got {unchanged}"
print(f"MINIC_SUPER_REPAIR_FROZEN_NEIGHBORS=PASS objects={unchanged} current_compiler_owners=2")
PY_PAIR_INTEGRITY
    rm -rf -- "$out"
    cp -a --reflink=auto "$gold_snapshot" "$out"
    echo "MINIC_SUPER_REPAIR=READY old_sha=$old_super_sha new_sha=$new_super_sha changed=$([[ "$old_super_sha" != "$new_super_sha" ]] && echo true || echo false) exact_golden_restored=true"
  fi
  if [[ "${COHORT_PATCH_NET_PAIR:-0}" == 1 ]]; then
    [[ "${COHORT_PATCH_SUPER_V1:-0}" == 1 ]] || {
      echo "MINIC_NET_REPAIR=ERROR requires_current_minic_super"; exit 8;
    }
    net_dir="$hotfix/network-repaired"
    mkdir -p "$net_dir"
    printf '%s\n' net/ipv4/tcp_cong.o net/netlink/genetlink.o >"$net_dir/selected.txt"
    mapfile -t net_targets <"$net_dir/selected.txt"
    printf 'owner\told_sha256\tnew_sha256\tcompiler_sha256\n' >"$net_dir/provenance.tsv"
    echo "MINIC_NET_REPAIR=START owners=2 minic_cc=$cc_sha gcc_swaps=0"
    python3 "$repo/tools/ci/linux-runtime-kbuild-regeneration-guard-v1.py" \
      --mode replay --out "$out" --golden-out "$gold_snapshot" \
      --evidence "$net_dir/replay.log" --selected "$net_dir/selected.txt" \
      --wrapper "$repo/tests/external/linux/stage2_kbuild_cc.sh" \
      --minic "$minic" --success-trace "$net_dir/success.txt" \
      --digest-trace "$net_dir/digests.txt" | tee "$net_dir/replay.log"
    python3 "$repo/tools/ci/linux-runtime-minic-route-audit-v1.py" \
      --objects-file "$net_dir/selected.txt" \
      --success-trace "$net_dir/success.txt" \
      --object-digest-trace "$net_dir/digests.txt" \
      --candidates "$out" | tee "$net_dir/route-audit.log"
    python3 - "$ev/gcc" "$ev/minic" "$out" "$net_dir/selected.txt" <<'PY_NET_ABI'
from pathlib import Path
import struct,sys
gold,old,new,sel=map(Path,sys.argv[1:])
for obj in sel.read_text().splitlines():
    headers=[(base/obj).read_bytes()[:52] for base in (gold,old,new)]
    for h in headers:
        assert len(h)==52 and h[:6]==b"\x7fELF\x02\x01", obj
        assert struct.unpack_from("<H",h,16)[0]==1, obj
        assert struct.unpack_from("<H",h,18)[0]==243, obj
    assert len({h[48:52] for h in headers})==1, obj
    print(f"MINIC_NET_REPAIR_ABI=PASS owner={obj}")
PY_NET_ABI
    for net_target in "${net_targets[@]}"; do
      old_net_sha=$(sha256sum "$ev/minic/$net_target" | cut -d' ' -f1)
      new_net_sha=$(sha256sum "$out/$net_target" | cut -d' ' -f1)
      cp -a "$out/$net_target" "$ev/minic/$net_target"
      printf '%s\t%s\t%s\t%s\n' "$net_target" "$old_net_sha" "$new_net_sha" "$cc_sha" >>"$net_dir/provenance.tsv"
      echo "MINIC_NET_REPAIR_OBJECT target=$net_target old=$old_net_sha new=$new_net_sha changed=$([[ "$old_net_sha" != "$new_net_sha" ]] && echo true || echo false)"
    done
    python3 - "$ev/minic-manifest.sha256" "$ev" "$net_dir/provenance.tsv" "$target" "$new_sha" "$super_target" "$new_super_sha" <<'PY_FOUR_INTEGRITY'
import hashlib,sys
from pathlib import Path
manifest,root,changes=map(Path,sys.argv[1:4])
expected={"minic/"+sys.argv[4]:sys.argv[5],"minic/"+sys.argv[6]:sys.argv[7]}
for line in changes.read_text().splitlines()[1:]:
    path,old,new,cc=line.split("\t")
    assert path and new and cc
    expected["minic/"+path]=new
assert len(expected)==4, f"Expected 4 freshly compiled MiniC objects: {expected}"
untouched=0
for line in manifest.read_text().splitlines():
    digest,relative=line.split(None,1)
    observed=hashlib.sha256((root/relative).read_bytes()).hexdigest()
    if relative in expected:
        assert observed==expected[relative], "new MiniC owner digest mismatch: "+relative
    else:
        assert observed==digest, "old MiniC owner changed unexpectedly: "+relative
        untouched+=1
assert untouched==2060, f"Expected 2060 frozen neighbors; found {untouched}"
print(f"MINIC_FOUR_OWNER_INTEGRITY=PASS new_minic_owners=4 frozen_neighbors={untouched}")
PY_FOUR_INTEGRITY
    if [[ "${COHORT_NETWORK_CAPTURE_ONLY:-0}" == 1 ]]; then
      capture="$net_dir/exact-TU-capture"
      mkdir -p "$capture"
      for net_target in "${net_targets[@]}"; do
        relative="${net_target%.o}"
        base="${relative##*/}"
        for ext in i s; do
          source_path="$out/$relative.minic-stage2.$ext"
          [[ -s "$source_path" ]] || {
            echo "MINIC_NETWORK_CAPTURE=FAIL missing=$source_path"; exit 8;
          }
          bytes=$(stat -c %s "$source_path")
          ((bytes < 48 * 1024 * 1024)) || {
            echo "MINIC_NETWORK_CAPTURE=FAIL file_too_large=$source_path bytes=$bytes"; exit 8;
          }
          cp -a "$source_path" "$capture/$base.$ext"
          echo "MINIC_NETWORK_CAPTURE_FILE owner=$net_target type=$ext bytes=$bytes sha=$(sha256sum "$source_path" | cut -d' ' -f1)"
        done
        riscv64-linux-gnu-objdump -dr "$ev/gcc/$net_target" >"$capture/$base.gcc-objdump.txt"
        riscv64-linux-gnu-objdump -dr "$ev/minic/$net_target" >"$capture/$base.minic-objdump.txt"
        if [[ "$net_target" == net/netlink/genetlink.o ]]; then
          # MiniC stderr is deliberately separate from wrapper stdout, and is
          # otherwise deleted after successful C compilation. Capture *before*
          # restoring the frozen GNU output tree.
          stderr_path="$out/$relative.minic-stage2.minic.stderr"
          [[ -s "$stderr_path" ]] || {
            echo "GENL_TRACE_CAPTURE=FAIL missing_minic_stderr=$stderr_path"; exit 8;
          }
          grep -E 'GENL_IF_DECISION|GENL_COND_EMIT' "$stderr_path" \
            >"$capture/genetlink.if-control-trace.log" || true
          head -c 65536 "$stderr_path" >"$capture/genetlink.compiler-stderr-head.log"
          # Capture exactly the effective Core branch implementation after all
          # runtime-profile patches, not the pristine repository source.
          python3 - "$capture/genetlink.core-branch-impl.c" <<'PY_GENL_SOURCE'
from pathlib import Path
import sys
s = Path("src/core/core_lower.c").read_text()
prefix = "static MinicCoreLowerStatus lower_condition_branch("
start = s.find(prefix)
if start < 0:
    raise SystemExit("GENL_BRANCH_SOURCE=FAIL no_function")
# Ignore the early declaration. The implementation uniquely has a body.
start = s.find(prefix, start + len(prefix))
if start < 0:
    raise SystemExit("GENL_BRANCH_SOURCE=FAIL no_definition")
finish = s.find("\n}\n", start)
if finish < 0:
    raise SystemExit("GENL_BRANCH_SOURCE=FAIL no_closing_brace")
out = Path(sys.argv[1])
out.write_text(s[start:finish+3])
print("GENL_BRANCH_SOURCE=CAPTURED bytes=" + str(out.stat().st_size))
PY_GENL_SOURCE
          echo "GENL_TRACE_CAPTURE=PASS lines=$(wc -l <"$capture/genetlink.if-control-trace.log") raw_bytes=$(stat -c %s "$stderr_path")"
          head -n 75 "$capture/genetlink.if-control-trace.log" || true
        fi
        echo "MINIC_NETWORK_CAPTURE_OWNER=PASS owner=$net_target exact_gnu_E=true same_minic=true"
      done
      echo "MINIC_NETWORK_CAPTURE=PASS owners=2 full_link=SKIPPED"
    fi
    rm -rf -- "$out"
    cp -a --reflink=auto "$gold_snapshot" "$out"
    echo "MINIC_NET_REPAIR=READY four_current_minic_objects=true gcc_swap_count=0 golden_restored=true"
  fi
  # Fifth-owner *candidate repair*: replay only runtime.o with the current
  # MiniC label-entry fact barrier, leaving 2059 frozen C objects untouched.
  if [[ "${COHORT_REPAIR_RUNTIME_OWNER:-0}" == 1 ]]; then
    runtime_target=drivers/base/power/runtime.o
    runtime_dir="$hotfix/runtime-repaired"
    mkdir -p "$runtime_dir"
    printf '%s\n' "$runtime_target" >"$runtime_dir/selected.txt"
    old_runtime_sha=$(sha256sum "$ev/minic/$runtime_target" | cut -d' ' -f1)
    echo "MINIC_RUNTIME_REPAIR=START owner=$runtime_target old=$old_runtime_sha"
    python3 "$repo/tools/ci/linux-runtime-kbuild-regeneration-guard-v1.py" \
      --mode replay --out "$out" --golden-out "$gold_snapshot" \
      --evidence "$runtime_dir/replay.log" --selected "$runtime_dir/selected.txt" \
      --wrapper "$repo/tests/external/linux/stage2_kbuild_cc.sh" \
      --minic "$minic" --success-trace "$runtime_dir/success.txt" \
      --digest-trace "$runtime_dir/digests.txt" | tee "$runtime_dir/replay.log"
    python3 "$repo/tools/ci/linux-runtime-minic-route-audit-v1.py" \
      --objects-file "$runtime_dir/selected.txt" \
      --success-trace "$runtime_dir/success.txt" \
      --object-digest-trace "$runtime_dir/digests.txt" \
      --candidates "$out" | tee "$runtime_dir/route-audit.log"
    new_runtime_sha=$(sha256sum "$out/$runtime_target" | cut -d' ' -f1)
    python3 - "$ev/gcc/$runtime_target" "$ev/minic/$runtime_target" "$out/$runtime_target" <<'PY_RUNTIME_ABI'
import struct,sys
from pathlib import Path
headers=[Path(arg).read_bytes()[:52] for arg in sys.argv[1:]]
for h in headers:
    assert len(h)==52 and h[:6]==b"\x7fELF\x02\x01"
    assert struct.unpack_from("<H",h,16)[0]==1
    assert struct.unpack_from("<H",h,18)[0]==243
assert len({h[48:52] for h in headers})==1, "runtime.o GNU/MiniC ABI mismatch"
print("MINIC_RUNTIME_REPAIR_ABI=PASS gcc_old_new")
PY_RUNTIME_ABI
    for ext in i s; do
      intermediate="$out/drivers/base/power/runtime.minic-stage2.$ext"
      [[ -s "$intermediate" ]] || {
        echo "MINIC_RUNTIME_REPAIR=INCONCLUSIVE missing=$intermediate"; exit 8;
      }
      cp -a "$intermediate" "$runtime_dir/runtime.$ext"
    done
    riscv64-linux-gnu-objdump -dr "$ev/gcc/$runtime_target" \
      >"$runtime_dir/runtime.gcc-objdump.txt"
    riscv64-linux-gnu-objdump -dr "$ev/minic/$runtime_target" \
      >"$runtime_dir/runtime.old-minic-objdump.txt"
    riscv64-linux-gnu-objdump -dr "$out/$runtime_target" \
      >"$runtime_dir/runtime.new-minic-objdump.txt"
    printf 'owner=%s\nold_sha256=%s\nnew_sha256=%s\ncompiler_sha256=%s\n' \
      "$runtime_target" "$old_runtime_sha" "$new_runtime_sha" "$cc_sha" \
      >"$runtime_dir/provenance.txt"
    [[ "$old_runtime_sha" != "$new_runtime_sha" ]] || {
      echo "MINIC_RUNTIME_REPAIR=INCONCLUSIVE reason=identical_object label_barrier_did_not_change_runtime"; exit 8;
    }
    cp -a "$out/$runtime_target" "$ev/minic/$runtime_target"
    python3 - "$ev/minic-manifest.sha256" "$ev" "$runtime_target" "$new_runtime_sha" <<'PY_RUNTIME_FREEZE'
import hashlib,sys
from pathlib import Path
manifest,root=map(Path,sys.argv[1:3])
target="minic/"+sys.argv[3]
changes={"minic/drivers/tty/serial/earlycon.o","minic/fs/super.o",
         "minic/net/ipv4/tcp_cong.o","minic/net/netlink/genetlink.o",target}
assert len(changes)==5
frozen=0
for row in manifest.read_text().splitlines():
    digest,name=row.split(None,1)
    observed=hashlib.sha256((root/name).read_bytes()).hexdigest()
    if name==target:
        assert observed==sys.argv[4], "fifth MiniC candidate SHA mismatch"
    elif name not in changes:
        assert observed==digest, "frozen MiniC object modified: "+name
        frozen+=1
assert frozen==2059, "expected exactly 2059 immutable neighbor C owners"
print("MINIC_FIFTH_OWNER_FROZEN_NEIGHBORS=PASS count=2059")
PY_RUNTIME_FREEZE
    rm -rf -- "$out"
    cp -a --reflink=auto "$gold_snapshot" "$out"
    echo "MINIC_RUNTIME_REPAIR=READY old=$old_runtime_sha new=$new_runtime_sha changed=true golden_restored=true"
  fi
  # Sixth-owner hypothesis from unique P1 bind() soft-lockup ELF provenance.
  # Replay only inet_connection_sock.o using the already-patched MiniC.
  if [[ "${COHORT_REPAIR_INET_CSK_OWNER:-0}" == 1 ]]; then
    inet_target=net/ipv4/inet_connection_sock.o
    inet_dir="$hotfix/inet-csk-repaired"
    mkdir -p "$inet_dir"
    printf '%s\n' "$inet_target" >"$inet_dir/selected.txt"
    old_inet_sha=$(sha256sum "$ev/minic/$inet_target" | cut -d' ' -f1)
    echo "MINIC_INET_CSK_REPAIR=START owner=$inet_target old=$old_inet_sha"
    python3 "$repo/tools/ci/linux-runtime-kbuild-regeneration-guard-v1.py" \
      --mode replay --out "$out" --golden-out "$gold_snapshot" \
      --evidence "$inet_dir/replay.log" --selected "$inet_dir/selected.txt" \
      --wrapper "$repo/tests/external/linux/stage2_kbuild_cc.sh" \
      --minic "$minic" --success-trace "$inet_dir/success.txt" \
      --digest-trace "$inet_dir/digests.txt" | tee "$inet_dir/replay.log"
    python3 "$repo/tools/ci/linux-runtime-minic-route-audit-v1.py" \
      --objects-file "$inet_dir/selected.txt" \
      --success-trace "$inet_dir/success.txt" \
      --object-digest-trace "$inet_dir/digests.txt" \
      --candidates "$out" | tee "$inet_dir/route-audit.log"
    new_inet_sha=$(sha256sum "$out/$inet_target" | cut -d' ' -f1)
    python3 - "$ev/gcc/$inet_target" "$ev/minic/$inet_target" "$out/$inet_target" <<'PY_INET_ABI'
from pathlib import Path
import struct,sys
headers=[Path(arg).read_bytes()[:52] for arg in sys.argv[1:]]
for h in headers:
    assert len(h)==52 and h[:6]==b"\x7fELF\x02\x01"
    assert struct.unpack_from("<H",h,16)[0]==1
    assert struct.unpack_from("<H",h,18)[0]==243
assert len({h[48:52] for h in headers})==1
print("MINIC_INET_CSK_ABI=PASS gcc_old_new")
PY_INET_ABI
    for ext in i s; do
      intermediate="$out/net/ipv4/inet_connection_sock.minic-stage2.$ext"
      [[ -s "$intermediate" ]] || {
        echo "MINIC_INET_CSK_REPAIR=INCONCLUSIVE missing=$intermediate"; exit 8;
      }
      cp -a "$intermediate" "$inet_dir/inet_connection_sock.$ext"
    done
    riscv64-linux-gnu-objdump -dr "$ev/minic/$inet_target" \
      >"$inet_dir/inet_connection_sock.old-minic-objdump.txt"
    riscv64-linux-gnu-objdump -dr "$out/$inet_target" \
      >"$inet_dir/inet_connection_sock.new-minic-objdump.txt"
    printf 'owner=%s\nold_sha256=%s\nnew_sha256=%s\n' \
      "$inet_target" "$old_inet_sha" "$new_inet_sha" \
      >"$inet_dir/provenance.txt"
    [[ "$old_inet_sha" != "$new_inet_sha" ]] || {
      echo "MINIC_INET_CSK_REPAIR=INCONCLUSIVE reason=identical_object no_compiler_delta"
      exit 8
    }
    cp -a "$out/$inet_target" "$ev/minic/$inet_target"
    python3 - "$ev/minic-manifest.sha256" "$ev" "$inet_target" "$new_inet_sha" <<'PY_INET_FREEZE'
import hashlib,sys
from pathlib import Path
manifest,root=map(Path,sys.argv[1:3])
target="minic/"+sys.argv[3]
allow={"minic/drivers/tty/serial/earlycon.o","minic/fs/super.o",
       "minic/net/ipv4/tcp_cong.o","minic/net/netlink/genetlink.o",
       "minic/drivers/base/power/runtime.o",target}
assert len(allow)==6
frozen=0
for row in manifest.read_text().splitlines():
    digest,name=row.split(None,1)
    observed=hashlib.sha256((root/name).read_bytes()).hexdigest()
    if name==target:
        assert observed==sys.argv[4], "inet_csk SHA mismatch"
    elif name not in allow:
        assert observed==digest, "frozen MiniC object changed: "+name
        frozen+=1
assert frozen==2058, "expected 2058 immutable neighbor C objects"
print("MINIC_SIXTH_OWNER_FROZEN_NEIGHBORS=PASS count=2058")
PY_INET_FREEZE
    rm -rf -- "$out"
    cp -a --reflink=auto "$gold_snapshot" "$out"
    echo "MINIC_INET_CSK_REPAIR=READY old=$old_inet_sha new=$new_inet_sha changed=true golden_restored=true"
  fi
  # Seventh-owner MiniC replay: previously GCC-swap-screened P1 listen()
  # hotspot. Keep existing six fixes and 2057 other objects stable.
  if [[ "${COHORT_REPAIR_INET_HASHTABLES_OWNER:-0}" == 1 ]]; then
    hash_target=net/ipv4/inet_hashtables.o
    hash_dir="$hotfix/inet-hashtables-repaired"
    mkdir -p "$hash_dir"
    printf '%s\n' "$hash_target" >"$hash_dir/selected.txt"
    old_hash_sha=$(sha256sum "$ev/minic/$hash_target" | cut -d' ' -f1)
    echo "MINIC_INET_HASHTABLES_REPAIR=START owner=$hash_target old=$old_hash_sha"
    python3 "$repo/tools/ci/linux-runtime-kbuild-regeneration-guard-v1.py" \
      --mode replay --out "$out" --golden-out "$gold_snapshot" \
      --evidence "$hash_dir/replay.log" --selected "$hash_dir/selected.txt" \
      --wrapper "$repo/tests/external/linux/stage2_kbuild_cc.sh" \
      --minic "$minic" --success-trace "$hash_dir/success.txt" \
      --digest-trace "$hash_dir/digests.txt" | tee "$hash_dir/replay.log"
    python3 "$repo/tools/ci/linux-runtime-minic-route-audit-v1.py" \
      --objects-file "$hash_dir/selected.txt" \
      --success-trace "$hash_dir/success.txt" \
      --object-digest-trace "$hash_dir/digests.txt" \
      --candidates "$out" | tee "$hash_dir/route-audit.log"
    new_hash_sha=$(sha256sum "$out/$hash_target" | cut -d' ' -f1)
    python3 - "$ev/gcc/$hash_target" "$ev/minic/$hash_target" "$out/$hash_target" <<'PY_HASH_ABI'
from pathlib import Path
import struct,sys
headers=[Path(arg).read_bytes()[:52] for arg in sys.argv[1:]]
for h in headers:
    assert len(h)==52 and h[:6]==b"\x7fELF\x02\x01"
    assert struct.unpack_from("<H",h,16)[0]==1
    assert struct.unpack_from("<H",h,18)[0]==243
assert len({h[48:52] for h in headers})==1
print("MINIC_INET_HASHTABLES_ABI=PASS gcc_old_new")
PY_HASH_ABI
    for ext in i s; do
      intermediate="$out/net/ipv4/inet_hashtables.minic-stage2.$ext"
      [[ -s "$intermediate" ]] || {
        echo "MINIC_INET_HASHTABLES_REPAIR=INCONCLUSIVE missing=$intermediate"; exit 8;
      }
      cp -a "$intermediate" "$hash_dir/inet_hashtables.$ext"
    done
    riscv64-linux-gnu-objdump -dr "$ev/minic/$hash_target" \
      >"$hash_dir/inet_hashtables.old-minic-objdump.txt"
    riscv64-linux-gnu-objdump -dr "$out/$hash_target" \
      >"$hash_dir/inet_hashtables.new-minic-objdump.txt"
    printf 'owner=%s\nold_sha256=%s\nnew_sha256=%s\n' \
      "$hash_target" "$old_hash_sha" "$new_hash_sha" >"$hash_dir/provenance.txt"
    [[ "$old_hash_sha" != "$new_hash_sha" ]] || {
      echo "MINIC_INET_HASHTABLES_REPAIR=INCONCLUSIVE reason=identical_object no_compiler_delta"
      exit 8
    }
    cp -a "$out/$hash_target" "$ev/minic/$hash_target"
    python3 - "$ev/minic-manifest.sha256" "$ev" "$hash_target" "$new_hash_sha" <<'PY_HASH_FREEZE'
import hashlib,sys
from pathlib import Path
manifest,root=map(Path,sys.argv[1:3])
target="minic/"+sys.argv[3]
allow={"minic/drivers/tty/serial/earlycon.o","minic/fs/super.o",
       "minic/net/ipv4/tcp_cong.o","minic/net/netlink/genetlink.o",
       "minic/drivers/base/power/runtime.o","minic/net/ipv4/inet_connection_sock.o",
       target}
assert len(allow)==7
frozen=0
for row in manifest.read_text().splitlines():
    digest,name=row.split(None,1)
    observed=hashlib.sha256((root/name).read_bytes()).hexdigest()
    if name==target:
        assert observed==sys.argv[4], "inet_hashtables SHA mismatch"
    elif name not in allow:
        assert observed==digest, "frozen MiniC object changed: "+name
        frozen+=1
assert frozen==2057, "expected 2057 immutable neighbor C objects"
print("MINIC_SEVENTH_OWNER_FROZEN_NEIGHBORS=PASS count=2057")
PY_HASH_FREEZE
    rm -rf -- "$out"
    cp -a --reflink=auto "$gold_snapshot" "$out"
    echo "MINIC_INET_HASHTABLES_REPAIR=READY old=$old_hash_sha new=$new_hash_sha changed=true golden_restored=true"
  fi
  # Eighth owner: replay the authentic current MiniC netfilter/core.o, not a
  # GCC substitution. Prior compiler-wide semantic repairs may already
  # correct the frozen object's runtime bug. Reuse the exact Kbuild .i.
  if [[ "${COHORT_REPAIR_NETFILTER_CORE:-0}" == 1 ]]; then
    core_target=net/netfilter/core.o
    core_dir="$hotfix/netfilter-core-current"
    mkdir -p "$core_dir"
    printf '%s\n' "$core_target" >"$core_dir/selected.txt"
    core_old_sha=$(sha256sum "$ev/minic/$core_target" | cut -d' ' -f1)
    python3 "$repo/tools/ci/linux-runtime-kbuild-regeneration-guard-v1.py" \
      --mode replay --out "$out" --golden-out "$gold_snapshot" \
      --evidence "$core_dir/replay.log" --selected "$core_dir/selected.txt" \
      --wrapper "$repo/tests/external/linux/stage2_kbuild_cc.sh" \
      --minic "$minic" --success-trace "$core_dir/success.txt" \
      --digest-trace "$core_dir/digests.txt" | tee "$core_dir/replay.log"
    python3 "$repo/tools/ci/linux-runtime-minic-route-audit-v1.py" \
      --objects-file "$core_dir/selected.txt" \
      --success-trace "$core_dir/success.txt" \
      --object-digest-trace "$core_dir/digests.txt" --candidates "$out" \
      | tee "$core_dir/route-audit.log"
    core_new_sha=$(sha256sum "$out/$core_target" | cut -d' ' -f1)
    [[ "$core_old_sha" != "$core_new_sha" ]] || {
      echo "NETFILTER_CORE_CURRENT=INCONCLUSIVE unchanged_frozen_object"
      exit 8
    }
    for ext in i s; do
      source_path="$out/net/netfilter/core.minic-stage2.$ext"
      test -s "$source_path" || { echo "NETFILTER_CORE_CURRENT=ERROR missing=$source_path"; exit 8; }
      cp -a "$source_path" "$core_dir/core.$ext"
    done
    python3 - "$ev/gcc/$core_target" "$ev/minic/$core_target" "$out/$core_target" <<'PY_CORE_ABI'
import pathlib,struct,sys
headers=[pathlib.Path(p).read_bytes()[:52] for p in sys.argv[1:]]
assert all(len(h)==52 and h[:6]==b"\x7fELF\x02\x01" for h in headers)
assert all(struct.unpack_from("<H",h,16)[0]==1 and
           struct.unpack_from("<H",h,18)[0]==243 for h in headers)
assert len({h[48:52] for h in headers})==1
print("NETFILTER_CORE_ABI=PASS same_RV64_identity")
PY_CORE_ABI
    cp -a "$out/$core_target" "$ev/minic/$core_target"
    python3 - "$ev/minic-manifest.sha256" "$ev" "$core_target" "$core_new_sha" <<'PY_CORE_FREEZE'
from pathlib import Path
import hashlib,sys
manifest,root=map(Path,sys.argv[1:3])
target="minic/"+sys.argv[3]
allow={"minic/drivers/tty/serial/earlycon.o","minic/fs/super.o",
       "minic/net/ipv4/tcp_cong.o","minic/net/netlink/genetlink.o",
       "minic/drivers/base/power/runtime.o","minic/net/ipv4/inet_connection_sock.o",
       "minic/net/ipv4/inet_hashtables.o",target}
assert len(allow)==8
frozen=0
for row in manifest.read_text().splitlines():
    digest,name=row.split(None,1)
    observed=hashlib.sha256((root/name).read_bytes()).hexdigest()
    if name==target:
        assert observed==sys.argv[4], "new Netfilter core object mismatch"
    elif name not in allow:
        assert observed==digest, "unrelated MiniC object changed: "+name
        frozen+=1
assert frozen==2056, f"expected 2056 frozen neighbors, found {frozen}"
print("NETFILTER_CORE_FROZEN_NEIGHBORS=PASS count=2056")
PY_CORE_FREEZE
    printf 'owner=%s\nold_sha256=%s\nnew_sha256=%s\n' \
      "$core_target" "$core_old_sha" "$core_new_sha" >"$core_dir/provenance.txt"
    rm -rf -- "$out"
    cp -a --reflink=auto "$gold_snapshot" "$out"
    echo "NETFILTER_CORE_CURRENT=READY old=$core_old_sha new=$core_new_sha gcc_swap=NONE"
  fi
  # Ninth owner: prior 145/186 Python oracle showed a page-list overrun
  # with call RA inside mm/gup.o. The old specialized loop has no runtime
  # zero check for the decremented page count. Rebuild ONLY this owner using
  # the already-fixed current MiniC profile, retaining eight earlier repairs.
  if [[ "${COHORT_REPAIR_GUP_OWNER:-0}" == 1 ]]; then
    [[ "${COHORT_REPAIR_NETFILTER_CORE:-0}" == 1 ]] || {
      echo "GUP_CURRENT=ERROR requires_previous_eight_minic_owner_repairs"; exit 8;
    }
    gup_target=mm/gup.o
    gup_dir="$hotfix/gup-current"
    mkdir -p "$gup_dir"
    printf '%s\n' "$gup_target" >"$gup_dir/selected.txt"
    gup_old_sha=$(sha256sum "$ev/minic/$gup_target" | cut -d' ' -f1)
    python3 "$repo/tools/ci/linux-runtime-kbuild-regeneration-guard-v1.py" \
      --mode replay --out "$out" --golden-out "$gold_snapshot" \
      --evidence "$gup_dir/replay.log" --selected "$gup_dir/selected.txt" \
      --wrapper "$repo/tests/external/linux/stage2_kbuild_cc.sh" \
      --minic "$minic" --success-trace "$gup_dir/success.txt" \
      --digest-trace "$gup_dir/digests.txt" | tee "$gup_dir/replay.log"
    python3 "$repo/tools/ci/linux-runtime-minic-route-audit-v1.py" \
      --objects-file "$gup_dir/selected.txt" --success-trace "$gup_dir/success.txt" \
      --object-digest-trace "$gup_dir/digests.txt" --candidates "$out" \
      | tee "$gup_dir/route-audit.log"
    gup_new_sha=$(sha256sum "$out/$gup_target" | cut -d' ' -f1)
    [[ "$gup_old_sha" != "$gup_new_sha" ]] || {
      echo "GUP_CURRENT=INCONCLUSIVE unchanged_frozen_object"; exit 8;
    }
    for ext in i s; do
      test -s "$out/mm/gup.minic-stage2.$ext" || {
        echo "GUP_CURRENT=ERROR missing_preprocessed_or_assembly=$ext"; exit 8;
      }
      cp -a "$out/mm/gup.minic-stage2.$ext" "$gup_dir/gup.$ext"
    done
    riscv64-linux-gnu-objdump -dr --disassemble=__minic_inline_spec_9309_155 \
      "$ev/minic/$gup_target" >"$gup_dir/gup-old-specialization.disasm"
    riscv64-linux-gnu-objdump -dr --disassemble=__minic_inline_spec_9309_155 \
      "$out/$gup_target" >"$gup_dir/gup-new-specialization.disasm"
    python3 - "$ev/gcc/$gup_target" "$ev/minic/$gup_target" "$out/$gup_target" <<'PY_GUP_ABI'
import pathlib,struct,sys
h=[pathlib.Path(p).read_bytes()[:52] for p in sys.argv[1:]]
assert all(len(x)==52 and x[:6]==b"\x7fELF\x02\x01" for x in h)
assert all(struct.unpack_from("<H",x,16)[0]==1 and struct.unpack_from("<H",x,18)[0]==243 for x in h)
assert len({x[48:52] for x in h})==1
print("GUP_CURRENT_ABI=PASS same_RV64_identity")
PY_GUP_ABI
    cp -a "$out/$gup_target" "$ev/minic/$gup_target"
    python3 - "$ev/minic-manifest.sha256" "$ev" "$gup_target" "$gup_new_sha" <<'PY_GUP_FREEZE'
import hashlib,sys
from pathlib import Path
manifest,root=map(Path,sys.argv[1:3])
target="minic/"+sys.argv[3]
allow={"minic/drivers/tty/serial/earlycon.o","minic/fs/super.o",
       "minic/net/ipv4/tcp_cong.o","minic/net/netlink/genetlink.o",
       "minic/drivers/base/power/runtime.o","minic/net/ipv4/inet_connection_sock.o",
       "minic/net/ipv4/inet_hashtables.o","minic/net/netfilter/core.o",target}
assert len(allow)==9
untouched=0
for line in manifest.read_text().splitlines():
    digest,name=line.split(None,1)
    got=hashlib.sha256((root/name).read_bytes()).hexdigest()
    if name==target:
        assert got==sys.argv[4], "new GUP object mismatch"
    elif name not in allow:
        assert got==digest, "unrelated object changed: "+name
        untouched+=1
assert untouched==2055, f"expected 2055 frozen MiniC objects, got {untouched}"
print("GUP_CURRENT_FROZEN_NEIGHBORS=PASS count=2055")
PY_GUP_FREEZE
    printf 'owner=%s\nold_sha256=%s\nnew_sha256=%s\n' \
      "$gup_target" "$gup_old_sha" "$gup_new_sha" >"$gup_dir/provenance.txt"
    rm -rf -- "$out"
    cp -a --reflink=auto "$gold_snapshot" "$out"
    echo "GUP_CURRENT=READY old=$gup_old_sha new=$gup_new_sha gcc_swap=NONE"
  fi
  # Replay exactly one additional MiniC owner using the pinned GNU
  # preprocessing inputs. Do not modify the immutable frozen 2064 pool.
  if [[ "${COHORT_SPINLOCK_CAPTURE_ONLY:-0}" == 1 ]]; then
    spin_target="${COHORT_CAPTURE_TARGET:-kernel/locking/spinlock_debug.o}"
    spin_base="${spin_target##*/}"
    spin_base="${spin_base%.o}"
    spin_dir="$hotfix/spinlock-debug-capture"
    mkdir -p "$spin_dir"
    printf '%s\n' "$spin_target" >"$spin_dir/selected.txt"
    old_spin_sha=$(sha256sum "$ev/minic/$spin_target" | cut -d' ' -f1)
    echo "SPINLOCK_CAPTURE=START owner=$spin_target old_sha=$old_spin_sha"
    python3 "$repo/tools/ci/linux-runtime-kbuild-regeneration-guard-v1.py" \
      --mode replay --out "$out" --golden-out "$gold_snapshot" \
      --evidence "$spin_dir/replay.log" --selected "$spin_dir/selected.txt" \
      --wrapper "$repo/tests/external/linux/stage2_kbuild_cc.sh" \
      --minic "$minic" --success-trace "$spin_dir/success.txt" \
      --digest-trace "$spin_dir/digests.txt" | tee "$spin_dir/replay.log"
    python3 "$repo/tools/ci/linux-runtime-minic-route-audit-v1.py" \
      --objects-file "$spin_dir/selected.txt" \
      --success-trace "$spin_dir/success.txt" \
      --object-digest-trace "$spin_dir/digests.txt" --candidates "$out" \
      | tee "$spin_dir/route-audit.log"
    new_spin_sha=$(sha256sum "$out/$spin_target" | cut -d' ' -f1)
    for ext in i s; do
      source_path="$out/${spin_target%.o}.minic-stage2.$ext"
      [[ -s "$source_path" ]] || {
        echo "SPINLOCK_CAPTURE=INCONCLUSIVE missing=$source_path"; exit 8;
      }
      cp -a "$source_path" "$spin_dir/$spin_base.$ext"
    done
    riscv64-linux-gnu-objdump -dr "$ev/gcc/$spin_target" \
      >"$spin_dir/$spin_base.gcc-objdump.txt"
    riscv64-linux-gnu-objdump -dr "$ev/minic/$spin_target" \
      >"$spin_dir/$spin_base.old-minic-objdump.txt"
    riscv64-linux-gnu-objdump -dr "$out/$spin_target" \
      >"$spin_dir/$spin_base.new-minic-objdump.txt"
    printf 'owner=%s\nold_sha256=%s\nnew_sha256=%s\n' \
      "$spin_target" "$old_spin_sha" "$new_spin_sha" \
      >"$spin_dir/provenance.txt"
    echo "SPINLOCK_CAPTURE=PASS owner=$spin_target old=$old_spin_sha new=$new_spin_sha changed=$([[ "$old_spin_sha" != "$new_spin_sha" ]] && echo true || echo false) full_link=SKIPPED frozen_pool_unchanged=true"
    exit 0
  fi
fi
if [[ "${COHORT_NETWORK_CAPTURE_ONLY:-0}" == 1 ]]; then
  [[ "${COHORT_PATCH_NET_PAIR:-0}" == 1 ]] || {
    echo "MINIC_NETWORK_CAPTURE=ERROR requires_two_authentic_replays"; exit 8;
  }
  echo "MINIC_NETWORK_CAPTURE=COMPLETE compiler_owners=2 runtime_not_run=true"
  exit 0
fi
# Focused stack-frame attribution without relinking a 220MB Image.
# Rebuild only the three P1-path TUs against frozen GCC preprocessing flags.
# A SHA match against the immutable candidate cache proves this is the SAME
# compiler / SAME object bytes; the trace is observational, not a new fix.
if [[ "${COHORT_FRAME_CAPTURE_ONLY:-0}" == 1 ]]; then
  [[ "$cohort_scope" == FULL_C_UNIVERSE && "$n" == 2064 &&
     "${COHORT_ACTION:-all}" == verify ]] || {
    echo "COHORT_FRAME_CAPTURE=ERROR invalid_frozen_pool_scope"; exit 8;
  }
  frame_dir="$ev/p1-frame-components"
  mkdir -p "$frame_dir"
  printf '%s\n' net/ipv6/ip6_output.o net/ipv6/tcp_ipv6.o net/ipv4/tcp_output.o \
    >"$frame_dir/selected.txt"
  rm -rf -- "$out"
  cp -a --reflink=auto "$gold_snapshot" "$out"
  MINIC_CORE_FRAME_TRACE=1 COHORT_FRAME_CAPTURE_ONLY=1 python3 \
    "$repo/tools/ci/linux-runtime-kbuild-regeneration-guard-v1.py" \
    --mode replay --out "$out" --golden-out "$gold_snapshot" \
    --evidence "$frame_dir/replay-evidence.txt" \
    --selected "$frame_dir/selected.txt" \
    --wrapper "$repo/tests/external/linux/stage2_kbuild_cc.sh" \
    --minic "$minic" --success-trace "$frame_dir/success.txt" \
    --digest-trace "$frame_dir/digests.sha256" \
    >"$frame_dir/replay.log" 2>&1 || {
      tail -n 60 "$frame_dir/replay.log"; exit 8;
    }
  python3 "$repo/tools/ci/linux-runtime-minic-route-audit-v1.py" \
    --objects-file "$frame_dir/selected.txt" \
    --success-trace "$frame_dir/success.txt" \
    --object-digest-trace "$frame_dir/digests.sha256" \
    --candidates "$out" >"$frame_dir/route-audit.log"
  while IFS= read -r target; do
    cmp -s "$ev/minic/$target" "$out/$target" || {
      echo "COHORT_FRAME_CAPTURE=NONIDENTICAL owner=$target current_profile_not_frozen_baseline"
      exit 8
    }
    echo "COHORT_FRAME_CAPTURE=SHA_IDENTICAL owner=$target"
  done <"$frame_dir/selected.txt"
  # stage2_kbuild_cc redirects MiniC stderr to per-TU *.minic.stderr;
  # read those exact captured streams, not the Python replay stdout.
  while IFS= read -r target; do
    stderr="$out/${target%.o}.minic-stage2.minic.stderr"
    if [[ -s "$stderr" ]]; then
      grep -E '^CORE_FRAME' "$stderr" >>"$frame_dir/replay.log" || true
    fi
  done <"$frame_dir/selected.txt"
  python3 - "$frame_dir/replay.log" "$frame_dir/summary.tsv" <<'PY_FRAME_SUMMARY'
from pathlib import Path
import re,sys
log,out=map(Path,sys.argv[1:])
pattern=re.compile(r"CORE_FRAME(?:_REUSE)? function=(\S+) frame=(\d+) value_bytes=(\d+) values=(\d+) objects=(\d+)")
rows=[]
for line in log.read_text(errors="replace").splitlines():
    m=pattern.search(line)
    if not m: continue
    name=m.group(1)
    frame,value,values,objects=map(int,m.groups()[1:])
    rows.append((frame,name,value,frame-value,values,objects))
if not rows:
    raise SystemExit("COHORT_FRAME_CAPTURE=ERROR zero_frame_traces")
rows.sort(reverse=True)
lines=["frame_bytes\tfunction\tvalue_bytes\tnon_value_bytes\tvalue_count\tobject_count"]
for frame,name,value,nonvalue,values,objects in rows:
    lines.append(f"{frame}\t{name}\t{value}\t{nonvalue}\t{values}\t{objects}")
out.write_text("\n".join(lines)+"\n")
print(f"COHORT_FRAME_CAPTURE=PASS traced_functions={len(rows)}")
for frame,name,value,nonvalue,values,objects in rows[:32]:
    print(f"COHORT_FRAME_TOP function={name} frame={frame} value_bytes={value} "
          f"non_value_bytes={nonvalue} values={values} objects={objects}")
PY_FRAME_SUMMARY
  echo "COHORT_FRAME_CAPTURE=COMPLETE relink=SKIPPED P1_oracle=NOT_RUN"
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
        exclude_list) if ! grep -Fqx "${objects[i]}" "$ev/p1_gcc_rescue_set.txt"; then selected=1; fi ;;
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
      --golden-out "$gold_snapshot" --selected "$objects_file" \
      --affected-file "$d/affected-objects.txt" | tee -a "$d/guard.log" || {
        echo "COHORT_RESULT=INCONCLUSIVE stage=header_impact name=$name"; exit 8;
      }
    last_candidate_delta=""
    if [[ -s "$d/affected-objects.txt" ]]; then
      # Replay is permitted for a mixed GCC/MiniC selection ONLY if every
      # TU that depends on changed generated headers is selected for MiniC
      # refresh. Check the FULL frozen GCC dependency set, not a subset.
      mapfile -t affected <"$d/affected-objects.txt"
      if ! python3 - "$d/affected-objects.txt" "$d/selected-objects.txt" <<'PY_HEADER_CLOSURE'
from pathlib import Path
import sys
affected={x.strip() for x in Path(sys.argv[1]).read_text().splitlines() if x.strip()}
selected={x.strip() for x in Path(sys.argv[2]).read_text().splitlines() if x.strip()}
missing=sorted(affected-selected)
if missing:
    print("COHORT_HEADER_MIXED=INCONCLUSIVE stale_GCC_dependents="+",".join(missing))
    raise SystemExit(8)
print(f"COHORT_HEADER_MIXED=PASS affected={len(affected)} all_selected=true")
PY_HEADER_CLOSURE
      then
        echo "COHORT_RESULT=INCONCLUSIVE stage=dynamic_header_requires_all_dependents_selected name=$name"
        exit 8
      fi
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
    # Same linked build directory, different guest run: keep the selection
    # manifest identical for the non-C Kbuild provenance verifier.
    cp -a "$last_link_source_dir/selected-objects.txt" "$d/selected-objects.txt"
  fi
  for ((i=0;i<n;i++)); do
    target=${objects[i]}
    selected=0
    case "$mode" in
      prefix) if ((i<count)); then selected=1; fi ;;
      single) if ((i==count)); then selected=1; fi ;;
      all_except) if ((i!=count)); then selected=1; fi ;;
      exclude_list) if ! grep -Fqx "${objects[i]}" "$ev/p1_gcc_rescue_set.txt"; then selected=1; fi ;;
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
    # An expected QEMU ROM-overlap diagnostic is nonzero, but it must NOT
    # fire the top-level ERR trap ("COHORT_UNHANDLED=FAIL"). The verdict is
    # classified explicitly below, keeping genuine unexpected shell errors.
    if LINUX_IMAGE="$d/Image" INITRAMFS="$ev/runtime-initramfs.cpio.gz" \
       BUILD_DIR="$d/qemu" LINUX_RELEASE=6.6.143 \
       LINUX_RUNTIME_PROFILE="$profile" QEMU_TIMEOUT_SECONDS=45 \
       bash "$repo/tests/external/linux/runtime_boot.sh" >"$d/runtime.log" 2>&1; then
      rc=0
    else
      rc=$?
    fi
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
      sample_args=(--timeout-seconds 25)
      if [[ "${COHORT_NEXT_FAULT_DIAG:-0}" == 1 && "$name" == earlycon_fixed_only ]]; then
        sample_args=(--timeout-seconds 100 --sample-seconds 1,4,8,12,16,20,25,30,35,40,45,50,55,60,70,80)
        echo "COHORT_NEXT_FAULT=QMP_EXTENDED samples=16 until_s=80 no_new_link=true"
      fi
      python3 "$repo/tools/ci/linux-runtime-qmp-pc-sampler-v1.py" \
        --image "$d/Image" --output-dir "$d" "${sample_args[@]}" \
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
layout={}
for line in nm.stdout.splitlines():
    cols=line.split(None,2)
    if len(cols)==3 and cols[2] in ("__earlycon_table","__earlycon_table_end"):
        try:
            layout[cols[2]]=int(cols[0],16)
        except ValueError:
            pass
if len(layout)==2:
    span=layout["__earlycon_table_end"]-layout["__earlycon_table"]
    print("COHORT_EARLYCON_TABLE_LAYOUT begin=0x%x end=0x%x span_bytes=%d "
          "struct_earlycon_id_size_expected=152 remainder=%d" %
          (layout["__earlycon_table"],layout["__earlycon_table_end"],
           span,span%152))
else:
    print("COHORT_EARLYCON_TABLE_LAYOUT=INCOMPLETE names="+",".join(sorted(layout)))
last_pcs=[]
pc_symbols=[]
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
            if register == "pc":
                pc_symbols.append((at, symbol))
            print(f"COHORT_QMP_SYMBOL at_s={at} reg={register} addr=0x{addr:x} "
                  f"symbol={symbol} offset=0x{addr-base:x}")
if len(last_pcs)>=2 and last_pcs[-1]==last_pcs[-2]:
    print(f"COHORT_QMP_STABLE_PC=OBSERVED samples={len(last_pcs)} addr=0x{last_pcs[-1]:x}")
from collections import Counter
frequency = Counter(s for _,s in pc_symbols)
print(f"COHORT_QMP_PC_DIVERSITY samples={len(last_pcs)} unique_pcs={len(set(last_pcs))} "
      f"unique_functions={len(frequency)} last_symbol={pc_symbols[-1][1] if pc_symbols else 'UNAVAILABLE'}")
for symbol,count in frequency.most_common(12):
    print(f"COHORT_QMP_HOTSPOT samples={count} symbol={symbol}")
PY_EARLY_SYMBOLS
        cat "$d/qmp-symbols.log" >>"$d/runtime.log"
      fi
      early_finished=$(date +%s%N)
      echo "COHORT_TIMING trial=$name stage=qemu_early_noinitrd elapsed_ms=$(((early_finished-early_started)/1000000)) rc=$early_rc"
      printf 'timing_%s_qemu_early_noinitrd_ms=%s\n' "$name" "$(((early_finished-early_started)/1000000))" >>"$ev/identity.txt"
      if grep -Eq 'Linux version 6\.6\.143|Kernel panic|Oops:|Unable to handle|BUG:' "$d/qemu-early-noinitrd.log"; then
        echo "COHORT_KERNEL_EARLY_FRONTIER=LINUX_SERIAL_BANNER trial=$name"
      elif [[ -s "$d/qmp-symbols.log" ]] && \
           grep -Eq 'COHORT_QMP_SYMBOL at_s=[0-9]+ reg=pc .* symbol=' "$d/qmp-symbols.log"; then
        # OpenSBI may be the only SERIAL output, while QMP proves the guest
        # PC has already reached a concrete linked Linux kernel function.
        echo "COHORT_KERNEL_EARLY_FRONTIER=LINUX_EXECUTING_PRE_BANNER trial=$name"
      elif grep -Eqi 'OpenSBI|Platform Name|Firmware Base' "$d/qemu-early-noinitrd.log"; then
        echo "COHORT_KERNEL_EARLY_FRONTIER=FIRMWARE_SERIAL_ONLY_NO_PC_PROOF trial=$name"
      else
        echo "COHORT_KERNEL_EARLY_FRONTIER=NO_SERIAL_OR_PC_PROOF trial=$name"
      fi
      # The full-all trial compares the pinned golden kernel on this
      # runner. Focused GCC-object swaps reuse that result: the golden
      # Image, QEMU command and runner have not changed.
      if [[ "$name" == full_all ]]; then
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
      else
        if [[ "${COHORT_EARLYCON_HOTFIX:-0}" == 1 ]]; then
          # Unlike the normal full_all sequence, this shortcut never runs a
          # same-runner GNU reference. Its reference is the pinned prior
          # QEMU-certified golden pool restored and hash-checked above.
          echo "COHORT_GCC_EARLY_REFERENCE=PINNED_PRIOR_CERTIFICATE trial=$name same_runner=false"
        else
          echo "COHORT_GCC_EARLY_REFERENCE=REUSED_FROM_FULL_ALL trial=$name same_runner=true"
        fi
      fi
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
  # A FAIL can advance to a different faulting function. Preserve the
  # exact P1 EPC/RA/SP fingerprint, not only the final PASS/FAIL bit.
  if [[ "$profile" == p1 && -s "$d/qemu/rdinit-init.log" ]]; then
    python3 "$repo/tools/ci/linux-runtime-p1-crash-fingerprint-v1.py" \
      --trial "$name" --log "$d/qemu/rdinit-init.log"
  fi
  echo "COHORT_TRIAL name=$name count=$count verdict=$verdict image=$image_sha"
  printf '%s\t%s\t%s\t%s\n' "$name" "$count" "$verdict" "$image_sha" >>"$ev/results.tsv"
  TRIAL_VERDICT="$verdict"
}
# One diagnostic only, deliberately no full-universe bisection or GCC-swap loop.
# Passing here proves just this mixed-image frontier, not a 2064-object
# recompilation or a complete Linux/P1 boot certificate.
if [[ "${COHORT_EARLYCON_HOTFIX:-0}" == 1 ]]; then
  trial earlycon_fixed_only "$n" fast
  if [[ "${COHORT_NETFILTER_EXPERIMENT_ONLY:-0}" == 1 ]]; then
    test "${COHORT_REPAIR_NETFILTER_CORE:-0}" == 1 || exit 8
    test -s "$ev/trials/earlycon_fixed_only/Image" || exit 8
    echo "NETFILTER_CURRENT_IMAGE=READY preliminary_verdict=$TRIAL_VERDICT single_current_minic_core=true original_python_oracle=NOT_YET_RUN"
    exit 0
  fi
  if [[ "$TRIAL_VERDICT" == PASS ]]; then
    trial earlycon_fixed_full "$n" full
    if [[ "$TRIAL_VERDICT" == PASS ]]; then
      echo "EARLYCON_HOTFIX_RESULT=BOOT_PASS mixed_profile=true"
      exit 0
    fi
    echo "EARLYCON_HOTFIX_RESULT=INCONCLUSIVE reason=full_or_p1_failed"
    exit 8
  fi
  diag="$ev/trials/earlycon_fixed_only/qmp-symbols.log"
  if [[ -s "$diag" ]] && grep -Eq 'COHORT_QMP_SYMBOL at_s=(4|8) reg=pc .* symbol=' "$diag" && \
     grep -E 'COHORT_QMP_SYMBOL at_s=(4|8) reg=pc .* symbol=' "$diag" | \
       grep -Ev 'symbol=(setup_earlycon|BELOW_LINKED_TEXT)' >/dev/null; then
    if [[ "${COHORT_PATCH_SUPER_V1:-0}" == 1 ]]; then
      if [[ "${COHORT_PATCH_NET_PAIR:-0}" == 1 ]]; then
        echo "MINIC_REPAIRED_FOUR_FRONTIER=ADVANCED earlycon_super_tcp_cong_genetlink_current_minic=true frozen_neighbors=2060 boot_certificate=NOT_CERTIFIED"
      else
      echo "MINIC_REPAIRED_PAIR_FRONTIER=ADVANCED earlycon_and_super_current_minic=true frozen_neighbors=2062 boot_certificate=NOT_CERTIFIED"
      fi
    else
    echo "EARLYCON_HOTFIX_RESULT=FRONTIER_ADVANCED after_earlycon=true p1=NOT_CERTIFIED before_link_2063_frozen=true regenerated_header_dependents=4"
    fi
    if [[ "${COHORT_EARLYCON_EXPLICIT_INITRD:-0}" == 1 ]]; then
      # Run the already-existing large-Image initrd loader against the SAME
      # linked Image; no second Kbuild link or MiniC compilation.
      d="$ev/trials/earlycon_fixed_only"
      wrapper="$repo/tests/external/linux/qemu_explicit_initrd_wrapper.py"
      test -s "$d/Image" && test -s "$ev/runtime-initramfs.cpio.gz"
      test -x "$wrapper"
      echo "EARLYCON_EXPLICIT_INITRD=START image_bytes=$(stat -c %s "$d/Image") same_linked_image=true"
      rc=0
      LINUX_IMAGE="$d/Image" INITRAMFS="$ev/runtime-initramfs.cpio.gz" \
        BUILD_DIR="$d/qemu-explicit-fast" LINUX_RELEASE=6.6.143 \
        LINUX_RUNTIME_PROFILE=fast QEMU_TIMEOUT_SECONDS=30 \
        QEMU_SYSTEM_RISCV64="$wrapper" \
        QEMU_REAL_SYSTEM_RISCV64=qemu-system-riscv64 \
        QEMU_EXPLICIT_DTB_DIR="$d/dtb-explicit-fast" \
          bash "$repo/tests/external/linux/runtime_boot.sh" \
          >"$d/runtime-explicit-fast.log" 2>&1 || rc=$?
      echo "EARLYCON_EXPLICIT_FAST_RC=$rc"
      if [[ -s "$d/qemu-explicit-fast/rdinit-init.log" ]]; then
        if grep -aq 'Linux version 6.6.143' "$d/qemu-explicit-fast/rdinit-init.log"; then
          echo "EARLYCON_EXPLICIT_FAST_BANNER=OBSERVED"
        else
          echo "EARLYCON_EXPLICIT_FAST_BANNER=NOT_OBSERVED"
        fi
        grep -aE 'Linux version 6.6.143|Kernel panic|Oops:|BUG:|soft lockup|devtmpfs:|Run /init|Starting init:' \
          "$d/qemu-explicit-fast/rdinit-init.log" | tail -n 45 || true
      fi
      if ((rc==0)); then
        echo "EARLYCON_EXPLICIT_FAST=PASS"
        if [[ "${COHORT_EARLYCON_EXPLICIT_SHELL:-0}" == 1 ]]; then
          echo "EARLYCON_EXPLICIT_SHELL=START same_linked_image=true independent_of_P1=true"
          shell_rc=0
          LINUX_IMAGE="$d/Image" INITRAMFS="$ev/runtime-initramfs.cpio.gz" \
            BUILD_DIR="$d/qemu-explicit-shell" LINUX_RELEASE=6.6.143 \
            LINUX_RUNTIME_PROFILE=shell QEMU_TIMEOUT_SECONDS=55 \
            QEMU_SHELL_INPUT_DELAY_SECONDS=22 \
            LINUX_RUNTIME_EARLYCON_ARG=earlycon=uart8250,mmio,0x10000000,115200n8 \
            QEMU_SYSTEM_RISCV64="$wrapper" \
            QEMU_REAL_SYSTEM_RISCV64=qemu-system-riscv64 \
            QEMU_EXPLICIT_DTB_DIR="$d/dtb-explicit-shell" \
            bash "$repo/tests/external/linux/runtime_boot.sh" \
            >"$d/runtime-explicit-shell.log" 2>&1 || shell_rc=$?
          if ((shell_rc==0)); then
            echo "EARLYCON_EXPLICIT_SHELL=PASS verified_commands=true"
          else
            echo "EARLYCON_EXPLICIT_SHELL=NOT_CERTIFIED rc=$shell_rc"
            grep -aE 'Run /bin/sh|SH_READY|PROC_CMDLINE|RDINIT_SH|DONE_RDINIT|Kernel panic|soft lockup|LINUX_RUNTIME_' \
              "$d/qemu-explicit-shell/rdinit-sh.log" | tail -n 45 || true
            tail -n 16 "$d/runtime-explicit-shell.log" || true
          fi
        fi
        p1_rc=0
        LINUX_IMAGE="$d/Image" INITRAMFS="$ev/runtime-initramfs.cpio.gz" \
          BUILD_DIR="$d/qemu-explicit-p1" LINUX_RELEASE=6.6.143 \
          LINUX_RUNTIME_PROFILE=p1 QEMU_TIMEOUT_SECONDS=45 \
          QEMU_SYSTEM_RISCV64="$wrapper" \
          QEMU_REAL_SYSTEM_RISCV64=qemu-system-riscv64 \
          QEMU_EXPLICIT_DTB_DIR="$d/dtb-explicit-p1" \
            bash "$repo/tests/external/linux/runtime_boot.sh" \
            >"$d/runtime-explicit-p1.log" 2>&1 || p1_rc=$?
        if ((p1_rc==0)) && python3 "$repo/tests/external/linux/runtime_v2_log_check.py" \
             --profile p1 --log "$d/qemu-explicit-p1/rdinit-init.log" \
             >"$d/runtime-explicit-p1-oracle.log" 2>&1; then
          echo "EARLYCON_EXPLICIT_P1=PASS syscalls=12 mixed_profile=true"
        else
          echo "EARLYCON_EXPLICIT_P1=NOT_CERTIFIED rc=$p1_rc"
          tail -n 35 "$d/runtime-explicit-p1.log" || true
        fi
      else
        echo "EARLYCON_EXPLICIT_FAST=NOT_CERTIFIED rc=$rc"
        tail -n 35 "$d/runtime-explicit-fast.log" || true
      fi
    fi
    qmp_snapshot_args=()
    if [[ "${COHORT_QMP_STOP_SNAPSHOT:-0}" == 1 ]]; then
      qmp_snapshot_args=(--stop-during-snapshot)
      echo "COHORT_QMP_SNAPSHOT_MODE=PAUSED"
    fi
    if [[ "${COHORT_UART_EARLYCON:-0}" == 1 ]]; then
      # Same already-linked mixed kernel Image; change ONLY the boot args.
      # Linux 6.6.143 defconfig does not enable SBI earlycon but selects
      # SERIAL_EARLYCON through SERIAL_8250_CONSOLE.
      d="$ev/trials/earlycon_fixed_only"
      config="$out/.config"
      echo "EARLYCON_UART_CONFIG=BEGIN"
      grep -E '^(CONFIG_SERIAL_8250_CONSOLE=|CONFIG_SERIAL_EARLYCON=|CONFIG_SERIAL_EARLYCON_RISCV_SBI=|# CONFIG_SERIAL_EARLYCON_RISCV_SBI)' "$config" || true
      echo "EARLYCON_UART_CONFIG=END"
      uartlog="$d/qemu-uart-earlycon.log"
      qmp_dir="$d/uart-qmp"
      mkdir -p "$qmp_dir"
      cmdline='console=ttyS0 earlycon=uart8250,mmio,0x10000000,115200n8 loglevel=8 ignore_loglevel panic=-1'
      uart_rc=0
      lock_probe_args=()
      if [[ "${COHORT_QMP_STOP_SNAPSHOT:-0}" == 1 &&
            "${COHORT_NEXT_GCC_TARGET:-}" == "kernel/locking/spinlock_debug.o" ]]; then
        # Resolve the PC against the linked MiniC vmlinux for this *trial*.
        # The saved lock argument lives in stack word 0 at offset +0x30.
        lock_symbol_hex=$(/usr/bin/riscv64-linux-gnu-nm -n --defined-only "$out/vmlinux" |
          awk '$3=="do_raw_spin_unlock" {print $1}')
        if [[ ! "$lock_symbol_hex" =~ ^[0-9a-fA-F]{16}$ ]]; then
          echo "COHORT_QMP_LOCK_PROBE=INCONCLUSIVE reason=owner_symbol_missing"
          exit 8
        fi
        lock_site_pc=$(python3 -c 'import sys; print(hex(int(sys.argv[1], 16) + 0x30))' "$lock_symbol_hex")
        lock_probe_args=(--probe-lock-at-pc "$lock_site_pc")
        echo "COHORT_QMP_LOCK_PROBE_SITE=$lock_site_pc stack_word=0 owner=do_raw_spin_unlock"
      fi
      python3 "$repo/tools/ci/linux-runtime-qmp-pc-sampler-v1.py" \
        --image "$d/Image" --output-dir "$qmp_dir" \
        --timeout-seconds 40 --sample-seconds 1,4,8,12,20,32 \
        --stack-words 48 "${qmp_snapshot_args[@]}" "${lock_probe_args[@]}" --kernel-cmdline "$cmdline" \
        >"$qmp_dir/qmp-probe.log" 2>&1 || uart_rc=$?
      cp -a "$qmp_dir/qemu-early-noinitrd.log" "$uartlog"
      echo "EARLYCON_UART_QMP_RC=$uart_rc"
      grep -E 'COHORT_QMP_EARLY_SAMPLE|COHORT_QMP_EARLY_COMPLETE|COHORT_QMP_LOCK_PROBE|COHORT_QMP_EARLY=UNAVAILABLE' "$qmp_dir/qmp-probe.log" || true
      python3 "$repo/tools/ci/linux-runtime-qmp-stack-symbols-v1.py" \
        --jsonl "$qmp_dir/qemu-registers.jsonl" \
        --vmlinux "$out/vmlinux" \
        --output "$qmp_dir/stack-symbols.txt" || true
      echo "EARLYCON_UART_STACK_SYMBOLS=BEGIN"
      head -n 95 "$qmp_dir/stack-symbols.txt" 2>/dev/null || true
      echo "EARLYCON_UART_STACK_SYMBOLS=END"
      grep -E 'REGISTER_LATE_DIVERSITY|REGISTER_CODE' "$qmp_dir/stack-symbols.txt" || true
      if grep -aq 'Linux version 6.6.143' "$uartlog"; then
        echo "EARLYCON_UART_BANNER=OBSERVED"
      else
        echo "EARLYCON_UART_BANNER=NOT_OBSERVED"
      fi
      grep -aE 'Linux version 6\.6\.143|Kernel command line|earlycon:|printk:|Kernel panic|Oops:|BUG:|soft lockup|devtmpfs:|Run /init|Starting init:' \
        "$uartlog" | tail -n 80 || true
    fi
    if [[ "${COHORT_GDB_BOUNDARY:-0}" == 1 ]]; then
      d="$ev/trials/earlycon_fixed_only"
      mkdir -p "$d/gdb-boundary"
      echo "GDB_BOUNDARY=START same_linked_image=true"
      python3 "$repo/tools/ci/linux-runtime-gdb-boundary-v1.py" \
        --image "$d/Image" --vmlinux "$out/vmlinux" \
        --output-dir "$d/gdb-boundary" --timeout-seconds 55 || {
          echo "GDB_BOUNDARY=INCONCLUSIVE runtime_certificate=false"
        }
      echo "GDB_BOUNDARY=END"
    fi
    if [[ "${COHORT_FAULT_OWNER_MAP:-0}" == 1 ]]; then
      # Determine the provenance of the linked panic-frame symbols from the
      # already authenticated C object cache, without rebuilding any owner.
      owner_dir="$ev/fault-owner"
      mkdir -p "$owner_dir"
      python3 - "$ev/minic" "$objects_file" "$owner_dir" <<'PY_FAULT_OWNER'
from collections import defaultdict
from pathlib import Path
import os,subprocess,sys
pool,manifest,ev=map(Path,sys.argv[1:])
markers=(os.environ.get("COHORT_FAULT_OWNER_SYMBOL","__minic_inline_spec_6515_19"),
         os.environ.get("COHORT_FAULT_OWNER_COMPANION","__get_unaligned_cpu32"))
names=[x.strip() for x in manifest.read_text().splitlines() if x.strip() and not x.startswith("#")]
result=defaultdict(set)
for start in range(0,len(names),64):
    owners=names[start:start+64]
    argv=["riscv64-linux-gnu-nm","-A","--defined-only"]+[str(pool/x) for x in owners]
    nm=subprocess.run(argv,capture_output=True,text=True,errors="replace")
    if nm.returncode:
        raise SystemExit("FAULT_OWNER=ERROR nm_failed="+nm.stderr[:350])
    for line in nm.stdout.splitlines():
        if ":" not in line: continue
        path,_,row=line.partition(":")
        fields=row.split()
        if not fields or fields[-1] not in markers: continue
        owner=Path(path).relative_to(pool).as_posix()
        result[fields[-1]].add(owner)
for symbol in markers:
    owners=sorted(result[symbol])
    print(f"FAULT_SYMBOL symbol={symbol} candidates={len(owners)}",flush=True)
    for owner in owners[:100]:
        print(f"FAULT_SYMBOL_OWNER symbol={symbol} owner={owner}",flush=True)
    (ev/(symbol+".txt")).write_text("\n".join(owners)+"\n")
spec=sorted(result[markers[0]])
companion=sorted(result[markers[1]])
paired=sorted(set(spec)&set(companion))
# One unique specialization owner is strong evidence. Otherwise do not guess:
# requiring the SAME TU to define the faulting helper is a conservative fallback.
selected=spec if len(spec)==1 else paired if len(paired)==1 else []
if selected:
    print(f"FAULT_OWNER=RESOLVED owner={selected[0]} unique_spec={len(spec)==1} paired_helper={selected[0] in companion}",flush=True)
    (ev/"candidate.txt").write_text(selected[0]+"\n")
else:
    print(f"FAULT_OWNER=INCONCLUSIVE spec_candidates={len(spec)} shared_helper={len(paired)}",flush=True)
PY_FAULT_OWNER
    fi
    if [[ "${COHORT_NEXT_GCC_SWAPS:-0}" == 1 ]]; then
      baseline="$ev/trials/earlycon_fixed_only/qemu-uart-earlycon.log"
      [[ -s "$baseline" ]] || { echo "NEXT_OWNER_SWAP=ERROR no_UARТ_baseline"; exit 8; }
      swap_targets=()
      if [[ "${COHORT_FAULT_OWNER_MAP:-0}" == 1 ]]; then
        if [[ -s "$ev/fault-owner/candidate.txt" ]]; then
          candidate=$(cat "$ev/fault-owner/candidate.txt")
          swap_targets+=("$candidate")
        elif [[ "${COHORT_FAULT_OWNER_FALLBACK:-}" != "" ]]; then
          echo "FAULT_OWNER=FALLBACK target=$COHORT_FAULT_OWNER_FALLBACK exact_owner_unresolved=true"
          swap_targets+=("$COHORT_FAULT_OWNER_FALLBACK")
        fi
        if [[ "${COHORT_FAULT_OWNER_EXTRA_NETLINK:-1}" == 1 ]]; then
          swap_targets+=(net/netlink/genetlink.o)
        fi
        if (("${#swap_targets[@]}"==0)); then
          echo "NEXT_OWNER_SWAP=INCONCLUSIVE reason=no_unique_fault_owner_or_fallback"
          exit 8
        fi
      else
        swap_targets+=("${COHORT_NEXT_GCC_TARGET:-fs/super.o}")
      fi
      for swap_target in "${swap_targets[@]}"; do
        swap_index=-1
        for ((swap_k=0;swap_k<n;swap_k++)); do
          if [[ "${objects[swap_k]}" == "$swap_target" ]]; then
            swap_index=$swap_k; break
          fi
        done
        if ((swap_index<0)); then
          echo "NEXT_OWNER_SWAP=INCONCLUSIVE target=$swap_target reason=outside_frozen_2064"
          exit 8
        fi
        swap_name="gcc_only_$(printf '%s' "$swap_target" | tr '/.' '__')"
        if [[ -n "${COHORT_NEXT_GCC_TARGET:-}" &&
              "$swap_target" == "$COHORT_NEXT_GCC_TARGET" ]]; then
          expected_symbol="${COHORT_NEXT_GCC_EXPECT_SYMBOL:-do_raw_spin_unlock}"
          riscv64-linux-gnu-nm --defined-only "$ev/minic/$swap_target" | awk -v symbol="$expected_symbol" '$NF == symbol {seen=1} END {exit !seen}' || {
            echo "NEXT_OWNER_SWAP=INCONCLUSIVE target=$swap_target reason=not_symbol_owner symbol=$expected_symbol"
            exit 8
          }
          echo "NEXT_OWNER_SYMBOL=CONFIRMED symbol=$expected_symbol owner=$swap_target"
        fi
        echo "NEXT_OWNER_SWAP=START target=$swap_target index=$swap_index fixed_earlycon=true"
        trial "$swap_name" "$swap_index" fast all_except
        test "$TRIAL_VERDICT" != LINK_FAIL
        d="$ev/trials/$swap_name"
        cmp -s "$out/$swap_target" "$ev/gcc/$swap_target" || {
          echo "NEXT_OWNER_SWAP=FAIL target=$swap_target reason=not_real_GCC"; exit 8;
        }
        mkdir -p "$d/uart-qmp"
        python3 "$repo/tools/ci/linux-runtime-qmp-pc-sampler-v1.py" \
          --image "$d/Image" --output-dir "$d/uart-qmp" \
          --timeout-seconds 39 --sample-seconds 1,4,8,12,20,32 --stack-words 48 "${qmp_snapshot_args[@]}" \
          --kernel-cmdline 'console=ttyS0 earlycon=uart8250,mmio,0x10000000,115200n8 loglevel=8 ignore_loglevel panic=-1' \
          >"$d/uart-qmp/qmp-probe.log" 2>&1 || {
            echo "NEXT_OWNER_SWAP=INCONCLUSIVE target=$swap_target reason=qmp_error"; exit 8;
          }
        cp -a "$d/uart-qmp/qemu-early-noinitrd.log" "$d/qemu-uart-earlycon.log"
        echo "NEXT_OWNER_SWAP_QMP_SAMPLES target=$swap_target"
        grep -E 'COHORT_QMP_EARLY_SAMPLE|COHORT_QMP_EARLY_COMPLETE' "$d/uart-qmp/qmp-probe.log" || true
        echo "NEXT_OWNER_SWAP_BASELINE_QMP_SAMPLES target=$swap_target"
        grep -E 'COHORT_QMP_EARLY_SAMPLE|COHORT_QMP_EARLY_COMPLETE' "$ev/trials/earlycon_fixed_only/uart-qmp/qmp-probe.log" || true
        python3 "$repo/tools/ci/linux-runtime-qmp-stack-symbols-v1.py" \
          --jsonl "$d/uart-qmp/qemu-registers.jsonl" \
          --vmlinux "$out/vmlinux" \
          --output "$d/uart-qmp/stack-symbols.txt" || true
        echo "NEXT_OWNER_SWAP_REGISTER_SYMBOLS target=$swap_target"
        grep -E 'REGISTER_LATE_DIVERSITY|REGISTER_CODE' "$d/uart-qmp/stack-symbols.txt" || true
        echo "NEXT_OWNER_SWAP_BASELINE target=$swap_target"
        grep -aE '^\[[[:space:]]*[0-9]+\.[0-9]+\]' "$baseline" | tail -n 5 || true
        echo "NEXT_OWNER_SWAP_OBSERVED target=$swap_target"
        grep -aE '^\[[[:space:]]*[0-9]+\.[0-9]+\]' "$d/qemu-uart-earlycon.log" | tail -n 18 || true
        if [[ "${COHORT_GDB_SWAP:-0}" == 1 ]]; then
          mkdir -p "$d/gdb-boundary"
          echo "NEXT_OWNER_GDB=START target=$swap_target same_image=true"
          python3 "$repo/tools/ci/linux-runtime-gdb-boundary-v1.py" \
            --image "$d/Image" --vmlinux "$out/vmlinux" \
            --output-dir "$d/gdb-boundary" --timeout-seconds 55 || {
              echo "NEXT_OWNER_GDB=INCONCLUSIVE target=$swap_target"
            }
          echo "NEXT_OWNER_GDB=END target=$swap_target"
        fi
        candidate_uart="$d/qemu-uart-earlycon.log"
        for pattern in 'genl_op_from_split' 'Unable to handle kernel paging request' 'Kernel panic' 'Run /init as init process'; do
          old_count=$(grep -Fca "$pattern" "$baseline" || true)
          new_count=$(grep -Fca "$pattern" "$candidate_uart" || true)
          echo "NEXT_FAULT_DELTA target=$swap_target pattern=$pattern baseline=$old_count gcc_swap=$new_count"
        done
        echo "NEXT_OWNER_SWAP=SCREENED target=$swap_target verdict=$TRIAL_VERDICT reboot_certificate=false"
        if [[ "${COHORT_NEXT_GCC_EXPLICIT_INITRD:-0}" == 1 ]]; then
          # Reuse the ALREADY linked one-GCC-owner Image. The wrapper only
          # relocates initrd into free guest RAM and updates chosen/ DTB.
          # Unlike kernel-only QMP, this executes the real /init contract.
          wrapper="$repo/tests/external/linux/qemu_explicit_initrd_wrapper.py"
          test -x "$wrapper"
          for boot_profile in fast p1; do
            boot_dir="$d/qemu-explicit-$boot_profile"
            boot_log="$d/runtime-explicit-$boot_profile.log"
            boot_rc=0
            echo "NEXT_OWNER_EXPLICIT_INITRD=START target=$swap_target profile=$boot_profile"
            LINUX_IMAGE="$d/Image" INITRAMFS="$ev/runtime-initramfs.cpio.gz" \
              BUILD_DIR="$boot_dir" LINUX_RELEASE=6.6.143 \
              LINUX_RUNTIME_PROFILE="$boot_profile" QEMU_TIMEOUT_SECONDS=65 \
              LINUX_RUNTIME_EARLYCON_ARG=earlycon=uart8250,mmio,0x10000000,115200n8 \
              QEMU_SYSTEM_RISCV64="$wrapper" \
              QEMU_REAL_SYSTEM_RISCV64=qemu-system-riscv64 \
              QEMU_EXPLICIT_DTB_DIR="$d/dtb-explicit-$boot_profile" \
              bash "$repo/tests/external/linux/runtime_boot.sh" \
              >"$boot_log" 2>&1 || boot_rc=$?
            if ((boot_rc == 0)); then
              echo "NEXT_OWNER_EXPLICIT_INITRD=PASS target=$swap_target profile=$boot_profile"
            else
              echo "NEXT_OWNER_EXPLICIT_INITRD=NOT_CERTIFIED target=$swap_target profile=$boot_profile rc=$boot_rc"
              grep -aE 'Linux version|devtmpfs:|Run /init|Starting init:|INIT_|RUNTIME_V2|Kernel panic|VFS:' \
                "$boot_dir/rdinit-init.log" | tail -n 35 || true
              break
            fi
          done
        fi
      done
    fi
    exit 0
  fi
  echo "EARLYCON_HOTFIX_RESULT=INCONCLUSIVE verdict=$TRIAL_VERDICT frontier_evidence=insufficient"
  exit 8
fi
# Exact immutable 2064-owner P1 differential: reuse certified MiniC .o pool
# and pinned GCC reference (no C compilation), overlay one *GCC* .o per
# trial, allow Kbuild to regenerate only derived link artifacts, then run the
# original P1 syscall oracle. This is a causal diagnostic, NOT a MiniC PASS.
if [[ "${COHORT_P1_STACK_OWNER_SWAPS:-0}" == 1 ]]; then
  [[ "$cohort_scope" == FULL_C_UNIVERSE && "$n" == 2064 && "${COHORT_ACTION:-all}" == verify ]] || {
    echo "COHORT_P1_OWNER=ERROR require_frozen_full_2064_verify" >&2
    exit 8
  }
  # The QEMU 8.2 -initrd placement is hard-coded at entry+128MiB and
  # overlaps this 215MB image. The stock QEMU virt DTB with explicit
  # /chosen initrd location allows exactly the same P1 guest oracle.
  p1dtb="$ev/p1-owner-virt.dtb"
  command -v fdtget >/dev/null
  command -v fdtput >/dev/null
  qemu-system-riscv64 -M "virt,dumpdtb=$p1dtb" -cpu max \
    -m 2G -smp 1 -nographic -no-reboot -bios default \
    -kernel "$gold_snapshot/arch/riscv/boot/Image" \
    >"$ev/p1-owner-dtb.log" 2>&1
  test -s "$p1dtb"
  fdtget -p "$p1dtb" /chosen >/dev/null 2>&1 || fdtput -c "$p1dtb" /chosen
  initrd_bytes=$(stat -c '%s' "$ev/runtime-initramfs.cpio.gz")
  printf -v p1_end '%x' "$((0xa0000000+initrd_bytes))"
  fdtput -t x "$p1dtb" /chosen linux,initrd-start 0 a0000000
  fdtput -t x "$p1dtb" /chosen linux,initrd-end 0 "$p1_end"
  [[ "$(fdtget -t x "$p1dtb" /chosen linux,initrd-start)" == "0 a0000000" ]] || exit 8
  export LINUX_RUNTIME_CUSTOM_DTB="$p1dtb"
  export LINUX_RUNTIME_INITRD_LOAD_ADDR=0xa0000000
  export LINUX_RUNTIME_QEMU_MEMORY=2G
  echo "COHORT_P1_OWNER_DTB=PASS guest_mem=2G initrd_start=a0000000 initrd_end=$p1_end"

  # Identify source object(s) for the precise panic symbols from the
  # previous 2/2 reproductions before interpreting individual swaps.
  symbol_map="$ev/p1-owner-symbols.txt"
  find "$ev/minic" -name '*.o' -type f -print0 \
    | xargs -0 -r riscv64-linux-gnu-nm -A --defined-only \
      >"$symbol_map" 2>"$ev/p1-owner-nm.log" || true
  grep -E ' (__list_add_valid_or_report|__minic_inline_spec_163_16339)' \
    "$symbol_map" | tee "$ev/p1-owner-hit.txt" || true
  if [[ -s "$ev/minic/lib/list_debug.o" ]]; then
    riscv64-linux-gnu-objdump -dr "$ev/minic/lib/list_debug.o" \
      >"$ev/p1-list-debug-disasm.txt"
    python3 - "$ev/p1-list-debug-disasm.txt" <<'PY_FRAME_PROLOGUE'
from pathlib import Path
import sys
lines=Path(sys.argv[1]).read_text().splitlines()
for name in ("__list_add_valid_or_report",):
    for idx,line in enumerate(lines):
        if line.strip().endswith("<"+name+">:"):
            print("COHORT_P1_OWNER_PROLOGUE symbol="+name)
            for row in lines[idx:idx+55]:
                print(row)
            break
PY_FRAME_PROLOGUE
  fi

  : >"$ev/p1-owner-verdicts.tsv"
  printf 'owner\tverdict\timage_sha\n' >"$ev/p1-owner-verdicts.tsv"
  # Frozen full-MiniC kernel must reproduce the observed P1 failure
  # in the same job and guest configuration before making causal claims.
  trial p1_full_minic "$n" p1
  baseline_verdict="$TRIAL_VERDICT"
  baseline_image_sha="$image_sha"
  if [[ "${COHORT_P1_IDENTICAL_IMAGE_REPEAT:-0}" == 1 ]]; then
    # Fresh QEMU process each time; no relink, no source changes.
    # Repeated success/failure on ONE IMAGE distinguishes runtime scheduling
    # sensitivity from invalid across-run source/object comparisons.
    printf 'trial\tprofile\timage_sha\tverdict\n' >"$ev/p1-identical-image-repeats.tsv"
    printf 'p1_full_minic\tbaseline\t%s\t%s\n' "$image_sha" "$TRIAL_VERDICT" >>"$ev/p1-identical-image-repeats.tsv"
    for attempt in 2 3; do
      trial "p1_full_minic_repeat_$attempt" "$n" p1
      [[ "$image_sha" == "$baseline_image_sha" ]] || {
        echo "COHORT_REPEAT_IMAGE=FAIL class=baseline_sha_changed attempt=$attempt"; exit 8;
      }
      printf 'p1_full_minic_repeat_%s\tbaseline\t%s\t%s\n' "$attempt" "$image_sha" "$TRIAL_VERDICT" >>"$ev/p1-identical-image-repeats.tsv"
      if [[ "$TRIAL_VERDICT" != "$baseline_verdict" ]]; then
        echo "COHORT_REPEAT_IMAGE=VARIABLE profile=baseline image=$image_sha attempt=$attempt"
      fi
    done
  fi
  TRIAL_VERDICT="$baseline_verdict"
  if [[ "${COHORT_P1_GDB_STACK:-0}" == 1 ]]; then
    echo "COHORT_GDB_STACK=START frozen_image_sha=$baseline_image_sha"
    # The trace attaches to the SAME exact linked Image and vmlinux whose
    # P1 failure just reproduced. Never edit any compiler object.
    bash "$repo/tools/ci/linux-runtime-p1-gdb-stack-v1.sh" \
      "$ev/trials/p1_full_minic/Image" "$out/vmlinux" \
      "$ev/runtime-initramfs.cpio.gz" "$LINUX_RUNTIME_CUSTOM_DTB" \
      "$ev/p1-gdb-stack" 65 || echo "COHORT_GDB_STACK=INCONCLUSIVE debugger_probe_non_certifying=true"
    echo "COHORT_GDB_STACK=CAPTURED evidence_only=true no_new_link=true"
    exit 0
  fi
  if [[ "$TRIAL_VERDICT" != FAIL ]]; then
    echo "COHORT_P1_OWNER=INCONCLUSIVE baseline=$TRIAL_VERDICT"
    exit 8
  fi
  echo "COHORT_P1_OWNER_BASELINE=REPRODUCED verdict=$TRIAL_VERDICT"
  if [[ "${COHORT_P1_MULTI_RESCUE:-0}" == 1 ]]; then
    cat >"$ev/p1_gcc_rescue_set.txt" <<'GCC_RESCUE_OWNERS'
net/ipv6/ip6_output.o
net/ipv6/tcp_ipv6.o
net/ipv4/tcp_output.o
net/core/dev.o
GCC_RESCUE_OWNERS
    sort -u -o "$ev/p1_gcc_rescue_set.txt" "$ev/p1_gcc_rescue_set.txt"
    # Strictly exact 4 objects from the frozen full-C inventory.
    python3 - "$ev/p1_gcc_rescue_set.txt" "$objects_file" <<'PY_RESCUE_SET'
from pathlib import Path
import sys
bad=set(Path(sys.argv[1]).read_text().splitlines())
allc={x for x in Path(sys.argv[2]).read_text().splitlines()
      if x.strip() and not x.lstrip().startswith("#")}
assert len(bad)==4 and bad<=allc, "unknown/duplicate GCC rescue owners"
print(f"COHORT_P1_MULTI_RESCUE=OWNERS_VERIFIED gcc={len(bad)} minic={len(allc-bad)}")
PY_RESCUE_SET
    printf 'trial\timage_sha\tverdict\n' >"$ev/p1-multi-rescue-repeats.tsv"
    rescue_sha=""
    pass_count=0
    for attempt in 1 2 3; do
      trial "p1_multi_gcc_rescue_$attempt" "$n" p1 exclude_list
      [[ -z "$rescue_sha" ]] && rescue_sha="$image_sha"
      [[ "$image_sha" == "$rescue_sha" ]] || {
        echo "COHORT_P1_MULTI_RESCUE=INCONCLUSIVE image_sha_drift"; exit 8;
      }
      printf '%s\t%s\t%s\n' "$attempt" "$image_sha" "$TRIAL_VERDICT" >>"$ev/p1-multi-rescue-repeats.tsv"
      if [[ "$TRIAL_VERDICT" == PASS ]]; then ((++pass_count)); fi
    done
    if ((pass_count==3)); then
      echo "COHORT_P1_MULTI_RESCUE=PASS gcc_owners=4 minic_owners=2060 same_image=true trials=3 full_minic_certified=false"
      exit 0
    fi
    echo "COHORT_P1_MULTI_RESCUE=FAIL passes=$pass_count attempts=3 full_minic_certified=false"
    exit 9
  fi
  # The panic's exact return-address symbol was resolved by 38212502667
  # to net/core/dev.o (NOT tcp_ipv4.o). Probe known owners first.
  p1_probe_owners_string="${COHORT_P1_SWAP_OWNERS:-lib/list_debug.o net/core/dev.o net/ipv6/tcp_ipv6.o}"
  for p1_probe_owner in $p1_probe_owners_string; do
    owner_idx=-1
    for ((i=0;i<n;i++)); do
      if [[ "${objects[i]}" == "$p1_probe_owner" ]]; then owner_idx=$i; break; fi
    done
    if ((owner_idx<0)); then
      echo "COHORT_P1_OWNER=SKIPPED missing_owner=$p1_probe_owner"
      printf '%s\t%s\t%s\n' "$p1_probe_owner" ABSENT - >>"$ev/p1-owner-verdicts.tsv"
      continue
    fi
    name=$(printf '%s' "$p1_probe_owner" | tr '/.' '__')
    echo "COHORT_P1_OWNER_SWAP=START owner=$p1_probe_owner index=$owner_idx others_minic=$((n-1))"
    trial "p1_gcc_$name" "$owner_idx" p1 all_except
    first_swap_verdict="$TRIAL_VERDICT"
    swap_image_sha="$image_sha"
    if [[ "${COHORT_P1_IDENTICAL_IMAGE_REPEAT:-0}" == 1 ]]; then
      printf 'p1_gcc_%s\tswap\t%s\t%s\n' "$name" "$image_sha" "$TRIAL_VERDICT" >>"$ev/p1-identical-image-repeats.tsv"
      swap_variable=0
      for attempt in 2 3; do
        trial "p1_gcc_${name}_repeat_$attempt" "$owner_idx" p1 all_except
        [[ "$image_sha" == "$swap_image_sha" ]] || {
          echo "COHORT_REPEAT_IMAGE=FAIL class=swap_sha_changed attempt=$attempt"; exit 8;
        }
        printf 'p1_gcc_%s_repeat_%s\tswap\t%s\t%s\n' "$name" "$attempt" "$image_sha" "$TRIAL_VERDICT" >>"$ev/p1-identical-image-repeats.tsv"
        if [[ "$TRIAL_VERDICT" != "$first_swap_verdict" ]]; then
          swap_variable=1
          echo "COHORT_REPEAT_IMAGE=VARIABLE profile=swap owner=$p1_probe_owner image=$image_sha attempt=$attempt"
        fi
      done
      if [[ "$swap_variable" == 1 ]]; then
        TRIAL_VERDICT=VARIABLE
      else
        TRIAL_VERDICT="$first_swap_verdict"
      fi
    fi
    printf '%s\t%s\t%s\n' "$p1_probe_owner" "$TRIAL_VERDICT" "$image_sha" \
      >>"$ev/p1-owner-verdicts.tsv"
    echo "COHORT_P1_OWNER_SWAP=RESULT owner=$p1_probe_owner verdict=$TRIAL_VERDICT"
    if [[ "$TRIAL_VERDICT" == PASS ]]; then
      echo "COHORT_P1_OWNER=ONE_GCC_SWAP_P1_PASS owner=$p1_probe_owner original_all_minic=FAIL"
      exit 0
    fi
  done
  echo "COHORT_P1_OWNER=UNRESOLVED baseline=FAIL attempts=$(($(wc -l <"$ev/p1-owner-verdicts.tsv")-1))"
  exit 9
fi
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
  *)
    # A too-large Image triggers a host QEMU initrd ROM overlap before the
    # kernel can run. Reuse authenticated MiniC objects and fixed GCC objects
    # to probe specific Linux early-init dependencies without rebuilding C.
    # These probes NEVER claim to satisfy the full runtime/initramfs oracle.
    if [[ "$cohort_scope" == FULL_C_UNIVERSE ]] && \
       grep -Fq "COHORT_QEMU_LOAD_OVERLAP=CONFIRMED trial=full_all" "$ev/cohort.log"; then
      for focused_target in drivers/tty/serial/earlycon.o; do
        focused_idx=-1
        for ((k=0;k<n;k++)); do
          if [[ "${objects[k]}" == "$focused_target" ]]; then
            focused_idx=$k
            break
          fi
        done
        if ((focused_idx<0)); then
          echo "COHORT_FOCUSED_GCC_SWAP=SKIPPED target=$focused_target reason=outside_golden_C_universe"
          continue
        fi
        probe_name=$(printf '%s' "$focused_target" | tr '/.' '__')
        echo "COHORT_FOCUSED_GCC_SWAP=START target=$focused_target index=$focused_idx other_minic=$((n-1))"
        trial "gcc_only_${probe_name}" "$focused_idx" fast all_except
        echo "COHORT_FOCUSED_GCC_SWAP=RESULT target=$focused_target verdict=$TRIAL_VERDICT"
      done
    fi
    echo "COHORT_RESULT=INCONCLUSIVE at=full"
    exit 8 ;;
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
