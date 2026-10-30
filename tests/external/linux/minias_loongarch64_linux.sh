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
replay_only=${LOONG_QEMU_REPLAY_ONLY:-0}
as_backend=${LOONG_AS_BACKEND:-minias}

if [[ $version != 6.6.143 ]]; then
  echo "MINIAS_LINUX_FOURARCH=ERROR unsupported_linux_version=$version" >&2
  exit 2
fi
case "$work" in
  "$repo/build/"*|/tmp/*) ;;
  *) echo "MINIAS_LINUX_FOURARCH=ERROR unsafe_workdir=$work" >&2; exit 2 ;;
esac
if [[ $replay_only != 0 && $replay_only != 1 ]]; then
  echo "MINIAS_LINUX_FOURARCH=ERROR invalid_replay_only=$replay_only" >&2
  exit 2
fi
if [[ $as_backend != minias && $as_backend != gnu ]]; then
  echo "MINIAS_LINUX_FOURARCH=ERROR invalid_as_backend=$as_backend" >&2
  exit 2
fi
tools=("$qemu" curl sha256sum git timeout python3)
if [[ $replay_only == 0 ]]; then
  tools+=("$cc" "$gnu_as" tar make cpio gzip)
  if [[ $as_backend == minias ]]; then tools+=("$minias"); fi
fi
for tool in "${tools[@]}"; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "MINIAS_LINUX_FOURARCH=ERROR arch=loongarch64 missing_tool=$tool" >&2
    exit 127
  fi
done

mkdir -p "$work/evidence"
kernel_tar="$work/linux-$version.tar.xz"
kernel_src="$work/linux-$version"
kernel_out="$work/out"
image="$kernel_out/arch/loongarch/boot/vmlinuz.efi"
firmware="$work/QEMU_EFI.fd"
initramfs="$work/runtime-initramfs.cpio.gz"
trace="$work/evidence/as.trace"
kbuild_log="$work/evidence/kbuild.log"

# Loongson firmware is pinned to immutable upstream bytes. QEMU-only replay
# uses the same firmware validation as a fresh Kbuild run.
# Ubuntu 24.04 has qemu-system-loongarch64 but NO qemu-efi-loongarch64
# package, so the Loongson-maintained EFI firmware must be supplied here.
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

if [[ $replay_only == 0 ]]; then
if [[ ! -s "$kernel_tar" ]]; then
  curl -fsSL --retry 3 \
    "https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-$version.tar.xz" \
    -o "$kernel_tar"
fi
printf '%s  %s\n' \
  dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932 \
  "$kernel_tar" | sha256sum -c -
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
build_cc="$cc"
build_as="$gnu_as"
if [[ $as_backend == minias ]]; then
mkdir -p "$work/as-override"
cat >"$work/as-override/loongarch64-linux-gnu-as" <<'MINIAS_WRAPPER'
#!/usr/bin/env bash
set -Eeuo pipefail
args=("$@")
out= input= temporary_input= query=0 version_probe=0
# Linux 6.6's arch/loongarch/Makefile forces `-msoft-float -mabi=lp64s`
# regardless of the lp64d GNU userspace ABI chosen for the initramfs.
flags=(--target=loongarch64 -march=loongarch64 -mabi=lp64s)
for ((i=0; i<${#args[@]}; ++i)); do
  arg=${args[i]}
  case "$arg" in
    --version) query=1; version_probe=1 ;;
    -v|--help) query=1 ;;
    -o) if ((i+1 < ${#args[@]})); then out=${args[i+1]}; ((i+=1)); fi ;;
    -mabi=*|-march=*) flags+=("$arg") ;;
    -*) ;;
    *) [[ -f "$arg" ]] && input=$arg ;;
  esac
done
if ((query)); then
  if [[ -n $out || -n $input ]]; then
    # Kconfig scripts/as-version.sh uses GCC -Wa,--version against a
    # temporary /tmp/cc*.s with non-object output.  Delegate only that exact
    # disposable probe to GNU.  No production .o or arbitrary I/O may bypass
    # MiniAS, even when GCC supplies a version-query flag.
    if ((version_probe)) &&
       { { [[ $out == /dev/null ]] && [[ -z $input || $input =~ ^/tmp/cc[[:alnum:]]+\.s$ ]]; } ||
         { [[ $out =~ (^|/)\.tmp_[0-9]+/tmp$ ]] && [[ $input =~ ^/tmp/cc[[:alnum:]]+\.s$ ]]; }; }; then
      printf 'delegate-kconfig-probe cwd=%q input=%q output=%q\n' "$PWD" "$input" "$out" >>"${MINIAS_AS_TRACE:?}"
      exec "${REAL_AS:?}" "$@"
    fi
    printf 'MINIAS_AS_WRAPPER_ERROR arch=loongarch64 query_with_production_io output=%q input=%q\n' "$out" "$input" >&2
    exit 2
  fi
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
printf 'assemble cwd=%q input=%q output=%q\n' "$PWD" "$input" "$out" >>"${MINIAS_AS_TRACE:?}"
if "${MINIAS:?}" "${flags[@]}" -o "$out" "$input"; then
  # Discard-only feature probes are real MiniAS invocations but cannot leave
  # a nonempty file. Keep the nonempty guarantee on every production object.
  if [[ $out != /dev/null && ! -s $out ]]; then
    echo "MINIAS_AS_WRAPPER_ERROR arch=loongarch64 empty_output=$out" >&2
    printf 'fail cwd=%q input=%q output=%q reason=empty-output\n' "$PWD" "$input" "$out" >>"$MINIAS_AS_TRACE"
    exit 1
  fi
  printf 'pass cwd=%q input=%q output=%q\n' "$PWD" "$input" "$out" >>"$MINIAS_AS_TRACE"
  if [[ -n "$temporary_input" ]]; then rm -f "$temporary_input"; fi
else
  status=$?
  printf 'fail status=%s cwd=%q input=%q output=%q\n' "$status" "$PWD" "$input" "$out" >>"$MINIAS_AS_TRACE"
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
export MINIAS="$minias" MINIAS_AS_TRACE="$trace" REAL_AS="$gnu_as"
export MINIAS_FAILURE_DIR="$work/evidence/failure-inputs"
build_cc="$cc -B$work/as-override/"
build_as="$work/as-override/loongarch64-linux-gnu-as"
fi

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

started=$(date +%s)
if ! make -C "$kernel_src" O="$kernel_out" ARCH=loongarch \
    CROSS_COMPILE=loongarch64-linux-gnu- \
    CC="$build_cc" \
    AS="$build_as" \
    KCFLAGS=-Wno-error \
    -j4 -k vmlinuz.efi >"$kbuild_log" 2>&1; then
  echo 'MINIAS_LINUX_FOURARCH_BUILD=FAIL arch=loongarch64 stage=make-vmlinuz.efi'
  echo 'MINIAS_LINUX_KBUILD_DIAGNOSTIC=first-errors'
  grep -nE 'minic-as:|(^| )error:|fatal error:|undefined reference|undefined symbol|Error [0-9]+|No rule to make target|unsupported-|bad-' \
    "$kbuild_log" | head -n 120 || true
  echo 'MINIAS_LINUX_KBUILD_DIAGNOSTIC=tail'
  tail -n 140 "$kbuild_log" || true
  echo 'MINIAS_LINUX_AS_TRACE_DIAGNOSTIC=last-failures'
  if [[ $as_backend == minias ]]; then grep '^fail ' "$trace" | tail -n 50 || true; fi
  exit 1
fi

test -s "$image"
sha256sum "$image" >"$work/evidence/vmlinuz.efi.sha256"
if [[ $as_backend == minias ]]; then
  passed=$(grep -c '^pass ' "$trace" || true)
  assembled=$(grep -c '^assemble ' "$trace" || true)
  test "$passed" -gt 1000
  python3 "$repo/tests/external/linux/minias_as_provenance_check.py" \
    --arch loongarch64 --kbuild-out "$kernel_out" --trace "$trace" \
    --compiler "$cc" --assembler loongarch64-linux-gnu-as --min-objects 1000
  printf 'MINIAS_LINUX_FOURARCH_BUILD=PASS arch=loongarch64 as_backend=minias assembled=%s passed=%s elapsed_s=%s image=%s head=%s\n' \
    "$assembled" "$passed" "$(( $(date +%s) - started ))" "$image" "${GITHUB_SHA:-local}"
else
  printf 'MINIAS_LINUX_FOURARCH_BUILD=PASS arch=loongarch64 as_backend=gnu elapsed_s=%s image=%s head=%s\n' \
    "$(( $(date +%s) - started ))" "$image" "${GITHUB_SHA:-local}"
fi
else
  # actions/download-artifact restores the exact linked vmlinuz.efi and the
  # GNU-built initramfs; there is no compiler, assembler or linker in replay.
  test -s "$image" && test -s "$initramfs"
  if [[ -s $work/evidence/vmlinuz.efi.sha256 ]]; then
    read -r expected_sha _ <"$work/evidence/vmlinuz.efi.sha256"
    read -r actual_sha _ < <(sha256sum "$image")
    if [[ $expected_sha != "$actual_sha" ]]; then
      echo "MINIAS_LINUX_FOURARCH_REPLAY=ERROR arch=loongarch64 image_digest_mismatch" >&2
      exit 1
    fi
  fi
  printf 'MINIAS_LINUX_FOURARCH_REPLAY=PASS arch=loongarch64 origin=%s image=%s initramfs=%s\n' \
    "${LOONG_QEMU_REPLAY_ORIGIN:-minias}" "$image" "$initramfs"
fi

# Boot the very same .efi executable (no objcopy/mini-linker substitutions).
# This is the QEMU LoongArch virt/EDK2 boot path recommended by QEMU docs.
boot() {
  local lane=$1 append=$2 interactive=$3
  local log="$work/evidence/$lane.log" status
  # QEMU's implicit virtio NIC asks for efi-virtio.rom, absent on Ubuntu's
  # loongarch64 package. Replace that default NIC with an explicit equivalent
  # whose PCI option ROM is disabled; preserve the virtio NIC itself.
  local command=("$qemu" -machine virt -cpu la464 -m 2G -smp 1
    -nic none -netdev user,id=loongnet
    -device virtio-net-pci,netdev=loongnet,romfile=
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
