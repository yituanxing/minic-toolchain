#!/usr/bin/env bash
set -Eeuo pipefail

if [[ $# -ne 7 ]]; then
  echo "usage: $0 <cohort> <completed-cohort> <linux-src> <out> <wrapper> <minic> <evidence-dir>" >&2
  exit 64
fi

cohort=$1
completed=$2
src=$3
out=$4
wrapper=$5
minic=$6
ev=$7
manifest="$(cd "$(dirname "$0")" && pwd)/linux-expanded-owner-targets-v1.txt"

if [[ ! "$cohort" =~ ^[1-4]$ || ! "$completed" =~ ^[0-4]$ ]]; then
  echo "invalid cohort/completed: cohort=$cohort completed=$completed" >&2
  exit 64
fi

mkdir -p "$ev"
mapfile -t targets < <(awk -v c="$cohort" '$1 == c { print $2 }' "$manifest")
if [[ ${#targets[@]} -eq 0 ]]; then
  echo "owner cohort $cohort has no targets" >&2
  exit 65
fi

printf 'OWNER_COHORT=%s completed_before=%s targets=%s\n' "$cohort" "$completed" "${#targets[@]}" \
  | tee "$ev/owner-c${cohort}-summary.txt"

if (( completed >= cohort )); then
  echo "OWNER_COHORT_${cohort}=SKIP_RESTORED" | tee -a "$ev/owner-c${cohort}-summary.txt"
  if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
    printf 'changed=false\n' >>"$GITHUB_OUTPUT"
  fi
  exit 0
fi

start=$(date +%s)
MINIC="$minic" REAL_CC=/usr/bin/riscv64-linux-gnu-gcc \
MINIC_KEEP_INTERMEDIATES=0 MINIC_KBUILD_TRACE="$ev/owner-c${cohort}.trace" CORE_FAST_TRACE=1 \
  make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
    CC="$wrapper" -j4 V=1 "${targets[@]}" >"$ev/owner-c${cohort}.log" 2>&1
elapsed=$(($(date +%s) - start))
printf 'OWNER_COHORT_%s=PASS elapsed_s=%s targets=%s\n' "$cohort" "$elapsed" "${#targets[@]}" \
  | tee -a "$ev/owner-c${cohort}-summary.txt"
if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
  printf 'changed=true\n' >>"$GITHUB_OUTPUT"
fi
