#!/usr/bin/env bash
set -euo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
qemu=${QEMU_SYSTEM_RISCV64:-qemu-system-riscv64}
image=${LINUX_RUNTIME_IMAGE:-}
initrd=${LINUX_RUNTIME_INITRD:-}
profile=${1:-initramfs-init}
timeout_s=${LINUX_RUNTIME_TIMEOUT:-180}
memory=${LINUX_RUNTIME_MEMORY:-512M}
cpus=${LINUX_RUNTIME_CPUS:-1}
work=${LINUX_RUNTIME_WORK:-"$root/build/linux-runtime"}
log="$work/qemu-$profile.log"

if [ -z "$image" ] || [ -z "$initrd" ]; then
    echo "usage: set LINUX_RUNTIME_IMAGE and LINUX_RUNTIME_INITRD" >&2
    exit 2
fi
if [ ! -f "$image" ] || [ ! -f "$initrd" ]; then
    echo "missing Image or initrd" >&2
    exit 2
fi
command -v "$qemu" >/dev/null 2>&1
command -v timeout >/dev/null 2>&1
mkdir -p "$work"

case "$profile" in
    initramfs-init)
        append="console=ttyS0 earlycon=sbi loglevel=8 ignore_loglevel panic=0 rdinit=/init"
        guest_input='echo USER_SHELL_OK
echo DONE_COMMANDS
'
        ;;
    rdinit-shell)
        append="console=ttyS0 earlycon=sbi loglevel=8 ignore_loglevel panic=0 rdinit=/bin/sh"
        guest_input='mkdir -p /proc
mount -t proc proc /proc || true
cat /proc/cmdline
echo RDINIT_SH_OK
echo DONE_RDINIT
'
        ;;
    poweroff)
        append="console=ttyS0 earlycon=sbi loglevel=8 ignore_loglevel panic=0 rdinit=/init"
        guest_input=''
        ;;
    *)
        echo "unknown profile: $profile" >&2
        exit 2
        ;;
esac

set +e
printf '%s' "$guest_input" | timeout "$timeout_s" "$qemu" \
    -M virt -cpu max -m "$memory" -smp "$cpus" \
    -nographic -no-reboot -bios default \
    -kernel "$image" \
    -initrd "$initrd" \
    -append "$append" \
    -monitor none \
    >"$log" 2>&1
qemu_rc=$?
set -e

args="--profile $profile --qemu-rc $qemu_rc"
case "$profile" in
    initramfs-init|rdinit-shell)
        args="$args --allow-timeout-after-endpoint"
        ;;
    poweroff)
        args="$args --require-powerdown"
        ;;
esac

# shellcheck disable=SC2086
python3 "$root/tools/ci/check-linux-runtime-log.py" "$log" $args

echo "LINUX_RUNTIME_QEMU profile=$profile qemu_rc=$qemu_rc log=$log"
