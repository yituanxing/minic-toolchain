#!/usr/bin/env bash
# Linux 6.6.143: GNU GCC -> MiniAS only -> GNU binutils -> LoongArch QEMU.
# The acceptance gate is an actually linked and booted kernel, with P0/P1
# userspace markers. Object-file byte equality is intentionally not a gate.
set -Eeuo pipefail

repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)
work=${WORK_DIR:-"$repo/build/minias-linux-fourarch/loongarch64"}
minias=${MINIAS:-"$repo/build/release/bin/minic-as"}
version=${LINUX_VERSION:-6.6.143}
cc=${LOONG_CC:-loongarch64-linux-gnu-gcc-13}
qemu=${LOONG_QEMU:-qemu-system-loongarch64}
gnu_as=${LOONG_GNU_AS:-/usr/bin/loongarch64-linux-gnu-as}

if [[ $version != 6.6.143 ]]; then
  echo "MINIAS_LINUX_FOURARCH=ERROR unsupported_linux_version=$version" >&2
  exit 2
fi
case "$work" in
  "$repo/build/"*|/tmp/*) ;;
  *) echo "MINIAS_LINUX_FOURARCH=ERROR unsafe_workdir=$work" >&2; exit 2 ;;
esac
for tool in "$cc" "$qemu" "$gnu_as" "$minias" curl tar sha256sum git make cpio gzip timeout; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "MINIAS_LINUX_FOURARCH=ERROR arch=loongarch64 missing_tool=$tool" >&2
    exit 127
  fi
done

mkdir -p "$work/evidence" "$work/as-override"
kernel_tar="$work/linux-$version.tar.xz"
kernel_src="$work/linux-$version"
kernel_out="$work/out"
firmware="$work/QEMU_EFI.fd"
initramfs="$work/runtime-initramfs.cpio.gz"
trace="$work/evidence/as.trace"
kbuild_log="$work/evidence/kbuild.log"

# Linux and Loongson firmware are both pinned to immutable upstream bytes.
# Ubuntu 24.04 has qemu-system-loongarch64 but NO qemu-efi-loongarch64
# package, so the Loongson-maintained EFI firmware must be supplied here.
if [[ ! -s "$kernel_tar" ]]; then
  curl -fsSL --retry 3 \
    "https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-$version.tar.xz" \
    -o "$kernel_tar"
fi
printf '%s  %s\n' \
  dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932 \
  "$kernel_tar" | sha256sum -c -
if [[ ! -s "$firmware" ]]; then
  curl -fsSL --retry 3 \
    https://raw.githubusercontent.com/loongson/Firmware/1c55f43743b947e4c327716afc818ac5c65558ce/LoongArchVirtMachine/QEMU_EFI.fd \
    -o "$firmware"
fi
firmware_blob=$(git hash-object "$firmware")
if [[ $firmware_blob != eac998fb49b1d977296269e0b0955084fba9dc4a ]]; then
  echo "MINIAS_LINUX_FOURARCH=ERROR arch=loongarch64 firmware_sha=$firmware_blob" >&2
  exit 1
fi
printf 'MINIAS_LINUX_FOURARCH_FIRMWARE=PASS arch=loongarch64 git_blob=%s\n' "$firmware_blob"

if [[ ! -d "$kernel_src" ]]; then
  tar -xJf "$kernel_tar" -C "$work"
fi

# No MiniAS output is used for this initramfs. Its three static userspace
# binaries are independently linked by GNU GCC/CRT; kernel-produced assembly
# is the ONLY substituted pipeline component.
RISCV_CC="$cc" \
  BUILD_DIR="$work/initramfs-build" \
  OUTPUT_INITRAMFS="$initramfs" \
  bash "$repo/tests/external/linux/build_runtime_initramfs.sh"

# GCC -B also locates a plain `as`; Kbuild's AS= may use the prefixed binary.
# Both must resolve to exactly the same fail-closed wrapper. In particular,
# a failed assembler invocation NEVER tries GNU as on production input.
cat >"$work/as-override/loongarch64-linux-gnu-as" <<'MINIAS_WRAPPER'
#!/usr/bin/env bash
set -Eeuo pipefail
args=("$@")
out= input= temporary_input= query=0
# Linux 6.6's arch/loongarch/Makefile forces `-msoft-float -mabi=lp64s`
# regardless of the lp64d GNU userspace ABI chosen for the initramfs.
flags=(--target=loongarch64 -march=loongarch64 -mabi=lp64s)
for ((i=0; i<${#args[@]}; ++i)); do
  arg=${args[i]}
  case "$arg" in
    --version|-v|--help) query=1 ;;
    -o) if ((i+1 < ${#args[@]})); then out=${args[i+1]}; ((i+=1)); fi ;;
    -mabi=*|-march=*) flags+=("$arg") ;;
    -*) ;;
    *) [[ -f "$arg" ]] && input=$arg ;;
  esac
done
if ((query)); then
  printf 'query %q\n' "$*" >>"${MINIAS_AS_TRACE:?}"
  exec "${REAL_AS:?}" "$@"
fi
# The LoongArch 6.6 kernel Makefile enables GCC's `-pipe`, so the real GNU
# assembler may receive its assembly stream on stdin without a positional
# `.s` pathname. Retain exactly those bytes in a temporary file for MiniAS;
# do NOT fall back to GNU as for any such production invocation.
if [[ -z "$input" && -n "$out" ]]; then
  temporary_input=$(mktemp "${TMPDIR:-/tmp}/minias-loong-kbuild.XXXXXX.s")
  cat >"$temporary_input"
  input=$temporary_input
fi
if [[ -z "$input" || -z "$out" ]]; then
  printf 'MINIAS_AS_WRAPPER_ERROR arch=loongarch64 missing_io argc=%s\n' "$#" >&2
  exit 2
fi
printf 'assemble input=%q output=%q\n' "$input" "$out" >>"${MINIAS_AS_TRACE:?}"
if "${MINIAS:?}" "${flags[@]}" -o "$out" "$input"; then
  printf 'pass input=%q output=%q\n' "$input" "$out" >>"$MINIAS_AS_TRACE"
  if [[ -n "$temporary_input" ]]; then rm -f "$temporary_input"; fi
else
  status=$?
  printf 'fail status=%s input=%q output=%q\n' "$status" "$input" "$out" >>"$MINIAS_AS_TRACE"
  if [[ -f "$input" ]]; then
    mkdir -p "${MINIAS_FAILURE_DIR:?}"
    cp "$input" "$MINIAS_FAILURE_DIR/$(basename "$out").s"
  fi
  if [[ -n "$temporary_input" ]]; then rm -f "$temporary_input"; fi
  exit "$status"
fi
MINIAS_WRAPPER
chmod +x "$work/as-override/loongarch64-linux-gnu-as"
ln -sfn loongarch64-linux-gnu-as "$work/as-override/as"
: >"$trace"

make -C "$kernel_src" O="$kernel_out" ARCH=loongarch \
  CROSS_COMPILE=loongarch64-linux-gnu- CC="$cc" loongson3_defconfig
"$kernel_src/scripts/config" --file "$kernel_out/.config" \
  -e EFI -e EFI_STUB -e EFI_ZBOOT \
  -e BLK_DEV_INITRD -e DEVTMPFS -e DEVTMPFS_MOUNT \
  -e SERIAL_8250 -e SERIAL_8250_CONSOLE -e TMPFS -e PROC_FS -e SYSFS \
  -e NET -e INET -e IPV6 -e UNIX -e IO_URING -e EVENTFD -e EPOLL \
  -e SIGNALFD -e INOTIFY_USER -e TIMERFD -e MEMFD_CREATE -e FUTEX \
  -e PIDFD
make -C "$kernel_src" O="$kernel_out" ARCH=loongarch \
  CROSS_COMPILE=loongarch64-linux-gnu- CC="$cc" olddefconfig
grep -qx 'CONFIG_EFI_STUB=y' "$kernel_out/.config"
grep -qx 'CONFIG_EFI_ZBOOT=y' "$kernel_out/.config"

export MINIAS="$minias" MINIAS_AS_TRACE="$trace" REAL_AS="$gnu_as"
export MINIAS_FAILURE_DIR="$work/evidence/failure-inputs"
started=$(date +%s)
if ! make -C "$kernel_src" O="$kernel_out" ARCH=loongarch \
    CROSS_COMPILE=loongarch64-linux-gnu- \
    CC="$cc -B$work/as-override/" \
    AS="$work/as-override/loongarch64-linux-gnu-as" \
    KCFLAGS=-Wno-error \
    -j4 vmlinuz.efi >"$kbuild_log" 2>&1; then
  echo 'MINIAS_LINUX_FOURARCH_BUILD=FAIL arch=loongarch64 stage=make-vmlinuz.efi'
  echo 'MINIAS_LINUX_KBUILD_DIAGNOSTIC=first-errors'
  grep -nE 'minic-as:|(^| )error:|fatal error:|undefined reference|undefined symbol|Error [0-9]+|No rule to make target|unsupported-|bad-' \
    "$kbuild_log" | head -n 120 || true
  echo 'MINIAS_LINUX_KBUILD_DIAGNOSTIC=tail'
  tail -n 140 "$kbuild_log" || true
  echo 'MINIAS_LINUX_AS_TRACE_DIAGNOSTIC=last-failures'
  grep '^fail ' "$trace" | tail -n 50 || true
  exit 1
fi

image="$kernel_out/arch/loongarch/boot/vmlinuz.efi"
test -s "$image"
sha256sum "$image" >"$work/evidence/vmlinuz.efi.sha256"
passed=$(grep -c '^pass ' "$trace" || true)
assembled=$(grep -c '^assemble ' "$trace" || true)
test "$passed" -gt 1000
printf 'MINIAS_LINUX_FOURARCH_BUILD=PASS arch=loongarch64 assembled=%s passed=%s elapsed_s=%s image=%s head=%s\n' \
  "$assembled" "$passed" "$(( $(date +%s) - started ))" "$image" "${GITHUB_SHA:-local}"

# Boot the very same .efi executable (no objcopy/mini-linker substitutions).
# This is the QEMU LoongArch virt/EDK2 boot path recommended by QEMU docs.
boot() {
  local lane=$1 append=$2 interactive=$3
  local log="$work/evidence/$lane.log" status
  local command=("$qemu" -machine virt -cpu la464 -m 2G -smp 1
    -bios "$firmware" -kernel "$image" -initrd "$initramfs"
    -append "$append" -display none -serial stdio -monitor none -no-reboot)
  set +e
  if [[ $interactive == yes ]]; then
    (
      sleep "${LOONG_SHELL_INPUT_DELAY_SECONDS:-25}"
      printf '%s\n' 'mount -t proc proc /proc' 'echo PROC_CMDLINE_BEGIN' \
        'cat /proc/cmdline' 'echo PROC_CMDLINE_END' 'echo RDINIT_SH_OK' \
        'echo DONE_RDINIT' 'sync' 'poweroff -f'
      sleep 2
    ) | timeout --signal=TERM "${LOONG_QEMU_TIMEOUT_SECONDS:-180}" \
      "${command[@]}" >"$log" 2>&1
    status=$?
  else
    timeout --signal=TERM "${LOONG_QEMU_TIMEOUT_SECONDS:-180}" \
      "${command[@]}" </dev/null >"$log" 2>&1
    status=$?
  fi
  set -e
  case "$status" in
    0|124|143) ;;
    *) echo "MINIAS_LINUX_FOURARCH_BOOT=FAIL arch=loongarch64 lane=$lane qemu_status=$status" >&2;
       tail -n 150 "$log" >&2; exit "$status" ;;
  esac
  printf 'MINIAS_LINUX_FOURARCH_BOOT arch=loongarch64 lane=%s qemu_status=%s log=%s\n' \
    "$lane" "$status" "$log"
}

require() {
  local lane=$1 pat=$2
  if ! grep -E "$pat" "$work/evidence/$lane.log" >/dev/null; then
    echo "MINIAS_LINUX_FOURARCH_RUNTIME=FAIL arch=loongarch64 lane=$lane missing=$pat" >&2
    tail -n 200 "$work/evidence/$lane.log" >&2
    exit 1
  fi
}

common='console=ttyS0 earlycon loglevel=8 panic=-1'
boot rdinit-init-p0 "$common rdinit=/init" no
for marker in "Linux version $version" 'Kernel command line:.*rdinit=/init' \
  'devtmpfs: initialized' \
  '(Trying to unpack rootfs image as initramfs|Unpacking initramfs)' \
  '(Run /init as init process|Starting init: /init)' \
  'INIT_PROC_MOUNT=PASS' 'INIT_SYSFS_MOUNT=PASS' 'INIT_DEVTMPFS_MOUNT=PASS' \
  'INIT_CMDLINE_READ=PASS' 'INIT_TMPFS_MOUNT=PASS' 'INIT_TMPFS_RW=PASS' \
  'MINIC_LINUX_RUNTIME_INIT_END fail=0' 'USER_SHELL_OK' 'DONE_COMMANDS'; do
  require rdinit-init-p0 "$marker"
done
boot rdinit-sh-p0 "$common rdinit=/bin/sh" yes
for marker in "Linux version $version" 'Kernel command line:.*rdinit=/bin/sh' \
  '(Run /bin/sh as init process|Starting init: /bin/sh)' \
  'MINIC_LINUX_RUNTIME_SH_READY' 'PROC_CMDLINE_BEGIN' \
  'console=ttyS0.*rdinit=/bin/sh' \
  'PROC_CMDLINE_END' 'RDINIT_SH_OK' 'DONE_RDINIT'; do
  require rdinit-sh-p0 "$marker"
done
echo 'MINIAS_LINUX_FOURARCH_P0=PASS arch=loongarch64 lanes=2/2'

boot rdinit-init-p1 "$common rdinit=/init minic_runtime=p1" no
for marker in 'Kernel command line:.*minic_runtime=p1' \
  'RUNTIME_V2_SYSCALL_BEGIN' 'RUNTIME_V2_SYSCALL_END pass=12 fail=0' \
  'INIT_RUNTIME_PROBE=PASS'; do
  require rdinit-init-p1 "$marker"
done
python3 "$repo/tests/external/linux/runtime_v2_log_check.py" \
  --profile p1 --log "$work/evidence/rdinit-init-p1.log"
echo 'MINIAS_LINUX_FOURARCH_RUNTIME=PASS arch=loongarch64 p0=2/2 p1=12/12'
