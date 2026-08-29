#!/usr/bin/env bash
set -Eeuo pipefail

manifest=${1:?usage: run_linux_exact_batch.sh MANIFEST [SAMPLE_MANIFEST]}
sample_manifest=${2:-}

src=${MINIPP_LINUX_SRC:?MINIPP_LINUX_SRC is not set}
out=${MINIPP_LINUX_OUT:?MINIPP_LINUX_OUT is not set}
trace=${MINIPP_LINUX_TRACE:?MINIPP_LINUX_TRACE is not set}
work=${MINIPP_LINUX_WORK:?MINIPP_LINUX_WORK is not set}
cc_wrapper=${MINIPP_LINUX_CC_WRAPPER:?MINIPP_LINUX_CC_WRAPPER is not set}
jobs=${MINIPP_LINUX_JOBS:-4}

if [[ ! -s "$manifest" ]]; then
  printf 'MINIPP_LINUX_BATCH=ERROR reason=empty-manifest manifest=%s\n' "$manifest" >&2
  exit 2
fi

mapfile -t sources < <(grep -Ev '^[[:space:]]*(#|$)' "$manifest")
targets=()
for source in "${sources[@]}"; do
  [[ "$source" == *.c ]] || {
    printf 'MINIPP_LINUX_BATCH=ERROR reason=non-c-source source=%s\n' "$source" >&2
    exit 2
  }
  target=${source%.c}.o
  targets+=("$target")
  rm -f "$out/$target" "$out/$(dirname "$target")/.$(basename "$target").cmd"
done

: >"$trace"
export MINIPP_LINUX_SELECT_FILE="$manifest"
printf 'MINIPP_LINUX_BATCH_JOBS=%s\n' "$jobs"

make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
  CC="$cc_wrapper" -j"$jobs" V=1 "${targets[@]}" >"$work/kbuild-batch.log" 2>&1

selected=$(grep -c '^RESULT ' "$trace" || true)
exact=$(grep -c ' status=EXACT' "$trace" || true)
fail=$(grep -c ' status=MINIPP_FAIL' "$trace" || true)
diff=$(grep -c ' status=DIFF' "$trace" || true)
expected=${#sources[@]}

failure_pool="$work/failure-pool.txt"
: >"$failure_pool"
for source in "${sources[@]}"; do
  if ! grep -Fq "source=$src/$source status=EXACT" "$trace"; then
    printf '%s\n' "$source" >>"$failure_pool"
  fi
done

printf 'MINIPP_LINUX_BATCH selected=%s exact=%s minipp_fail=%s diff=%s expected=%s\n' \
  "$selected" "$exact" "$fail" "$diff" "$expected"
grep '^RESULT ' "$trace" || true
printf 'MINIPP_LINUX_FAILURE_POOL count=%s\n' "$(wc -l <"$failure_pool")"

if [[ -n "$sample_manifest" ]]; then
  sample_total=0
  sample_exact=0
  sample_fail=0
  sample_diff=0
  while IFS= read -r source; do
    [[ -n "$source" ]] || continue
    [[ "$source" == \#* ]] && continue
    ((++sample_total))
    if grep -Fq "source=$src/$source status=EXACT" "$trace"; then
      ((++sample_exact))
    elif grep -F "source=$src/$source " "$trace" | grep -Fq ' status=MINIPP_FAIL'; then
      ((++sample_fail))
    elif grep -F "source=$src/$source " "$trace" | grep -Fq ' status=DIFF'; then
      ((++sample_diff))
    fi
  done <"$sample_manifest"
  printf 'MINIPP_LINUX_SAMPLE selected=%s exact=%s minipp_fail=%s diff=%s\n' \
    "$sample_total" "$sample_exact" "$sample_fail" "$sample_diff"
fi

test "$selected" -eq "$expected"
