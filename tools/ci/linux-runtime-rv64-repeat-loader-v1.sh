#!/usr/bin/env bash
# Source from the exact-HEAD RV64 twelve-boot workflow. The preceding cohort
# already generated and verified a QEMU virt DTB for this identical Image.
# Reuse it when the original QEMU -initrd placement would overlap the Image;
# never regenerate Linux objects, alter guest bytes, or relax P1 assertions.

minic_rv64_repeat_loader_classify() {
  local image_bytes=${1:-} initrd_bytes=${2:-}
  local default_limit=$((128 * 1024 * 1024))
  local kernel_base=$((0x80200000))
  local relocated_initrd=$((0xa0000000))
  local guest_end=$((0x100000000))
  [[ "$image_bytes" =~ ^[0-9]+$ && "$initrd_bytes" =~ ^[0-9]+$ ]] || return 2
  ((image_bytes > 0 && initrd_bytes > 0)) || return 2
  if ((image_bytes <= default_limit)); then
    printf 'default\n'
  elif ((kernel_base + image_bytes <= relocated_initrd &&
          relocated_initrd + initrd_bytes < guest_end)); then
    printf 'large\n'
  else
    return 2
  fi
}

minic_rv64_repeat_loader_select() {
  local ev=$1 image=$2 initramfs=$3 image_sha=$4
  local image_bytes initrd_bytes mode dtb expected_end actual_end
  [[ -s "$image" && -s "$initramfs" && -s "$ev/cohort.log" ]] || {
    echo 'RV64_REPEAT_LOADER=FAIL missing_image_initrd_or_cohort' >&2
    return 1
  }
  [[ "$(sha256sum "$image" | cut -d' ' -f1)" == "$image_sha" ]] || {
    echo 'RV64_REPEAT_LOADER=FAIL image_sha_mismatch' >&2
    return 1
  }
  image_bytes=$(stat -c '%s' "$image")
  initrd_bytes=$(stat -c '%s' "$initramfs")
  mode=$(minic_rv64_repeat_loader_classify "$image_bytes" "$initrd_bytes") || {
    echo "RV64_REPEAT_LOADER=FAIL unsafe_image_initrd_layout image_bytes=$image_bytes initrd_bytes=$initrd_bytes" >&2
    return 1
  }
  if [[ "$mode" == default ]]; then
    # Preserve the original GNU/QEMU 512MiB default for images that fit.
    unset LINUX_RUNTIME_CUSTOM_DTB LINUX_RUNTIME_INITRD_LOAD_ADDR LINUX_RUNTIME_QEMU_MEMORY
    echo "RV64_REPEAT_LOADER=PASS mode=default image_sha=$image_sha memory=512M original_initrd_placement=1"
    return 0
  fi

  dtb="$ev/trials/full_all_verified/full-crossblock-virt.dtb"
  [[ -s "$dtb" ]] && command -v fdtget >/dev/null 2>&1 &&
    grep -Fq "COHORT_LARGE_IMAGE_LOADER=PASS trial=full_all_verified image_sha=$image_sha " \
      "$ev/cohort.log" || {
    echo 'RV64_REPEAT_LOADER=FAIL missing_exact_cohort_large_loader_proof' >&2
    return 1
  }
  expected_end=$(printf '%x' $((0xa0000000 + initrd_bytes)))
  [[ "$(fdtget -t x "$dtb" /chosen linux,initrd-start)" == '0 a0000000' ]] &&
    actual_end=$(fdtget -t x "$dtb" /chosen linux,initrd-end) &&
    [[ "$actual_end" == "0 $expected_end" ]] || {
    echo "RV64_REPEAT_LOADER=FAIL mismatched_cohort_dtb_initrd_range expected_end=$expected_end" >&2
    return 1
  }
  export LINUX_RUNTIME_CUSTOM_DTB="$dtb"
  export LINUX_RUNTIME_INITRD_LOAD_ADDR=0xa0000000
  export LINUX_RUNTIME_QEMU_MEMORY=2G
  echo "RV64_REPEAT_LOADER=PASS mode=large image_sha=$image_sha memory=2G initrd_start=a0000000 original_image=1 original_initramfs=1"
}

minic_rv64_repeat_loader_selftest() {
  local max_default=$((128 * 1024 * 1024))
  [[ "$(minic_rv64_repeat_loader_classify 1 1)" == default ]] || return 1
  [[ "$(minic_rv64_repeat_loader_classify "$max_default" 1)" == default ]] || return 1
  [[ "$(minic_rv64_repeat_loader_classify "$((max_default + 1))" 1)" == large ]] || return 1
  [[ "$(minic_rv64_repeat_loader_classify 150491136 858708)" == large ]] || return 1
  if minic_rv64_repeat_loader_classify 600000000 1 >/dev/null 2>&1; then return 1; fi
  if minic_rv64_repeat_loader_classify 150491136 1610612736 >/dev/null 2>&1; then return 1; fi
  if minic_rv64_repeat_loader_classify NaN 1 >/dev/null 2>&1; then return 1; fi
  echo 'RV64_REPEAT_LOADER_SELFTEST=PASS default_boundary=128MiB large_image=150491136 unsafe_overlap=rejected invalid_sizes=rejected'
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  [[ "${1:-}" == --selftest ]] || { echo 'usage: source this script, or --selftest' >&2; exit 2; }
  minic_rv64_repeat_loader_selftest
fi
