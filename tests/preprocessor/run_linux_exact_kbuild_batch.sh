#!/usr/bin/env bash
set -Eeuo pipefail

manifest=${1:?usage: run_linux_exact_kbuild_batch.sh MANIFEST_TSV [SAMPLE_INDICES]}
sample_indices=${2:-}

src=${MINIPP_LINUX_SRC:?MINIPP_LINUX_SRC is not set}
out=${MINIPP_LINUX_OUT:?MINIPP_LINUX_OUT is not set}
trace=${MINIPP_LINUX_TRACE:?MINIPP_LINUX_TRACE is not set}
work=${MINIPP_LINUX_WORK:?MINIPP_LINUX_WORK is not set}
cc_wrapper=${MINIPP_LINUX_CC_WRAPPER:?MINIPP_LINUX_CC_WRAPPER is not set}
jobs=${MINIPP_LINUX_JOBS:-4}

[[ -s "$manifest" ]] || {
  printf 'MINIPP_LINUX_MAPPED_BATCH=ERROR reason=empty-manifest manifest=%s\n' "$manifest" >&2
  exit 2
}

mapfile -t rows < <(awk -F '\t' 'NR > 1 && NF >= 3 { print $1 "\t" $2 "\t" $3 }' "$manifest")
targets=()
for row in "${rows[@]}"; do
  IFS=$'\t' read -r index target source <<<"$row"
  [[ "$index" =~ ^[0-9]+$ && "$target" == *.o && "$source" == *.c ]] || {
    printf 'MINIPP_LINUX_MAPPED_BATCH=ERROR reason=bad-row row=%q\n' "$row" >&2
    exit 2
  }
  targets+=("$target")
  rm -f "$out/$target" "$out/$(dirname "$target")/.$(basename "$target").cmd"
done

: >"$trace"
export MINIPP_LINUX_SELECT_TSV="$manifest"
printf 'MINIPP_LINUX_MAPPED_BATCH_JOBS=%s\n' "$jobs"

make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
  CC="$cc_wrapper" -k -j"$jobs" V=1 "${targets[@]}" >"$work/kbuild-mapped-batch.log" 2>&1

selected=$(grep -c '^RESULT ' "$trace" || true)
exact=$(grep -c ' status=EXACT' "$trace" || true)
fail=$(grep -c ' status=MINIPP_FAIL' "$trace" || true)
diff=$(grep -c ' status=DIFF' "$trace" || true)
expected=${#rows[@]}

failure_pool="$work/failure-pool.tsv"
printf 'index\tobject\tsource\n' >"$failure_pool"
for row in "${rows[@]}"; do
  IFS=$'\t' read -r index target source <<<"$row"
  if ! grep -Fq "target=$target source=$src/$source status=EXACT" "$trace"; then
    printf '%s\t%s\t%s\n' "$index" "$target" "$source" >>"$failure_pool"
  fi
done

failure_count=$(( $(wc -l <"$failure_pool") - 1 ))
printf 'MINIPP_LINUX_MAPPED_BATCH selected=%s exact=%s minipp_fail=%s diff=%s expected=%s\n' \
  "$selected" "$exact" "$fail" "$diff" "$expected"
grep '^RESULT ' "$trace" || true
printf 'MINIPP_LINUX_MAPPED_FAILURE_POOL count=%s\n' "$failure_count"

if [[ -n "$sample_indices" ]]; then
  sample_total=0
  sample_exact=0
  sample_fail=0
  sample_diff=0
  while IFS= read -r index; do
    [[ -n "$index" ]] || continue
    [[ "$index" == \#* ]] && continue
    row=$(awk -F '\t' -v index="$index" 'NR > 1 && $1 == index { print $1 "\t" $2 "\t" $3; exit }' "$manifest")
    [[ -n "$row" ]] || {
      printf 'MINIPP_LINUX_MAPPED_SAMPLE=ERROR reason=index-not-found index=%s\n' "$index" >&2
      exit 2
    }
    IFS=$'\t' read -r _ target source <<<"$row"
    ((++sample_total))
    if grep -Fq "target=$target source=$src/$source status=EXACT" "$trace"; then
      ((++sample_exact))
    elif grep -F "target=$target source=$src/$source " "$trace" | grep -Fq ' status=MINIPP_FAIL'; then
      ((++sample_fail))
    elif grep -F "target=$target source=$src/$source " "$trace" | grep -Fq ' status=DIFF'; then
      ((++sample_diff))
    fi
  done <"$sample_indices"
  printf 'MINIPP_LINUX_MAPPED_SAMPLE selected=%s exact=%s minipp_fail=%s diff=%s\n' \
    "$sample_total" "$sample_exact" "$sample_fail" "$sample_diff"
fi

test "$selected" -eq "$expected"
