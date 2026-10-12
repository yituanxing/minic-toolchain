#!/usr/bin/env bash
# 2026-10-12 P1 reduction phase 2. CI 38237171824 confirmed that
# ALL three 3-GCC sets containing ip6_output.o PASS (3/3), while the only
# 3-GCC set WITHOUT ip6_output.o FAILS (0/3).
# Exhaust every 2-GCC subset of those proven 3-GCC sets, retaining the
# common ip6_output.o. Leave all 2064 candidate objects SHA immutable.
set -Eeuo pipefail
anchor=net/ipv6/ip6_output.o
[[ "$n" -eq 2064 ]] || {
  echo "COHORT_P1_PAIR=ERROR expected_frozen_2064_objects got=$n"
  exit 8
}
partners=(
  net/ipv6/tcp_ipv6.o
  net/ipv4/tcp_output.o
  net/core/dev.o
)
printf 'two_gcc_owners\timage_sha256\tpass_count\tfail_count\tverdicts\n' >"$ev/p1-rescue-pairs.tsv"
printf 'two_gcc_owners\tattempt\timage_sha256\tverdict\n' >"$ev/p1-rescue-pairs-repeats.tsv"
for partner in "${partners[@]}"; do
  printf '%s\n%s\n' "$anchor" "$partner" | sort -u >"$ev/p1_gcc_rescue_set.txt"
  [[ $(wc -l <"$ev/p1_gcc_rescue_set.txt") -eq 2 ]] || exit 8
  candidate="$anchor,$partner"
  tag=$(printf '%s' "$partner" | tr '/.' '__')
  echo "COHORT_P1_PAIR=START gcc_owners=$candidate minic_owners=$((n-2))"
  image_pin=""
  pass_count=0
  fail_count=0
  verdicts=""
  for attempt in 1 2 3; do
    trial "p1_pair_${tag}_$attempt" "$n" p1 exclude_list
    [[ -z "$image_pin" ]] && image_pin="$image_sha"
    [[ "$image_sha" == "$image_pin" ]] || {
      echo "COHORT_P1_PAIR=ERROR image_digest_changed candidate=$candidate"; exit 8;
    }
    printf '%s\t%s\t%s\t%s\n' "$candidate" "$attempt" "$image_sha" "$TRIAL_VERDICT" >>"$ev/p1-rescue-pairs-repeats.tsv"
    verdicts="${verdicts:+$verdicts,}$TRIAL_VERDICT"
    if [[ "$TRIAL_VERDICT" == PASS ]]; then
      pass_count=$((pass_count+1))
    else
      fail_count=$((fail_count+1))
    fi
  done
  printf '%s\t%s\t%s\t%s\t%s\n' "$candidate" "$image_pin" "$pass_count" "$fail_count" "$verdicts" >>"$ev/p1-rescue-pairs.tsv"
  if ((pass_count==3)); then
    echo "COHORT_P1_PAIR=TRIPLE_PASS gcc_owners=$candidate minic_owners=$((n-2)) image=$image_pin"
  else
    echo "COHORT_P1_PAIR=NOT_CERTIFIED gcc_owners=$candidate pass=$pass_count fail=$fail_count"
  fi
done
echo "COHORT_P1_PAIR=COMPLETE candidates=3 boots_per_image=3 full_MiniC_certified=false"
exit 0
