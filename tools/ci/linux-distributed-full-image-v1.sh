#!/usr/bin/env bash
# Full Linux 6.6.143 real-Kbuild object distribution experiment.
# This does NOT replace the existing certified Linux Image/QEMU production gates.
set -Eeuo pipefail
mode=${1:?usage: prepare|produce <shard>|image|qemu}
shard=${2:-}
root=${GITHUB_WORKSPACE:?}
work="$root/build/linux-distributed-full-image-v1"
src="$work/linux-6.6.143"
out="$work/out"
plan="$work/plan/manifest.tsv"
compiler="$work/toolchain/bin/minic"
wrapper="$root/tests/external/linux/stage2_kbuild_cc.sh"
mkdir -p "$work"
test -s "$plan" || { echo "DIST_IMAGE_MISSING_PLAN" >&2; exit 70; }
( cd "$work/plan"; test "$(sha256sum manifest.tsv | awk '{print $1}')" = "$(cat manifest.sha256)" )
total=$(wc -l <"$plan")
[[ "$total" -ge 2500 && "$total" -le 6000 ]] || { echo "DIST_IMAGE_INVALID_COUNT=$total" >&2; exit 70; }
[[ $(awk -F '\t' '{print $3}' "$plan" | sort -u | wc -l) -eq "$total" ]] || exit 70

