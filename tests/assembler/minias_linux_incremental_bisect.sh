#!/usr/bin/env bash
# Early Linux no-banner diagnostic: use the SAME Kbuild object tree, NOT
# another full GCC/MiniAS compilation. GNU as references are captured during
# the original assembler invocation (the source .s is GCC-temporary).
set -Eeuo pipefail
work=${MINIAS_LINUX_WORK:?}
src="$work/linux-6.6.143"
out="$work/out"
bindir="$work/as-override"
gold="$work/golden-as"
backup="$work/mini-originals"
mkdir -p "$backup" "$work/bisect"
cp "$out/arch/riscv/boot/Image" "$work/bisect/native-original-Image"
export MINIAS=${MINIAS:?}
export MINIAS_AS_TRACE=${MINIAS_AS_TRACE:?}
export REAL_AS=/usr/bin/riscv64-linux-gnu-as

# A kernel that reaches OpenSBI but not its own Linux banner is most likely
# stalled before printk/earlycon. Diagnose the entry/early-init owner cohort
# before attempting any expensive all-object bisect.
owners=(
  arch/riscv/kernel/head.o
  arch/riscv/kernel/entry.o
  arch/riscv/mm/init.o
  arch/riscv/kernel/setup.o
  arch/riscv/kernel/sbi.o
  drivers/of/fdt.o
  init/main.o
)
present=()
for obj in "${owners[@]}"; do
  if test -s "$out/$obj" && test -s "$gold/$obj"; then
    mkdir -p "$(dirname "$backup/$obj")"
    cp "$out/$obj" "$backup/$obj"
    present+=("$obj")
    sha256sum "$out/$obj" "$gold/$obj" >>"$work/bisect/initial-hashes.txt"
    if [[ "$obj" == arch/riscv/kernel/head.o ]]; then
      riscv64-linux-gnu-objdump -dr "$out/$obj" >"$work/bisect/mini-head-disassembly.txt"
      riscv64-linux-gnu-objdump -dr "$gold/$obj" >"$work/bisect/gnu-head-disassembly.txt"
      readelf -WS "$out/$obj" >"$work/bisect/mini-head-sections.txt"
      readelf -WS "$gold/$obj" >"$work/bisect/gnu-head-sections.txt"
      diff -u "$work/bisect/gnu-head-disassembly.txt" \
        "$work/bisect/mini-head-disassembly.txt" \
        >"$work/bisect/head-disassembly.diff" || true
      diff -u "$work/bisect/gnu-head-sections.txt" \
        "$work/bisect/mini-head-sections.txt" \
        >"$work/bisect/head-sections.diff" || true
    fi
  fi
done
printf 'MINIAS_BISECT_CAPTURE=PASS available=%s requested=%s no_full_recompile=1\n' \
  "${#present[@]}" "${#owners[@]}"
test "${#present[@]}" -ge 3

reset_native() {
  local obj
  for obj in "${present[@]}"; do
    cp "$backup/$obj" "$out/$obj"
    touch "$out/$obj"
  done
}
apply_gnu() {
  local obj
  for obj in "$@"; do
    test -s "$gold/$obj"
    cp "$gold/$obj" "$out/$obj"
    touch "$out/$obj"
  done
}
probe() {
  local label=$1
  shift
  local build_rc=0 run_rc=0
  reset_native
  apply_gnu "$@"
  printf 'MINIAS_BISECT_PROBE_BEGIN label=%s gnu_count=%s owners=%s\n' \
    "$label" "$#" "$*"
  # Same Kbuild command identity as production: .cmd remains stable and
  # only archives / link outputs downstream of touched object timestamps
  # should be regenerated.
  make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
    CC="riscv64-linux-gnu-gcc -B$bindir/" \
    AS="$bindir/riscv64-linux-gnu-as" -j4 Image \
    >"$work/bisect/$label.make.log" 2>&1 || build_rc=$?
  if test "$build_rc" -ne 0; then
    printf 'MINIAS_BISECT_PROBE=BUILD_FAIL label=%s rc=%s\n' "$label" "$build_rc"
    tail -n 45 "$work/bisect/$label.make.log" || true
    return 1
  fi
  # A working kernel prints the version/PID1 markers within seconds.
  # Profile=fast checks the original early Linux and user-space contract.
  LINUX_IMAGE="$out/arch/riscv/boot/Image" \
  INITRAMFS="$work/runtime-initramfs.cpio.gz" \
  BUILD_DIR="$work/bisect/$label-qemu" \
  LINUX_RELEASE=6.6.143 \
  LINUX_RUNTIME_PROFILE=fast \
  QEMU_SYSTEM_RISCV64=qemu-system-riscv64 \
  QEMU_TIMEOUT_SECONDS=25 \
    bash tests/external/linux/runtime_boot.sh \
    >"$work/bisect/$label.runtime.log" 2>&1 || run_rc=$?
  if test "$run_rc" -eq 0; then
    printf 'MINIAS_BISECT_PROBE=BOOT_PASS label=%s gnu_count=%s\n' "$label" "$#"
    return 0
  fi
  printf 'MINIAS_BISECT_PROBE=BOOT_FAIL label=%s gnu_count=%s qemu_rc=%s\n' \
    "$label" "$#" "$run_rc"
  return 1
}
head="arch/riscv/kernel/head.o"
head_ok=0
if test -s "$gold/$head" && probe head_gnu "$head"; then head_ok=1; fi
if test "$head_ok" -eq 1; then
  echo 'MINIAS_BISECT_EARLY=ISOLATED suspect=arch/riscv/kernel/head.o'
else
  # One cohort replacement, then an in-run binary split of THAT cohort.
  # No fresh 2,000-object recompilation, and only one QEMU per probe.
  if probe early_gnu_cohort "${present[@]}"; then
    echo 'MINIAS_BISECT_EARLY=COHORT_ISOLATED'
    lo=0
    hi=${#present[@]}
    while (( hi - lo > 1 )); do
      mid=$(( (lo + hi) / 2 ))
      half=("${present[@]:lo:mid-lo}")
      if probe "early_bisect_${lo}_${mid}" "${half[@]}"; then
        hi=$mid
      else
        lo=$mid
      fi
    done
    printf 'MINIAS_BISECT_EARLY_SUSPECT=%s\n' "${present[lo]}"
  else
    echo 'MINIAS_BISECT_EARLY=NOT_ISOLATED inspect_other_owners=1'
  fi
fi
reset_native
# Restore the exact original MiniAS Image for artifact identity.
cp "$work/bisect/native-original-Image" "$out/arch/riscv/boot/Image"
rm "$work/bisect/native-original-Image"
echo 'MINIAS_BISECT_DONE=PASS baseline_restored=1 exact_originals_preserved=1'
