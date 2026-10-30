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

for bin in "$cc" aarch64-linux-gnu-as aarch64-linux-gnu-ld "$qemu" curl sha256sum cpio timeout python3; do
  if ! command -v "$bin" >/dev/null 2>&1; then
    echo "MINIAS_LINUX_FOURARCH_ERROR missing_tool=$bin" >&2
    exit 2
  fi
done
if [[ ! -x $minias ]]; then
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
for ((i=0; i<${#args[@]}; ++i)); do
  arg=${args[i]}
  case "$arg" in
    --version|-v|--help) query=1 ;;
    -march=*|-mabi=*) options+=("$arg") ;;
    -o) if (( i + 1 < ${#args[@]} )); then out=${args[i+1]}; ((++i)); fi ;;
    -*) ;;
    *) if [[ -f $arg ]]; then input=$arg; fi ;;
  esac
done
if ((query)); then
  printf 'delegate-query' >>"$MINIAS_AS_TRACE"
  printf ' %q' "$@" >>"$MINIAS_AS_TRACE"
  printf '\n' >>"$MINIAS_AS_TRACE"
  exec "${REAL_AS:-/usr/bin/aarch64-linux-gnu-as}" "$@"
fi
if [[ -z $out || -z $input ]]; then
  echo "MINIAS_AS_WRAPPER_ERROR missing_io output=$out input=$input" >&2
  exit 2
fi
printf 'assemble input=%q output=%q\n' "$input" "$out" >>"$MINIAS_AS_TRACE"
if "$MINIAS" "${options[@]}" -o "$out" "$input"; then
  printf 'pass input=%q output=%q\n' "$input" "$out" >>"$MINIAS_AS_TRACE"
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
            $relative_out == arch/arm64/mm/* ]]; then
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
  printf 'fail status=%s input=%q output=%q\n' "$status" "$input" "$out" >>"$MINIAS_AS_TRACE"
  if [[ -f $input && -n ${MINIAS_FAILURE_DIR:-} ]]; then
    mkdir -p "$MINIAS_FAILURE_DIR"
    cp "$input" "$MINIAS_FAILURE_DIR/$(basename "$out").s"
  fi
  exit "$status"
fi
AS_WRAPPER
chmod +x "$bindir/aarch64-linux-gnu-as"
ln -sfn aarch64-linux-gnu-as "$bindir/as"

make -C "$src" O="$out" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig
"$src/scripts/config" --file "$out/.config" \
  -e BLK_DEV_INITRD -e DEVTMPFS -e DEVTMPFS_MOUNT \
  -e SERIAL_AMBA_PL011 -e SERIAL_AMBA_PL011_CONSOLE \
  -e PROC_FS -e SYSFS -e TMPFS -e IPV6 -e IO_URING
make -C "$src" O="$out" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig

export MINIAS="$minias"
export MINIAS_AS_TRACE="$trace"
export MINIAS_FAILURE_DIR="$work/failure-inputs"
export REAL_AS=/usr/bin/aarch64-linux-gnu-as
export MINIAS_KBUILD_OUT="$out"
export MINIAS_GNU_OBJECT_DIR="$gnu_objects"
export MINIAS_GNU_SOURCE_DIR="$work/asm-sources"
: >"$trace"
start=$(date +%s)
if ! make -C "$src" O="$out" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
  CC="$cc -B$bindir/" AS="$bindir/aarch64-linux-gnu-as" \
  -j4 Image >"$work/kbuild.log" 2>&1; then
  echo 'MINIAS_LINUX_FOURARCH_BUILD=FAIL arch=aarch64 stage=make-Image'
  echo 'MINIAS_LINUX_KBUILD_DIAGNOSTIC=first-errors'
  grep -nE 'minic-as:|(^| )error:|fatal error:|undefined reference|undefined symbol|Error [0-9]+|No rule to make target|unsupported-|bad-' \
    "$work/kbuild.log" | head -n 100 || true
  echo 'MINIAS_LINUX_KBUILD_DIAGNOSTIC=tail'
  tail -n 90 "$work/kbuild.log" || true
  echo 'MINIAS_LINUX_AS_TRACE_DIAGNOSTIC=last-failures'
  grep '^fail ' "$trace" | tail -n 40 || true
  exit 1
fi
test -s "$image"
sha256sum "$image" >"$work/Image.sha256"
file "$image"
passed=$(grep -c '^pass ' "$trace" || true)
assembled=$(grep -c '^assemble ' "$trace" || true)
failed=$(grep -c '^fail ' "$trace" || true)
test "$passed" -gt 1000
printf 'MINIAS_LINUX_FOURARCH_BUILD=PASS arch=aarch64 assembled=%s passed=%s probe_failures=%s elapsed_s=%s\n' \
  "$assembled" "$passed" "$failed" "$(( $(date +%s) - start ))"
reference_count=$(find "$gnu_objects" -type f -name '*.o' 2>/dev/null | wc -l) || reference_count=0
printf 'MINIAS_AARCH64_GNU_SIDECAR=CAPTURED objects=%s production_link=MiniAS_only\n' "$reference_count"
if ! python3 tests/external/linux/minias_aarch64_object_oracle.py \
  --mini-dir "$out" --gnu-dir "$gnu_objects" --out "$work/aarch64-object-oracle.tsv" \
  >"$work/aarch64-object-oracle.log" 2>&1; then
  echo 'MINIAS_AARCH64_GNU_SIDECAR=DIAGNOSTIC_INCOMPLETE'
fi
tail -n 45 "$work/aarch64-object-oracle.log" || true
# proc.o is the first object isolated by real QEMU boot bisection.  Keep
# instruction + relocation evidence even on runners where the ELF files
# themselves are expensive to retrieve for an immediate next repair batch.
for producer in minias gnu; do
  if [[ $producer == minias ]]; then
    object="$out/arch/arm64/mm/proc.o"
  else
    object="$gnu_objects/arch/arm64/mm/proc.o"
  fi
  if [[ -f $object ]]; then
    aarch64-linux-gnu-objdump -dr "$object" >"$work/aarch64-proc-$producer-objdump.log" 2>&1 || true
  fi
done

qemu_timeout=${QEMU_TIMEOUT_SECONDS:-120}
boot() {
  local name=$1 append=$2 interactive=$3 log="$work/qemu-$1.log" status
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
    if [[ $lane == p0-init && ${MINIAS_AARCH64_AUTO_BISECT:-1} == 1 ]]; then
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
  'RDINIT_SH_OK' 'DONE_RDINIT'; do
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
echo 'MINIAS_LINUX_FOURARCH_RUNTIME=PASS arch=aarch64 p0=2/2 p1=12/12'