case "$mode" in
  prepare)
    archive="$work/linux-6.6.143.tar.xz"
    curl -fsSL --retry 5 --retry-delay 2 --retry-all-errors \
      https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.6.143.tar.xz -o "$archive"
    printf '%s  %s\n' dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932 "$archive" | sha256sum -c -
    tar -xJf "$archive" -C "$work"
    make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- defconfig
    "$src/scripts/config" --file "$out/.config" \
      -e BLK_DEV_INITRD -e DEVTMPFS -e DEVTMPFS_MOUNT \
      -e SERIAL_8250 -e SERIAL_8250_CONSOLE
    make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- olddefconfig
    make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- -j4 prepare scripts
    sha256sum "$out/.config" | awk '{print $1}' >"$work/config.sha256"
    cmp "$work/config.sha256" <(awk '{print $1}' "$work/plan/config.sha256")
    bash "$root/tools/ci/linux-runtime-build-minic-profile-v1.sh" "$work/toolchain" "$work/profile"
    # The certified 39-patch profile alone cannot parse Nouveau's
    # __builtin_choose_expr(__builtin_constant_p(...)) ICE constructs.
    # Isolate this tested single correctness patch to the *experiment*,
    # never silently change the canonical Linux Image profile.
    python3 "$root/tools/ci/apply-correctness-constant-p-ice-v1.py" >"$work/profile/constant-p-ice.log"
    if [[ "${MINIC_DISTRIBUTED_FIX_NESTED_CONSTANT_P:-0}" == 1 ]]; then
      # Opt-in V2 full Image profile: unchanged production Runtime stays pinned.
      python3 "$root/tools/ci/apply-correctness-constant-p-core-local-facts-v1.py" >"$work/profile/constant-p-core-local.log"
      python3 "$root/tools/ci/apply-correctness-constant-p-nested-core-v2.py" >"$work/profile/constant-p-nested.log"
    fi
    git diff --check
    make -j4 MODE=release CFLAGS=-Werror BUILD_DIR="$work/toolchain" \
      "$work/toolchain/bin/minic" "$work/toolchain/bin/minic-cc" >/dev/null
    printf '%s\n' 'apply-correctness-constant-p-ice-v1.py' >>"$work/profile/minic-profile.txt"
    if [[ "${MINIC_DISTRIBUTED_FIX_NESTED_CONSTANT_P:-0}" == 1 ]]; then
      printf '%s\n' 'apply-correctness-constant-p-core-local-facts-v1.py' 'apply-correctness-constant-p-nested-core-v2.py' >>"$work/profile/minic-profile.txt"
      # Include the *compiler* and driver: old experiments hashed only minic,
      # which stays unchanged even when core_lower.c and minic-cc differ.
      ( cd "$work/toolchain"; sha256sum bin/minic bin/minic-cc ) | sha256sum | awk '{print $1}' >"$work/compiler.sha256"
      sha256sum "$work/toolchain/bin/minic-cc" >"$work/profile/minic-cc.sha256"
    else
      sha256sum "$compiler" | awk '{print $1}' >"$work/compiler.sha256"
    fi
    echo "DIST_IMAGE_PREPARE=PASS objects=$total config=$(cat "$work/config.sha256") compiler=$(cat "$work/compiler.sha256")"
    ;;
  produce)
    [[ "$shard" =~ ^[0-6]$ ]] || exit 70
    mapfile -t targets < <(awk -F '\t' -v id="$shard" '$2 == id {print $3}' "$plan")
    [[ ${#targets[@]} -gt 330 && ${#targets[@]} -lt 900 ]] || {
      echo "DIST_IMAGE_BAD_SHARD=$shard count=${#targets[@]}"; exit 70;
    }
    log="$work/produce-$shard.log"
    trace="$work/produce-$shard.trace"
    start=$(date +%s)
    # -k retains other failure evidence but any failed requested object is fatal.
    set +e
    MINIC="$compiler" REAL_CC=/usr/bin/riscv64-linux-gnu-gcc MINIC_KEEP_INTERMEDIATES=0 \
      MINIC_KBUILD_TRACE="$trace" CORE_FAST_TRACE=0 \
      make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
      CC="$wrapper" -k -j4 V=1 "${targets[@]}" >"$log" 2>&1
    rc=$?
    set -e
    if [[ $rc -ne 0 ]]; then
      echo "DIST_IMAGE_PRODUCE=FAIL shard=$shard make_rc=$rc" >&2
      tail -n 120 "$log" >&2
      exit "$rc"
    fi
    bundle="$work/bundle"
    mkdir -p "$bundle/out"
    cp "$work/config.sha256" "$work/compiler.sha256" "$bundle/"
    cp "$work/plan/manifest.sha256" "$bundle/"
    printf '%s\n' "${targets[@]}" >"$bundle/targets.txt"
    : >"$bundle/SHA256SUMS"
    for obj in "${targets[@]}"; do
      cmd="$(dirname "$obj")/.$(basename "$obj").cmd"
      if [[ ! -s "$out/$obj" || ! -s "$out/$cmd" ]]; then
        echo "DIST_IMAGE_MISSING_OBJECT=$obj cmd=$cmd" >&2
        exit 1
      fi
      riscv64-linux-gnu-readelf -h "$out/$obj" | grep -q 'Machine:.*RISC-V'
      ( cd "$out"; cp --parents "$obj" "$cmd" "$bundle/out" )
      ( cd "$bundle"; sha256sum "out/$obj" "out/$cmd" ) >>"$bundle/SHA256SUMS"
    done
    [[ $(wc -l <"$bundle/SHA256SUMS") -eq $((${#targets[@]} * 2)) ]] || exit 70
    echo "DIST_IMAGE_PRODUCE=PASS shard=$shard objects=${#targets[@]} seconds=$(($(date +%s)-start)) mini_calls=$(grep -c '^minic ' "$trace" || true)"
    ;;
  image|preflight)
    : >"$work/reuse.sha256"
    n=0
    for shard in 0 1 2 3 4 5 6; do
      bundle="$work/incoming/distributed-full-image-shard-$shard"
      test -d "$bundle/out" || { echo "DIST_IMAGE_MISSING_SHARD=$shard" >&2; exit 70; }
      cmp "$work/config.sha256" "$bundle/config.sha256"
      cmp "$work/compiler.sha256" "$bundle/compiler.sha256"
      cmp "$work/plan/manifest.sha256" "$bundle/manifest.sha256"
      mapfile -t targets < <(awk -F '\t' -v id="$shard" '$2 == id {print $3}' "$plan")
      diff -u <(printf '%s\n' "${targets[@]}") "$bundle/targets.txt"
      ( cd "$bundle"; sha256sum -c SHA256SUMS ) >"$work/shard-$shard-sha256-check.txt"
      [[ $(find "$bundle/out" -type f | wc -l) -eq $((${#targets[@]} * 2)) ]] || exit 70
      cp -a "$bundle/out/." "$out/"
      for obj in "${targets[@]}"; do
        cmd="$(dirname "$obj")/.$(basename "$obj").cmd"
        ( cd "$out"; sha256sum "$obj" "$cmd" ) >>"$work/reuse.sha256"
        n=$((n+1))
      done
    done
    [[ "$n" -eq "$total" ]] || exit 70
    ( cd "$out"; sha256sum --status -c "$work/reuse.sha256" )
    echo "DIST_IMAGE_RESTORED=PASS objects=$n checked_files=$((n*2))"
    # Independently prepared receiver generates headers *after* the producer
    # compiled its objects. Even with matching .cmd and bytes, GNU make treats
    # old restored objects as stale. Only after verifying both exact identities
    # and all 6704 content hashes may we update the object metadata. Do not
    # modify source, configuration, .cmd, or a single content byte.
    restamped=0
    mapfile -t restored_objects < <(cut -f3 "$plan")
    for obj in "${restored_objects[@]}"; do
      test -s "$out/$obj" || exit 70
      touch -- "$out/$obj"
      restamped=$((restamped+1))
    done
    [[ "$restamped" -eq "$total" ]] || exit 70
    ( cd "$out"; sha256sum --status -c "$work/reuse.sha256" )
    echo "DIST_IMAGE_RESTAMP=PASS objects=$restamped preserved_sha256=$((n*2))"
    if [[ "$mode" == preflight ]]; then
      # Cheap proof of reuse after fresh Kbuild header generation. Preserve the
      # full no-recompilation + SHA gates in the subsequent Image run.
      samples=(init/main.o arch/riscv/kernel/alternative.o kernel/bpf/bpf_lru_list.o fs/nfs/dir.o net/core/page_pool.o)
      : >"$work/preflight.trace"
      for sample in "${samples[@]}"; do
        MINIC="$compiler" REAL_CC=/usr/bin/riscv64-linux-gnu-gcc MINIC_KEEP_INTERMEDIATES=0 \
          MINIC_KBUILD_TRACE="$work/preflight.trace" CORE_FAST_TRACE=0 \
          make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
            CC="$wrapper" -j1 V=1 "$sample" >>"$work/preflight.log" 2>&1 || {
              tail -n 80 "$work/preflight.log" >&2; exit 1;
            }
      done
      ( cd "$out"; sha256sum --status -c "$work/reuse.sha256" ) || {
        echo "DIST_IMAGE_PREFLIGHT=FAIL hashes_changed" >&2; exit 1;
      }
      if grep -q "^minic input=" "$work/preflight.trace"; then
        echo "DIST_IMAGE_PREFLIGHT=FAIL MiniC_recompiled_sample" >&2
        grep "^minic input=" "$work/preflight.trace" | head -n 20 >&2
        exit 1
      fi
      echo "DIST_IMAGE_PREFLIGHT=PASS objects=$total samples=${#samples[@]} no_minic_recompile=1"
      exit 0
    fi
    trace="$work/image.trace"
    : >"$trace"
    start=$(date +%s)
    set +e
    MINIC="$compiler" REAL_CC=/usr/bin/riscv64-linux-gnu-gcc MINIC_KEEP_INTERMEDIATES=0 \
      MINIC_KBUILD_TRACE="$trace" CORE_FAST_TRACE=0 \
      make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
      CC="$wrapper" -j4 V=1 Image >"$work/image.log" 2>&1
    rc=$?
    set -e
    echo "DIST_IMAGE_KBUILD_RC=$rc elapsed_s=$(($(date +%s)-start))" | tee "$work/image-summary.txt"
    if [[ $rc -ne 0 ]]; then
      tail -n 120 "$work/image.log" >&2
      exit "$rc"
    fi
    sed -n -E 's/^pass source=.* output=([^ ]+).*$/\1/p' "$trace" >"$work/final-compiled.txt"
    awk -F '\t' '{print $3}' "$plan" >"$work/planned-targets.txt"
    rebuilt=$(grep -Fxf "$work/planned-targets.txt" "$work/final-compiled.txt" || true)
    if [[ "${MINIC_DISTRIBUTED_TRACED_REBUILD_LIMIT:-0}" =~ ^[1-9][0-9]*$ ]]; then
      # All 6704 producer hashes were checked before make. After linking,
      # allow changes only for exact manifest objects with proven MiniC passes.
      python3 "$root/tools/ci/linux-distributed-provenance-audit-v2.py" \
        "$out" "$work/reuse.sha256" "$trace" "$plan" \
        "$MINIC_DISTRIBUTED_TRACED_REBUILD_LIMIT" "$work/reuse-post-audit.txt" \
        | tee -a "$work/image-summary.txt"
      echo "DIST_IMAGE_REUSE=PASS mode=bounded-provenance initial_sha256=6704" \
        | tee -a "$work/image-summary.txt"
    else
      ( cd "$out"; sha256sum --status -c "$work/reuse.sha256" ) || {
        echo 'DIST_IMAGE_REUSE=FAIL object/cmd changed during final Kbuild' >&2
        exit 1
      }
      if [[ -n "$rebuilt" ]]; then
        echo "DIST_IMAGE_REUSE=FAIL transferred_target_recompiled=$rebuilt" >&2
        exit 1
      fi
      echo "DIST_IMAGE_REUSE=PASS target_recompiles=0 ancillary_minic_calls=$(grep -c '^minic ' "$trace" || true)" \
        | tee -a "$work/image-summary.txt"
    fi
    test -s "$out/arch/riscv/boot/Image"
    test -s "$out/vmlinux"
    test -s "$out/System.map"
    riscv64-linux-gnu-readelf -h "$out/vmlinux" | grep -q 'Machine:.*RISC-V'
    sha256sum "$out/arch/riscv/boot/Image" "$out/vmlinux" | tee -a "$work/image-summary.txt"
    echo "DIST_IMAGE_LINK=PASS bytes=$(stat -c %s "$out/arch/riscv/boot/Image")" | tee -a "$work/image-summary.txt"
    ;;
  qemu)
    test -s "$out/arch/riscv/boot/Image"
    mkdir -p "$work/initroot"/{dev,proc,sys,tmp}
    cat >"$work/init.c" <<'EOF_C'
#include <unistd.h>
#include <sys/reboot.h>
#include <linux/reboot.h>
int main(void) {
  static const char ok[] = "MINIC_LINUX_RUNTIME_PASS\n";
  (void)write(1,ok,sizeof(ok)-1);
  sync();
  (void)reboot(LINUX_REBOOT_CMD_POWER_OFF);
  for (;;) pause();
}
EOF_C
    riscv64-linux-gnu-gcc -static -Os -s "$work/init.c" -o "$work/initroot/init"
    ( cd "$work/initroot"; find . -print0 | cpio --null -ov --format=newc 2>/dev/null | gzip -9 >"$work/initramfs.cpio.gz" )
    start=$(date +%s)
    set +e
    QEMU_REAL_SYSTEM_RISCV64=qemu-system-riscv64 \
    QEMU_EXPLICIT_DTB_DIR="$work/runtime-dtb" \
      timeout 90s python3 tests/external/linux/qemu_explicit_initrd_wrapper.py \
        -machine virt -cpu max -m 512M -smp 1 -nographic -no-reboot -bios default \
        -kernel "$out/arch/riscv/boot/Image" -initrd "$work/initramfs.cpio.gz" \
        -append 'console=ttyS0 earlycon=sbi rdinit=/init panic=-1' \
        >"$work/qemu.log" 2>&1
    rc=$?
    set -e
    echo "DIST_IMAGE_QEMU_RC=$rc elapsed_s=$(($(date +%s)-start))" | tee "$work/qemu-summary.txt"
    tail -n 80 "$work/qemu.log"
    grep -aF 'MINIC_LINUX_RUNTIME_PASS' "$work/qemu.log" || exit 1
    if grep -aE 'Kernel panic|Oops:|BUG:|Unable to handle kernel|unhandled signal' "$work/qemu.log"; then
      echo 'DIST_IMAGE_QEMU=FAIL_FATAL' | tee -a "$work/qemu-summary.txt"
      exit 1
    fi
    echo 'DIST_IMAGE_QEMU=PASS' | tee -a "$work/qemu-summary.txt"
    ;;
  *)
    echo "invalid mode: $mode" >&2
    exit 64
    ;;
esac
