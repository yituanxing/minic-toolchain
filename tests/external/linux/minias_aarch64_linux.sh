#!/usr/bin/env bash
# Linux 6.6.143 AArch64: GCC emits every .s, MiniAS alone assembles production
# objects, GNU Kbuild/binutils link Image, and QEMU boots both P0 and P1.
set -Eeuo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$root"

version=${LINUX_VERSION:-6.6.143}
if [[ $version != 6.6.143 ]]; then
  echo "MINIAS_LINUX_FOURARCH_ERROR unsupported_version=$version" >&2
  exit 2
fi
work=${WORK_DIR:-"$root/build/minias-linux-aarch64"}
if [[ -z $work || $work == / || $work == "$root" ]]; then
  echo 'MINIAS_LINUX_FOURARCH_ERROR unsafe_work_directory' >&2
  exit 2
fi
mkdir -p "$work"
work=$(cd "$work" && pwd)
src="$work/linux-$version"
out="$work/out"
image="$out/arch/arm64/boot/Image"
initramfs="$work/runtime-initramfs.cpio.gz"
trace="$work/minias-as.trace"
bindir="$work/as-override"
gnu_objects="$work/gnu-as-objects"
minias=${MINIAS:-"$root/build/release/bin/minic-as"}
cc=${AARCH64_CC:-aarch64-linux-gnu-gcc}
qemu=${AARCH64_QEMU:-qemu-system-aarch64}
as_backend=${AARCH64_AS_BACKEND:-minias}
if [[ $as_backend != minias && $as_backend != gnu ]]; then
  echo "MINIAS_LINUX_FOURARCH_ERROR invalid_as_backend=$as_backend" >&2
  exit 2
fi

for bin in "$cc" aarch64-linux-gnu-as aarch64-linux-gnu-ld "$qemu" curl sha256sum cpio timeout python3; do
  if ! command -v "$bin" >/dev/null 2>&1; then
    echo "MINIAS_LINUX_FOURARCH_ERROR missing_tool=$bin" >&2
    exit 2
  fi
done
if [[ $as_backend == minias && ! -x $minias ]]; then
  echo "MINIAS_LINUX_FOURARCH_ERROR missing_minias=$minias" >&2
  exit 2
fi

archive="$work/linux-$version.tar.xz"
if [[ ! -f $archive ]]; then
  curl -fL --retry 3 --retry-delay 2 \
    "https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-$version.tar.xz" -o "$archive"
fi
printf '%s  %s\n' \
  dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932 \
  "$archive" | sha256sum -c -
if [[ ! -d $src ]]; then tar -xJf "$archive" -C "$work"; fi

# The initramfs contains real AArch64 GNU-compiled static PID1, shell and
# syscall-probe executables. It is deliberately identical between both runs.
RISCV_CC="$cc" BUILD_DIR="$work/initramfs" OUTPUT_INITRAMFS="$initramfs" \
  bash tests/external/linux/build_runtime_initramfs.sh

