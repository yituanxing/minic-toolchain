#!/usr/bin/env bash
# Diagnostic only: never count an Image containing GNU-AS objects as MiniAS PASS.
# Reuses the exact MiniAS Kbuild directory and the GNU-AS sidecars assembled
# from the same GCC .s while those temporary sources existed.
set -Eeuo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$root"
work=${WORK_DIR:?}
out=${MINIAS_KBUILD_OUT:?}
gnu=${MINIAS_GNU_OBJECT_DIR:?}
image="$out/arch/arm64/boot/Image"
src="$work/linux-6.6.143"
originals="$work/minias-original-objects"
production_image="$work/aarch64-minias-production.Image"
cc=${AARCH64_CC:-aarch64-linux-gnu-gcc}
qemu=${AARCH64_QEMU:-qemu-system-aarch64}
bindir="$work/as-override"
initramfs="$work/runtime-initramfs.cpio.gz"
summary="$work/aarch64-object-bisect.tsv"
max_probes=${MINIAS_AARCH64_MAX_BISECT_PROBES:-24}

mkdir -p "$originals"
# Diagnostic probes rewrite Kbuild objects and Image temporarily. Restore
# the original MiniAS-only evidence before Actions uploads any artifacts:
# otherwise the archived Image could silently become a GNU-mixed trial.
cp -- "$image" "$production_image"
declare -a changed=()
restore_production() {
  local rel
  for rel in "${changed[@]}"; do
    if [[ -f "$originals/$rel" ]]; then
      cp -- "$originals/$rel" "$out/$rel" || true
    fi
  done
  cp -- "$production_image" "$image" || true
}
trap restore_production EXIT
printf 'trial\tgnu_objects\tresult\tlast_object\n' >"$summary"
declare -A installed=()
while IFS= read -r -d '' ref; do
  rel=${ref#"$gnu/"}
  candidate="$out/$rel"
  [[ -f $candidate ]] || continue
  if ! cmp -s "$ref" "$candidate"; then
    changed+=("$rel")
    mkdir -p "$(dirname "$originals/$rel")"
    cp -- "$candidate" "$originals/$rel"
    installed["$rel"]=mini
  fi
done < <(find "$gnu" -type f -name '*.o' -print0 | sort -z)

count=${#changed[@]}
echo "MINIAS_AARCH64_BISECT=START changed_objects=$count mode=diagnostic_only"
if ((count == 0)); then
  echo 'MINIAS_AARCH64_BISECT=UNAVAILABLE no_distinct_reference_objects'
  exit 0
fi

trial=0
probe() {
  local label=$1
  shift
  local selected=("$@")
  local rel desired status=0
  declare -A selected_set=()
  for rel in "${selected[@]}"; do selected_set["$rel"]=1; done
  for rel in "${changed[@]}"; do
    desired=mini
    [[ -n ${selected_set["$rel"]:-} ]] && desired=gnu
    [[ ${installed["$rel"]} == "$desired" ]] && continue
    if [[ $desired == gnu ]]; then
      cp -- "$gnu/$rel" "$out/$rel"
    else
      cp -- "$originals/$rel" "$out/$rel"
    fi
    installed["$rel"]=$desired
  done

  trial=$((trial+1))
  local build_log="$work/aarch64-bisect-$trial-kbuild.log"
  local boot_log="$work/aarch64-bisect-$trial-qemu.log"
  if ! make -C "$src" O="$out" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
    CC="$cc -B$bindir/" AS="$bindir/aarch64-linux-gnu-as" \
    -j4 Image >"$build_log" 2>&1; then
    printf '%s\t%s\tLINK_FAIL\t%s\n' "$trial" "${#selected[@]}" "$label" >>"$summary"
    echo "MINIAS_AARCH64_BISECT_TRIAL=$trial selected=${#selected[@]} result=LINK_FAIL label=$label"
    return 1
  fi
  timeout --signal=TERM "${MINIAS_AARCH64_BISECT_QEMU_TIMEOUT:-20}" "$qemu" \
    -M virt -cpu cortex-a57 -m 1G -smp 1 -nographic -no-reboot -nic none \
    -kernel "$image" -initrd "$initramfs" \
    -append 'console=ttyAMA0 earlycon=pl011,0x9000000 loglevel=8 panic=-1 rdinit=/init' \
    </dev/null >"$boot_log" 2>&1 || status=$?
  if grep -q 'Linux version 6.6.143' "$boot_log"; then
    printf '%s\t%s\tEARLY_BOOT_PASS\t%s\n' "$trial" "${#selected[@]}" "$label" >>"$summary"
    echo "MINIAS_AARCH64_BISECT_TRIAL=$trial selected=${#selected[@]} result=EARLY_BOOT_PASS label=$label qemu=$status"
    return 0
  fi
  printf '%s\t%s\tEARLY_BOOT_FAIL\t%s\n' "$trial" "${#selected[@]}" "$label" >>"$summary"
  echo "MINIAS_AARCH64_BISECT_TRIAL=$trial selected=${#selected[@]} result=EARLY_BOOT_FAIL label=$label qemu=$status"
  return 1
}

if ! probe all-gnu "${changed[@]}"; then
  echo 'MINIAS_AARCH64_BISECT=INCONCLUSIVE all_GNU_sidecars_did_not_recover_early_boot'
  exit 0
fi

# This is reduction of a *diagnostic* repair set, not acceptance.  If both
# halves fail, there are multiple interacting bad objects (or link-order
# effects); leave them all visible instead of falsely blaming a single .o.
declare -a remaining=("${changed[@]}")
while ((${#remaining[@]} > 1 && trial + 2 <= max_probes)); do
  half=$(( (${#remaining[@]} + 1) / 2 ))
  left=("${remaining[@]:0:half}")
  right=("${remaining[@]:half}")
  if probe left-half "${left[@]}"; then
    remaining=("${left[@]}")
  elif probe right-half "${right[@]}"; then
    remaining=("${right[@]}")
  else
    echo "MINIAS_AARCH64_BISECT=MULTIPLE_OR_INTERACTING min_required_group=${#remaining[@]}"
    printf '%s\n' "${remaining[@]}" >"$work/aarch64-bisect-unresolved-objects.txt"
    exit 0
  fi
done

printf '%s\n' "${remaining[@]}" >"$work/aarch64-bisect-suspect-objects.txt"
echo "MINIAS_AARCH64_BISECT=REDUCED suspicious_objects=${#remaining[@]} probes=$trial"
printf 'MINIAS_AARCH64_BISECT_OBJECT=%s\n' "${remaining[@]}"
