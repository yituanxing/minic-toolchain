#!/usr/bin/env bash
# A bounded, exact-input cross-Runner Kbuild object transfer experiment.
# This file is only invoked by linux-distributed-kbuild-objects120-v1.yml.
set -Eeuo pipefail
mode=${1:?usage: $0 prepare|produce|verify [shard]}
shard=${2:-}
root=${GITHUB_WORKSPACE:?}
work="$root/build/linux-distributed-o120-v1"
src="$work/linux-6.6.143"
out="$work/out"
compiler="$work/toolchain/bin/minic"
wrapper="$root/tests/external/linux/stage2_kbuild_cc.sh"
targets_file="$root/tools/ci/linux-distributed-kbuild-objects120-targets.tsv"
mkdir -p "$work"
[[ $(wc -l < "$targets_file") -eq 120 ]] || { echo "not exactly 120 target entries" >&2; exit 3; }
[[ $(awk '{ print $2 }' "$targets_file" | sort -u | wc -l) -eq 120 ]] || { echo "duplicate target" >&2; exit 3; }

case "$mode" in
  prepare)
    archive="$work/linux-6.6.143.tar.xz"
    curl -fsSL --retry 5 --retry-delay 2 --retry-all-errors \
      https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.6.143.tar.xz -o "$archive"
    printf '%s  %s\n' dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932 "$archive" | sha256sum -c -
    tar -xJf "$archive" -C "$work"
    make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- defconfig
    make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- -j4 prepare
    sha256sum "$out/.config" | awk '{ print $1 }' > "$work/config.sha256"
    # Fail early if a manually selected target has no C source in pinned Linux.
    while IFS=
    sha256sum "$compiler" | awk '{ print $1 }' > "$work/compiler.sha256"
    echo "DIST_O120_PREPARE=PASS config=$(cat "$work/config.sha256") compiler=$(cat "$work/compiler.sha256")"
    ;;
  produce)
    [[ "$shard" =~ ^[0-5]$ ]] || exit 3
    mapfile -t targets < <(awk -v id="$shard" '$1 == id { print $2 }' "$targets_file")
    [[ ${#targets[@]} -eq 20 ]] || exit 3
    log="$work/produce-$shard.log"
    trace="$work/produce-$shard.trace"
    start=$(date +%s)
    MINIC="$compiler" REAL_CC=/usr/bin/riscv64-linux-gnu-gcc MINIC_KEEP_INTERMEDIATES=0 \
      MINIC_KBUILD_TRACE="$trace" CORE_FAST_TRACE=0 \
      make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
      CC="$wrapper" -j4 V=1 "${targets[@]}" >"$log" 2>&1 || { tail -n 160 "$log"; exit 1; }
    bundle="$work/bundle"
    mkdir -p "$bundle/out"
    cp "$work/config.sha256" "$work/compiler.sha256" "$bundle/"
    printf '%s\n' "${targets[@]}" >"$bundle/targets.txt"
    : >"$bundle/SHA256SUMS"
    for obj in "${targets[@]}"; do
      cmd="$(dirname "$obj")/.$(basename "$obj").cmd"
      test -s "$out/$obj" && test -s "$out/$cmd" || { echo "missing object/cmd $obj $cmd"; exit 1; }
      riscv64-linux-gnu-readelf -h "$out/$obj" | grep -q 'Machine:.*RISC-V'
      (cd "$out" && cp --parents "$obj" "$cmd" "$bundle/out")
      (cd "$bundle" && sha256sum "out/$obj" "out/$cmd") >>"$bundle/SHA256SUMS"
    done
    [[ $(wc -l < "$bundle/SHA256SUMS") -eq 40 ]] || exit 3
    echo "DIST_O120_PRODUCE=PASS shard=$shard objects=20 seconds=$(($(date +%s)-start)) minic_calls=$(grep -c '^minic ' "$trace" || true)"
    ;;
  verify)
    [[ -f "$work/config.sha256" && -f "$work/compiler.sha256" ]] || exit 3
    objects=()
    : >"$work/object-sha-before.txt"
    for id in 0 1 2 3 4 5; do
      bundle="$work/incoming/distributed-kbuild-objects120-shard-$id"
      test -d "$bundle/out" || { echo "missing artifact shard $id"; exit 1; }
      cmp "$work/config.sha256" "$bundle/config.sha256"
      cmp "$work/compiler.sha256" "$bundle/compiler.sha256"
      mapfile -t targets < <(awk -v sid="$id" '$1 == sid { print $2 }' "$targets_file")
      [[ ${#targets[@]} -eq 20 ]] || exit 3
      diff -u <(printf '%s\n' "${targets[@]}") "$bundle/targets.txt"
      (cd "$bundle" && sha256sum -c SHA256SUMS)
      [[ $(find "$bundle/out" -type f | wc -l) -eq 40 ]] || { echo "artifact missing or extra object/cmd" >&2; exit 1; }
      cp -a "$bundle/out/." "$out/"
      for obj in "${targets[@]}"; do
        cmd="$(dirname "$obj")/.$(basename "$obj").cmd"
        test -s "$out/$obj" && test -s "$out/$cmd"
        (cd "$out" && sha256sum "$obj" "$cmd") >>"$work/object-sha-before.txt"
        objects+=("$out/$obj")
      done
    done
    [[ ${#objects[@]} -eq 120 ]] || exit 3
    (cd "$out" && sha256sum --status -c "$work/object-sha-before.txt")
    trace="$work/reuse.trace"
    : >"$trace"
    start=$(date +%s)
    mapfile -t all_targets < <(awk '{ print $2 }' "$targets_file")
    # Keep CC and every Kbuild argument identical to each producer.
    MINIC="$compiler" REAL_CC=/usr/bin/riscv64-linux-gnu-gcc MINIC_KEEP_INTERMEDIATES=0 \
      MINIC_KBUILD_TRACE="$trace" CORE_FAST_TRACE=0 \
      make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
      CC="$wrapper" -j4 V=1 "${all_targets[@]}" >"$work/reuse.log" 2>&1 || {
        tail -n 160 "$work/reuse.log"; exit 1;
      }
    (cd "$out" && sha256sum --status -c "$work/object-sha-before.txt") || {
      echo 'DIST_O120_REUSE=FAIL object/cmd changed' >&2; exit 1;
    }
    # A fresh Kbuild prepare can regenerate *unrelated* support objects
    # (e.g. scripts/mod/empty.o and native vDSO). The invariant is that none
    # of the 120 transferred requested objects was rebuilt, even to identical bytes.
    count=$(grep -c '^minic ' "$trace" || true)
    sed -n -E 's/^pass source=.* output=([^ ]+).*$/\1/p' "$trace" >"$work/compiled-targets.txt"
    awk '{ print $2 }' "$targets_file" >"$work/requested-targets.txt"
    unexpected=$(grep -Fxf "$work/requested-targets.txt" "$work/compiled-targets.txt" || true)
    if [[ -n "$unexpected" ]]; then
      printf 'DIST_O120_REUSE=FAIL transferred_target_recompiled=%s\n' "$unexpected" >&2
      exit 1
    fi
    echo "DIST_O120_REUSE=PASS objects=120 target_recompiles=0 ancillary_minic_calls=$count seconds=$(($(date +%s)-start))"
    printf 'DIST_O120_ANCILLARY_COMPILED=%s\n' "$(paste -sd, "$work/compiled-targets.txt")"
    # This is a *relocatable partial link*, NOT a full Image/runtime test.
    riscv64-linux-gnu-ld -r -o "$work/distributed120.o" "${objects[@]}"
    riscv64-linux-gnu-readelf -h "$work/distributed120.o" | grep -q 'Machine:.*RISC-V'
    echo "DIST_O120_PARTIAL_LINK=PASS objects=120 bytes=$(stat -c %s "$work/distributed120.o")"
    ;;
  *)
    echo "unknown mode: $mode" >&2
    exit 64
    ;;
esac
\t' read -r _sid obj; do
      test -f "$src/${obj%.o}.c" || { echo "DIST_O120_MISSING_C_SOURCE=$obj" >&2; exit 1; }
    done <"$targets_file"
    bash "$root/tools/ci/linux-runtime-build-minic-profile-v1.sh" "$work/toolchain" "$work/profile"
    sha256sum "$compiler" | awk '{ print $1 }' > "$work/compiler.sha256"
    echo "DIST_O120_PREPARE=PASS config=$(cat "$work/config.sha256") compiler=$(cat "$work/compiler.sha256")"
    ;;
  produce)
    [[ "$shard" =~ ^[0-5]$ ]] || exit 3
    mapfile -t targets < <(awk -v id="$shard" '$1 == id { print $2 }' "$targets_file")
    [[ ${#targets[@]} -eq 20 ]] || exit 3
    log="$work/produce-$shard.log"
    trace="$work/produce-$shard.trace"
    start=$(date +%s)
    MINIC="$compiler" REAL_CC=/usr/bin/riscv64-linux-gnu-gcc MINIC_KEEP_INTERMEDIATES=0 \
      MINIC_KBUILD_TRACE="$trace" CORE_FAST_TRACE=0 \
      make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
      CC="$wrapper" -j4 V=1 "${targets[@]}" >"$log" 2>&1 || { tail -n 160 "$log"; exit 1; }
    bundle="$work/bundle"
    mkdir -p "$bundle/out"
    cp "$work/config.sha256" "$work/compiler.sha256" "$bundle/"
    printf '%s\n' "${targets[@]}" >"$bundle/targets.txt"
    : >"$bundle/SHA256SUMS"
    for obj in "${targets[@]}"; do
      cmd="$(dirname "$obj")/.$(basename "$obj").cmd"
      test -s "$out/$obj" && test -s "$out/$cmd" || { echo "missing object/cmd $obj $cmd"; exit 1; }
      riscv64-linux-gnu-readelf -h "$out/$obj" | grep -q 'Machine:.*RISC-V'
      (cd "$out" && cp --parents "$obj" "$cmd" "$bundle/out")
      (cd "$bundle" && sha256sum "out/$obj" "out/$cmd") >>"$bundle/SHA256SUMS"
    done
    [[ $(wc -l < "$bundle/SHA256SUMS") -eq 40 ]] || exit 3
    echo "DIST_O120_PRODUCE=PASS shard=$shard objects=20 seconds=$(($(date +%s)-start)) minic_calls=$(grep -c '^minic ' "$trace" || true)"
    ;;
  verify)
    [[ -f "$work/config.sha256" && -f "$work/compiler.sha256" ]] || exit 3
    objects=()
    : >"$work/object-sha-before.txt"
    for id in 0 1 2 3 4 5; do
      bundle="$work/incoming/distributed-kbuild-objects120-shard-$id"
      test -d "$bundle/out" || { echo "missing artifact shard $id"; exit 1; }
      cmp "$work/config.sha256" "$bundle/config.sha256"
      cmp "$work/compiler.sha256" "$bundle/compiler.sha256"
      mapfile -t targets < <(awk -v sid="$id" '$1 == sid { print $2 }' "$targets_file")
      [[ ${#targets[@]} -eq 20 ]] || exit 3
      diff -u <(printf '%s\n' "${targets[@]}") "$bundle/targets.txt"
      (cd "$bundle" && sha256sum -c SHA256SUMS)
      [[ $(find "$bundle/out" -type f | wc -l) -eq 40 ]] || { echo "artifact missing or extra object/cmd" >&2; exit 1; }
      cp -a "$bundle/out/." "$out/"
      for obj in "${targets[@]}"; do
        cmd="$(dirname "$obj")/.$(basename "$obj").cmd"
        test -s "$out/$obj" && test -s "$out/$cmd"
        (cd "$out" && sha256sum "$obj" "$cmd") >>"$work/object-sha-before.txt"
        objects+=("$out/$obj")
      done
    done
    [[ ${#objects[@]} -eq 120 ]] || exit 3
    (cd "$out" && sha256sum --status -c "$work/object-sha-before.txt")
    trace="$work/reuse.trace"
    : >"$trace"
    start=$(date +%s)
    mapfile -t all_targets < <(awk '{ print $2 }' "$targets_file")
    # Keep CC and every Kbuild argument identical to each producer.
    MINIC="$compiler" REAL_CC=/usr/bin/riscv64-linux-gnu-gcc MINIC_KEEP_INTERMEDIATES=0 \
      MINIC_KBUILD_TRACE="$trace" CORE_FAST_TRACE=0 \
      make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
      CC="$wrapper" -j4 V=1 "${all_targets[@]}" >"$work/reuse.log" 2>&1 || {
        tail -n 160 "$work/reuse.log"; exit 1;
      }
    (cd "$out" && sha256sum --status -c "$work/object-sha-before.txt") || {
      echo 'DIST_O120_REUSE=FAIL object/cmd changed' >&2; exit 1;
    }
    # A fresh Kbuild prepare can regenerate *unrelated* support objects
    # (e.g. scripts/mod/empty.o and native vDSO). The invariant is that none
    # of the 120 transferred requested objects was rebuilt, even to identical bytes.
    count=$(grep -c '^minic ' "$trace" || true)
    sed -n -E 's/^pass source=.* output=([^ ]+).*$/\1/p' "$trace" >"$work/compiled-targets.txt"
    awk '{ print $2 }' "$targets_file" >"$work/requested-targets.txt"
    unexpected=$(grep -Fxf "$work/requested-targets.txt" "$work/compiled-targets.txt" || true)
    if [[ -n "$unexpected" ]]; then
      printf 'DIST_O120_REUSE=FAIL transferred_target_recompiled=%s\n' "$unexpected" >&2
      exit 1
    fi
    echo "DIST_O120_REUSE=PASS objects=120 target_recompiles=0 ancillary_minic_calls=$count seconds=$(($(date +%s)-start))"
    printf 'DIST_O120_ANCILLARY_COMPILED=%s\n' "$(paste -sd, "$work/compiled-targets.txt")"
    # This is a *relocatable partial link*, NOT a full Image/runtime test.
    riscv64-linux-gnu-ld -r -o "$work/distributed120.o" "${objects[@]}"
    riscv64-linux-gnu-readelf -h "$work/distributed120.o" | grep -q 'Machine:.*RISC-V'
    echo "DIST_O120_PARTIAL_LINK=PASS objects=120 bytes=$(stat -c %s "$work/distributed120.o")"
    ;;
  *)
    echo "unknown mode: $mode" >&2
    exit 64
    ;;
esac