mkdir -p "$bindir"
cat >"$bindir/aarch64-linux-gnu-as" <<'AS_WRAPPER'
#!/usr/bin/env bash
set -Eeuo pipefail
args=("$@")
options=(--target=aarch64)
out=
input=
query=0
version_probe=0
for ((i=0; i<${#args[@]}; ++i)); do
  arg=${args[i]}
  case "$arg" in
    --version) query=1; version_probe=1 ;;
    -v|--help) query=1 ;;
    -march=*|-mabi=*) options+=("$arg") ;;
    -o) if (( i + 1 < ${#args[@]} )); then out=${args[i+1]}; ((++i)); fi ;;
    -*) ;;
    *) if [[ -f $arg ]]; then input=$arg; fi ;;
  esac
done

# Both paths intercept this *exact* real GCC-generated entry.o assembly input.
# The GNU lane delegates unchanged bytes/flags to actual GNU GAS, whereas the
# MiniAS lane hands the same input to MiniAS. This is a targeted source
# provenance oracle, not a claim about all remaining Kbuild objects.
capture_entry_source() {
  if [[ $out != arch/arm64/kernel/entry.o && $out != */arch/arm64/kernel/entry.o ]]; then
    return 0
  fi
  if [[ ! -f $input ]]; then
    printf 'MINIAS_AARCH64_INPUT_PROVENANCE=FAIL entry.o missing_input=%q\n' "$input" >&2
    exit 2
  fi
  mkdir -p "${MINIAS_ENTRY_INPUT_DIR:?}"
  cp -- "$input" "$MINIAS_ENTRY_INPUT_DIR/entry.o.s"
  sha256sum "$MINIAS_ENTRY_INPUT_DIR/entry.o.s" | cut -d' ' -f1 \
    >"$MINIAS_ENTRY_INPUT_DIR/entry.o.s.sha256"
  printf 'MINIAS_AARCH64_INPUT_PROVENANCE=CAPTURED backend=%s output=%q sha256=%s\n' \
    "${MINIAS_AS_BACKEND:?}" "$out" "$(<"$MINIAS_ENTRY_INPUT_DIR/entry.o.s.sha256")" \
    >>"${MINIAS_AS_TRACE:?}"
}

# This transparent GNU control wrapper must never execute MiniAS. We give
# GCC the same -B search-path shape as the MiniAS lane so capability probes
# encounter a real GNU assembler, and capture entry.o without changing the
# content of the assembler's argv or its original input file.
if [[ ${MINIAS_AS_BACKEND:?} == gnu ]]; then
  capture_entry_source
  exec "${REAL_AS:?}" "$@"
fi
if ((query)); then
  if [[ -n $out || -n $input ]]; then
    # GCC's Kconfig AS capability probe passes `--version` alongside an actual
    # temporary assembly: -o .tmp_<pid>/tmp /tmp/cc<random>.s.  It is not a
    # Kbuild object.  Only this exact probe shape may use GNU as; production
    # .o files (and all other assembler I/O) remain MiniAS-only and fail closed.
    if ((version_probe)) &&
       { { [[ $out == /dev/null ]] && [[ -z $input || $input =~ ^/tmp/cc[[:alnum:]]+\.s$ ]]; } ||
         { [[ $out =~ (^|/)\.tmp_[0-9]+/tmp$ ]] && [[ $input =~ ^/tmp/cc[[:alnum:]]+\.s$ ]]; }; }; then
      printf 'delegate-kconfig-probe cwd=%q input=%q output=%q\n' "$PWD" "$input" "$out" >>"$MINIAS_AS_TRACE"
      exec "${REAL_AS:-/usr/bin/aarch64-linux-gnu-as}" "$@"
    fi
    printf 'MINIAS_AS_WRAPPER_ERROR query_with_production_io output=%q input=%q\n' "$out" "$input" >&2
    exit 2
  fi
  printf 'delegate-query' >>"$MINIAS_AS_TRACE"
  printf ' %q' "$@" >>"$MINIAS_AS_TRACE"
  printf '\n' >>"$MINIAS_AS_TRACE"
  exec "${REAL_AS:-/usr/bin/aarch64-linux-gnu-as}" "$@"
fi
if [[ -z $out || -z $input ]]; then
  echo "MINIAS_AS_WRAPPER_ERROR missing_io output=$out input=$input" >&2
  exit 2
fi
capture_entry_source
printf 'assemble cwd=%q input=%q output=%q\n' "$PWD" "$input" "$out" >>"$MINIAS_AS_TRACE"
if "$MINIAS" "${options[@]}" -o "$out" "$input"; then
  # Kconfig can intentionally assemble to /dev/null; those invocations are
  # still MiniAS-owned and need a terminal pass in the provenance trace.
  if [[ $out != /dev/null && ! -s $out ]]; then
    printf 'MINIAS_AS_WRAPPER_ERROR empty_output=%q\n' "$out" >&2
    printf 'fail cwd=%q input=%q output=%q reason=empty-output\n' "$PWD" "$input" "$out" >>"$MINIAS_AS_TRACE"
    exit 1
  fi
  printf 'pass cwd=%q input=%q output=%q\n' "$PWD" "$input" "$out" >>"$MINIAS_AS_TRACE"
  # A sidecar oracle captures the very same GCC-generated assembly while it
  # still exists in /tmp.  It is never linked into the production Image.
  capture_gnu_sidecar() {
    absolute_out=$(realpath -m -- "$out")
    if [[ $absolute_out == "$MINIAS_KBUILD_OUT/"* ]]; then
      relative_out=${absolute_out#"$MINIAS_KBUILD_OUT/"}
      reference="$MINIAS_GNU_OBJECT_DIR/$relative_out"
      mkdir -p "$(dirname "$reference")" || return 1
      reference_args=()
      for ((j=0; j<${#args[@]}; ++j)); do
        if [[ ${args[j]} == -o && $((j+1)) -lt ${#args[@]} ]]; then
          reference_args+=(-o "$reference")
          ((++j))
        else
          reference_args+=("${args[j]}")
        fi
      done
      if "$REAL_AS" "${reference_args[@]}"; then
        printf 'gnu-reference input=%q output=%q\n' "$input" "$relative_out" >>"$MINIAS_AS_TRACE"
      else
        printf 'gnu-reference-fail input=%q output=%q\n' "$input" "$relative_out" >>"$MINIAS_AS_TRACE"
        return 1
      fi
      if [[ $relative_out == arch/arm64/kernel/* ||
            $relative_out == arch/arm64/mm/* ||
            $relative_out == mm/sparse-vmemmap.o ]]; then
        preserved="$MINIAS_GNU_SOURCE_DIR/${relative_out%.o}.s"
        mkdir -p "$(dirname "$preserved")" || return 1
        cp -- "$input" "$preserved" || return 1
      fi
    fi
  }
  if [[ -n ${MINIAS_GNU_OBJECT_DIR:-} && $out == *.o ]] && ! capture_gnu_sidecar; then
    printf 'gnu-reference-diagnostic-fail input=%q output=%q\n' "$input" "$out" >>"$MINIAS_AS_TRACE" || true
  fi
else
  status=$?
  printf 'fail status=%s cwd=%q input=%q output=%q\n' "$status" "$PWD" "$input" "$out" >>"$MINIAS_AS_TRACE"
  if [[ -f $input && -n ${MINIAS_FAILURE_DIR:-} ]]; then
    mkdir -p "$MINIAS_FAILURE_DIR"
    cp "$input" "$MINIAS_FAILURE_DIR/$(basename "$out").s"
  fi
  exit "$status"
fi
AS_WRAPPER
chmod +x "$bindir/aarch64-linux-gnu-as"
ln -sfn aarch64-linux-gnu-as "$bindir/as"

export MINIAS="$minias"
export MINIAS_AS_TRACE="$trace"
export MINIAS_FAILURE_DIR="$work/failure-inputs"
export REAL_AS=/usr/bin/aarch64-linux-gnu-as
export MINIAS_KBUILD_OUT="$out"
export MINIAS_GNU_OBJECT_DIR="$gnu_objects"
export MINIAS_GNU_SOURCE_DIR="$work/asm-sources"
export MINIAS_AS_BACKEND="$as_backend"
export MINIAS_ENTRY_INPUT_DIR="$work/evidence"
mkdir -p "$work/evidence"
: >"$trace"
# Use the same GCC -B wrapper and assembler for Kconfig probes and actual
# Kbuild. Only the assembler engine changes between MiniAS and GNU controls.
build_cc="$cc -B$bindir/"
build_as="$bindir/aarch64-linux-gnu-as"

make -C "$src" O="$out" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
  CC="$build_cc" AS="$build_as" defconfig
"$src/scripts/config" --file "$out/.config" \
  -e BLK_DEV_INITRD -e DEVTMPFS -e DEVTMPFS_MOUNT \
  -e SERIAL_AMBA_PL011 -e SERIAL_AMBA_PL011_CONSOLE \
  -e PROC_FS -e SYSFS -e TMPFS -e IPV6 -e IO_URING
make -C "$src" O="$out" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
  CC="$build_cc" AS="$build_as" olddefconfig

# Keep the resolved pre-build config and independently verify the final
# Kbuild config matches it and the same-commit GNU control's final config.
config_sha256=$(sha256sum "$out/.config")
cp -- "$out/.config" "$work/kernel-config.pre.config"
printf '%s\n' "${config_sha256%% *}" >"$work/kernel-config.pre.sha256"
printf 'MINIAS_LINUX_KCONFIG_PRE_SHA256 arch=aarch64 backend=%s sha256=%s\n' \
  "$as_backend" "${config_sha256%% *}"

capture_final_kconfig() {
  if [[ ! -s $out/.config ]]; then
    echo 'MINIAS_LINUX_FOURARCH_ERROR arch=aarch64 missing_final_kconfig' >&2
    return 1
  fi
  cp -- "$out/.config" "$work/kernel-config.final.config"
  local final_hash diff_status
  final_hash=$(sha256sum "$out/.config")
  printf '%s\n' "${final_hash%% *}" >"$work/kernel-config.sha256"
  printf 'MINIAS_LINUX_KCONFIG_FINAL_SHA256 arch=aarch64 backend=%s sha256=%s\n' \
    "$as_backend" "${final_hash%% *}"
  if diff -u "$work/kernel-config.pre.config" "$work/kernel-config.final.config" \
      >"$work/evidence/kernel-config.diff.log"; then
    echo 'MINIAS_LINUX_KCONFIG_STABLE=PASS arch=aarch64'
  else
    diff_status=$?
    if [[ $diff_status -ne 1 ]]; then
      echo "MINIAS_LINUX_FOURARCH_ERROR arch=aarch64 kconfig_diff_status=$diff_status" >&2
      return 1
    fi
    echo 'MINIAS_LINUX_KCONFIG_STABLE=FAIL arch=aarch64 changed_during_kbuild' >&2
    grep -E '^[+-](CONFIG_[A-Z0-9_]+=|# CONFIG_[A-Z0-9_]+ is not set)' \
      "$work/evidence/kernel-config.diff.log" | head -n 100 >&2 || true
    return 1
  fi
}
start=$(date +%s)
printf 'MINIAS_AARCH64_STAGE=KBUILD_START epoch=%s\n' "$start"
# Keep long GNU Kbuild stages observable without changing the production
# assembler, build exit status, or acceptance markers. Its normal output stays
# in kbuild.log, while the append-only AS trace provides live counts.
progress_monitor_pid=
stop_progress_monitor() {
  if [[ -n ${progress_monitor_pid:-} ]]; then
    kill "$progress_monitor_pid" 2>/dev/null || true
    wait "$progress_monitor_pid" 2>/dev/null || true
    progress_monitor_pid=
  fi
}
trap stop_progress_monitor EXIT
(
  while sleep 60; do
    assembled_now=$(grep -c '^assemble ' "$trace" || true)
    passed_now=$(grep -c '^pass ' "$trace" || true)
    failed_now=$(grep -c '^fail ' "$trace" || true)
    printf 'MINIAS_AARCH64_STAGE=KBUILD_PROGRESS elapsed_s=%s assembled=%s passed=%s failed=%s\n' \
      "$(( $(date +%s) - start ))" "$assembled_now" "$passed_now" "$failed_now"
  done
) &
progress_monitor_pid=$!
if ! make -C "$src" O="$out" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
  CC="$build_cc" AS="$build_as" \
  -j4 -k Image >"$work/kbuild.log" 2>&1; then
  stop_progress_monitor
  echo 'MINIAS_LINUX_FOURARCH_BUILD=FAIL arch=aarch64 stage=make-Image'
  echo 'MINIAS_LINUX_KBUILD_DIAGNOSTIC=first-errors'
  grep -nE 'minic-as:|(^| )error:|fatal error:|undefined reference|undefined symbol|Error [0-9]+|No rule to make target|unsupported-|bad-' \
    "$work/kbuild.log" | head -n 100 || true
  echo 'MINIAS_LINUX_KBUILD_DIAGNOSTIC=tail'
  tail -n 90 "$work/kbuild.log" || true
  echo 'MINIAS_LINUX_AS_TRACE_DIAGNOSTIC=last-failures'
  grep '^fail ' "$trace" | tail -n 40 || true
  capture_final_kconfig || true
  exit 1
fi
stop_progress_monitor
printf 'MINIAS_AARCH64_STAGE=KBUILD_END elapsed_s=%s\n' \
  "$(( $(date +%s) - start ))"
capture_final_kconfig
test -s "$image"
sha256sum "$image" >"$work/Image.sha256"
file "$image"
if [[ $as_backend == minias ]]; then
passed=$(grep -c '^pass ' "$trace" || true)
assembled=$(grep -c '^assemble ' "$trace" || true)
failed=$(grep -c '^fail ' "$trace" || true)
test "$passed" -gt 1000
python3 tests/external/linux/minias_as_provenance_check.py \
  --arch aarch64 --kbuild-out "$out" --trace "$trace" \
  --compiler "$cc" --assembler aarch64-linux-gnu-as --min-objects 1000
printf 'MINIAS_LINUX_FOURARCH_BUILD=PASS arch=aarch64 assembled=%s passed=%s probe_failures=%s elapsed_s=%s\n' \
  "$assembled" "$passed" "$failed" "$(( $(date +%s) - start ))"
reference_count=$(find "$gnu_objects" -type f -name '*.o' 2>/dev/null | wc -l) || reference_count=0
printf 'MINIAS_AARCH64_GNU_SIDECAR=CAPTURED objects=%s production_link=MiniAS_only\n' "$reference_count"
else
  printf 'MINIAS_LINUX_FOURARCH_GNU_BASELINE=BUILD_PASS arch=aarch64 elapsed_s=%s image=%s\n' \
    "$(( $(date +%s) - start ))" "$image"
fi
# The 4,000+ object strict ELF diff is diagnostic, but missing production
# comparisons and oracle errors are fatal. Never run it before first QEMU.
# Preserve targeted boot suspects immediately; run the full oracle after P1,
# or opt in to it on a failed boot for a deeper offline investigation.
run_full_object_oracle() {
  if [[ $as_backend != minias ]]; then return 0; fi
  local oracle_start
  oracle_start=$(date +%s)
  printf 'MINIAS_AARCH64_STAGE=ORACLE_START objects=%s\n' "$reference_count"
  if ! python3 tests/external/linux/minias_aarch64_object_oracle.py \
    --mini-dir "$out" --gnu-dir "$gnu_objects" --out "$work/aarch64-object-oracle.tsv" \
    >"$work/aarch64-object-oracle.log" 2>&1; then
    tail -n 60 "$work/aarch64-object-oracle.log" >&2 || true
    echo 'MINIAS_AARCH64_GNU_SIDECAR=FAIL production_oracle_incomplete' >&2
    return 1
  fi
  printf 'MINIAS_AARCH64_STAGE=ORACLE_END elapsed_s=%s\n' \
    "$(( $(date +%s) - oracle_start ))"
  tail -n 45 "$work/aarch64-object-oracle.log" || true
}
# Successive *real QEMU* object bisections isolated proc.o and later
# sparse-vmemmap.o. Retain both GNU/Mini instruction and relocation evidence
# without uploading the 4,000+ GNU sidecar objects.
for suspect in arch/arm64/mm/proc mm/sparse-vmemmap; do
  if [[ $as_backend != minias ]]; then break; fi
  for producer in minias gnu; do
    if [[ $producer == minias ]]; then
      object="$out/$suspect.o"
    else
      object="$gnu_objects/$suspect.o"
    fi
    if [[ -f $object ]]; then
      aarch64-linux-gnu-objdump -dr "$object" \
        >"$work/aarch64-${suspect##*/}-$producer-objdump.log" 2>&1 || true
    fi
  done
done

qemu_timeout=${QEMU_TIMEOUT_SECONDS:-120}
boot() {
  local name=$1 append=$2 interactive=$3 log="$work/qemu-$1.log" status
  printf 'MINIAS_AARCH64_STAGE=QEMU_START lane=%s timeout_s=%s\n' \
    "$name" "$qemu_timeout"
  if [[ $interactive == 1 ]]; then
    (
      sleep "${QEMU_SHELL_INPUT_DELAY_SECONDS:-8}"
      printf '%s\n' \
        'mount -t proc proc /proc' \
        'echo PROC_CMDLINE_BEGIN' \
        'cat /proc/cmdline' \
        'echo PROC_CMDLINE_END' \
        'echo RDINIT_SH_OK' \
        'echo DONE_RDINIT' \
        'sync' \
        'poweroff -f'
    ) | timeout --signal=TERM "$qemu_timeout" "$qemu" \
      -M virt -cpu cortex-a57 -m 1G -smp 1 \
      -nographic -no-reboot -nic none -kernel "$image" -initrd "$initramfs" \
      -append "$append" >"$log" 2>&1 || status=$?
  else
    timeout --signal=TERM "$qemu_timeout" "$qemu" \
      -M virt -cpu cortex-a57 -m 1G -smp 1 \
      -nographic -no-reboot -nic none -kernel "$image" -initrd "$initramfs" \
      -append "$append" </dev/null >"$log" 2>&1 || status=$?
  fi
  status=${status:-0}
  case "$status" in 0|124|143) ;; *) tail -n 150 "$log" >&2; return "$status" ;; esac
  printf 'MINIAS_LINUX_FOURARCH_BOOT arch=aarch64 lane=%s status=%s log=%s\n' "$name" "$status" "$log"
}
require() {
  local lane=$1 pattern=$2
  if ! grep -E "$pattern" "$work/qemu-$lane.log" >/dev/null; then
    printf 'MINIAS_LINUX_FOURARCH_RUNTIME=FAIL arch=aarch64 lane=%s missing=%s\n' "$lane" "$pattern" >&2
    tail -n 160 "$work/qemu-$lane.log" >&2
    if [[ ${MINIAS_AARCH64_FULL_OBJECT_ORACLE_ON_FAILURE:-0} == 1 ]]; then
      run_full_object_oracle
    fi
    # Repeated full-object QEMU bisection is an opt-in diagnosis, not a
    # default side effect of every failed exact-head Linux acceptance run.
    if [[ $lane == p0-init && ${MINIAS_AARCH64_AUTO_BISECT:-0} == 1 ]]; then
      printf 'MINIAS_AARCH64_STAGE=BISECT_START lane=%s marker=%s\n' \
        "$lane" "$pattern"
      WORK_DIR="$work" MINIAS_KBUILD_OUT="$out" MINIAS_GNU_OBJECT_DIR="$gnu_objects" \
        MINIAS_AARCH64_BISECT_MARKER="$pattern" \
        bash tests/external/linux/minias_aarch64_object_bisect.sh || true
    fi
    exit 1
  fi
}

common='console=ttyAMA0 earlycon=pl011,0x9000000 loglevel=8 panic=-1'
boot p0-init "$common rdinit=/init" 0
for marker in "Linux version $version" 'Kernel command line:.*rdinit=/init' \
  '(Trying to unpack rootfs image as initramfs|Unpacking initramfs)' \
  'devtmpfs: initialized' \
  '(Run /init as init process|Starting init: /init)' \
  'INIT_PROC_MOUNT=PASS' 'INIT_SYSFS_MOUNT=PASS' \
  'INIT_DEVTMPFS_MOUNT=PASS' 'INIT_CMDLINE_READ=PASS' \
  'INIT_TMPFS_MOUNT=PASS' 'INIT_TMPFS_RW=PASS' \
  'MINIC_LINUX_RUNTIME_INIT_END fail=0' 'USER_SHELL_OK' 'DONE_COMMANDS'; do
  require p0-init "$marker"
done

boot p0-sh "$common rdinit=/bin/sh" 1
for marker in "Linux version $version" \
  'Kernel command line:.*rdinit=/bin/sh' \
  '(Run /bin/sh as init process|Starting init: /bin/sh)' \
  'MINIC_LINUX_RUNTIME_SH_READY' 'PROC_CMDLINE_BEGIN' \
  'console=ttyAMA0.*rdinit=/bin/sh' 'PROC_CMDLINE_END' \
  'MINISH_PROC_CMDLINE_READ=PASS' 'RDINIT_SH_OK' 'DONE_RDINIT'; do
  require p0-sh "$marker"
done

boot p1-init "$common rdinit=/init minic_runtime=p1" 0
for marker in "Linux version $version" \
  'Kernel command line:.*rdinit=/init.*minic_runtime=p1' \
  'RUNTIME_V2_SYSCALL_BEGIN' \
  'RUNTIME_V2_SYSCALL_END pass=12 fail=0' 'INIT_RUNTIME_PROBE=PASS' \
  'MINIC_LINUX_RUNTIME_INIT_END fail=0'; do
  require p1-init "$marker"
done
python3 tests/external/linux/runtime_v2_log_check.py \
  --profile p1 --log "$work/qemu-p1-init.log"
if [[ $as_backend == gnu ]]; then
  echo 'MINIAS_LINUX_FOURARCH_GNU_BASELINE=PASS arch=aarch64 p0=2/2 p1=12/12'
else
  echo 'MINIAS_LINUX_FOURARCH_RUNTIME=PASS arch=aarch64 p0=2/2 p1=12/12'
  run_full_object_oracle
fi
