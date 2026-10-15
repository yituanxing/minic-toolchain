#!/usr/bin/env bash
# Sourced by linux-runtime-gcc-cohort-bisect-v1.sh after the all-MiniC P1
# baseline has FAILED. Uses the SAME authenticated 2064 MiniC + GCC objects.
# Never promotes compiler changes or calls a single P1 PASS certification.
set -Eeuo pipefail
owners=(
  net/ipv6/ip6_output.o
  net/ipv6/tcp_ipv6.o
  net/ipv4/tcp_output.o
  net/core/dev.o
)
printf 'restored_minic_owner\tgcc_owner_count\timage_sha256\tpass_count\tfail_count\tverdicts\n' >"$ev/p1-rescue-minimize.tsv"
printf 'owner\tattempt\timage_sha256\tverdict\n' >"$ev/p1-rescue-minimize-repeats.tsv"
for restored_owner in "${owners[@]}"; do
  : >"$ev/p1_gcc_rescue_set.txt"
  for gcc_owner in "${owners[@]}"; do
    [[ "$gcc_owner" == "$restored_owner" ]] || printf '%s\n' "$gcc_owner" >>"$ev/p1_gcc_rescue_set.txt"
  done
  sort -u -o "$ev/p1_gcc_rescue_set.txt" "$ev/p1_gcc_rescue_set.txt"
  [[ $(wc -l <"$ev/p1_gcc_rescue_set.txt") -eq 3 ]] || exit 8
  owner_label=$(printf '%s' "$restored_owner" | tr '/.' '__')
  echo "COHORT_RESCUE_REDUCE=START restored_minic=$restored_owner gcc_owners=3"
  subset_sha=""
  pass_count=0
  fail_count=0
  verdicts=""
  for attempt in 1 2 3; do
    trial "p1_minimize_${owner_label}_$attempt" "$n" p1 exclude_list
    [[ -z "$subset_sha" ]] && subset_sha="$image_sha"
    [[ "$subset_sha" == "$image_sha" ]] || {
      echo "COHORT_RESCUE_REDUCE=ERROR image_drift restored=$restored_owner"; exit 8;
    }
    printf '%s\t%s\t%s\t%s\n' "$restored_owner" "$attempt" "$image_sha" "$TRIAL_VERDICT" >>"$ev/p1-rescue-minimize-repeats.tsv"
    verdicts="${verdicts:+$verdicts,}$TRIAL_VERDICT"
    if [[ "$TRIAL_VERDICT" == PASS ]]; then
      pass_count=$((pass_count+1))
    else
      fail_count=$((fail_count+1))
    fi
  done
  printf '%s\t3\t%s\t%s\t%s\t%s\n' "$restored_owner" "$subset_sha" "$pass_count" "$fail_count" "$verdicts" >>"$ev/p1-rescue-minimize.tsv"
  if ((pass_count==3)); then
    echo "COHORT_RESCUE_REDUCE=TRIPLE_PASS restored_minic=$restored_owner three_gcc=true image=$subset_sha"
  else
    echo "COHORT_RESCUE_REDUCE=NOT_CERTIFIED restored_minic=$restored_owner pass=$pass_count fail=$fail_count"
  fi
done
echo "COHORT_RESCUE_REDUCE=COMPLETE candidates=4 three_boots_each=true full_MiniC_certified=false"
exit 0
