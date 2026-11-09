#!/usr/bin/env bash
# Verify the already completed exact-Image RV64 P1 cohort, without relinking or rebooting.
set -Eeuo pipefail

ev="${1:?usage: linux-runtime-p1-strict12-evidence-v1.sh EVIDENCE_DIR}"
results="$ev/results.tsv"
log="$ev/cohort.log"
first="$ev/trials/p1_full_minic/Image"
test -s "$first" && test -s "$results" && test -s "$log"
test "$(wc -l < "$ev/minic-manifest.sha256")" -eq 2064
test "$(head -n 1 "$results")" = $'trial\tcount\tverdict\timage_sha'
test "$(wc -l < "$results")" -eq 13

image_sha="$(sha256sum "$first" | cut -d' ' -f1)"
[[ "$image_sha" =~ ^[0-9a-f]{64}$ ]]
marker="P1_STRICT_SAME_IMAGE=PASS minic_c=2064 boots=12/12 image_sha=$image_sha original_16KiB=true no_GCC_C_fallback=true"
test "$(grep -Fxc "$marker" "$log")" -eq 1

for attempt in $(seq 1 12); do
  if ((attempt == 1)); then
    name=p1_full_minic
  else
    name="p1_eight_control_$attempt"
  fi
  trial_dir="$ev/trials/$name"
  image="$trial_dir/Image"
  runtime="$trial_dir/runtime.log"
  guest="$trial_dir/qemu/rdinit-init.log"
  test -s "$image" && test -s "$runtime" && test -s "$guest"
  test "$(sha256sum "$image" | cut -d' ' -f1)" = "$image_sha"
  test "$(grep -Fxc "$name"$'\t'"2064"$'\t'"PASS"$'\t'"$image_sha" "$results")" -eq 1
  test "$(grep -Fxc "COHORT_TRIAL name=$name count=2064 verdict=PASS image=$image_sha" "$log")" -eq 1
  grep -Fq 'LINUX_RUNTIME_BOOT lane=rdinit-init qemu_status=0 ' "$runtime"
  grep -Fq 'LINUX_RUNTIME_EXACT=PASS release=6.6.143 profile=p1 lanes=1/1' "$runtime"
  python3 tests/external/linux/runtime_v2_log_check.py --profile p1 --log "$guest" \
    >"$trial_dir/p1-strict-oracle.log" 2>&1
  echo "RV64_STRICT_P1_EVIDENCE=PASS attempt=$attempt/12 name=$name image_sha256=$image_sha qemu_exit=0 oracle=PASS"
done

echo "RV64_STRICT_SAME_IMAGE=PASS miniC_C=2064 boots=12/12 syscalls_each=12/12 image_sha256=$image_sha original_16KiB=true no_GCC_C_fallback=true"
