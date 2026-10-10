#!/bin/sh
set -eu

export LC_ALL=C
export LANG=C

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
qemu=${QEMU_SYSTEM_RISCV64:-qemu-system-riscv64}
image=${LINUX_IMAGE:-}
initramfs=${INITRAMFS:-}
work=${BUILD_DIR:-"$root/build/linux-runtime"}
timeout_seconds=${QEMU_TIMEOUT_SECONDS:-90}
shell_input_delay=${QEMU_SHELL_INPUT_DELAY_SECONDS:-8}
expected_release=${LINUX_RELEASE:-6.6.143}
profile=${LINUX_RUNTIME_PROFILE:-full}

if test -z "$image" || test ! -s "$image"; then
    printf '%s\n' "LINUX_RUNTIME_ERROR missing LINUX_IMAGE=$image" >&2
    exit 2
fi
if test -z "$initramfs" || test ! -s "$initramfs"; then
    printf '%s\n' "LINUX_RUNTIME_ERROR missing INITRAMFS=$initramfs" >&2
    exit 2
fi
if ! command -v "$qemu" >/dev/null 2>&1; then
    printf '%s\n' "LINUX_RUNTIME_ERROR missing qemu=$qemu" >&2
    exit 2
fi

rm -rf "$work"
mkdir -p "$work"

run_boot() {
    name=$1
    append=$2
    input_mode=$3
    log="$work/$name.log"

    set +e
    if test "$input_mode" = shell; then
        (
            sleep "$shell_input_delay"
            printf '%s\n' \
                'mount -t proc proc /proc' \
                'echo PROC_CMDLINE_BEGIN' \
                'cat /proc/cmdline' \
                'echo PROC_CMDLINE_END' \
                'echo RDINIT_SH_OK' \
                'echo DONE_RDINIT' \
                'sync' \
                'poweroff -f'
            sleep 2
        ) | timeout --signal=TERM "$timeout_seconds" "$qemu" \
            -M virt \
            -cpu max \
            -m 512M \
            -smp 1 \
            -nographic \
            -no-reboot \
            -bios default \
            -kernel "$image" \
            -initrd "$initramfs" \
            -append "$append" \
            >"$log" 2>&1
        status=$?
    else
        timeout --signal=TERM "$timeout_seconds" "$qemu" \
            -M virt \
            -cpu max \
            -m 512M \
            -smp 1 \
            -nographic \
            -no-reboot \
            -bios default \
            -kernel "$image" \
            -initrd "$initramfs" \
            -append "$append" \
            </dev/null >"$log" 2>&1
        status=$?
    fi
    set -e

    case "$status" in
        0|124|143) ;;
        *)
            printf '%s\n' "LINUX_RUNTIME_ERROR lane=$name qemu_status=$status" >&2
            tail -n 160 "$log" >&2
            exit "$status"
            ;;
    esac
    printf '%s\n' "LINUX_RUNTIME_BOOT lane=$name qemu_status=$status log=$log"
}

require_log() {
    lane=$1
    pattern=$2
    log="$work/$lane.log"
    if ! grep -E "$pattern" "$log" >/dev/null; then
        printf '%s\n' "LINUX_RUNTIME_MISSING lane=$lane pattern=$pattern" >&2
        tail -n 200 "$log" >&2
        exit 1
    fi
}

common="console=ttyS0 earlycon=sbi loglevel=8 panic=-1"
init_append="$common rdinit=/init"
if test "$profile" = p1; then
    init_append="$init_append minic_runtime=p1"
fi
run_boot rdinit-init "$init_append" none

# Preserve the Python-era runtime contract: this is not just a banner test.
# We require kernel identity, initramfs/devtmpfs setup, PID1 handoff, and the
# user-space markers emitted by the validation initramfs.
require_log rdinit-init "Linux version $expected_release"
require_log rdinit-init "Kernel command line:.*rdinit=/init"
require_log rdinit-init "devtmpfs: initialized"
require_log rdinit-init "(Trying to unpack rootfs image as initramfs|Unpacking initramfs)"
require_log rdinit-init "(Run /init as init process|Starting init: /init)"
require_log rdinit-init "INIT_PROC_MOUNT=PASS"
require_log rdinit-init "INIT_SYSFS_MOUNT=PASS"
require_log rdinit-init "INIT_DEVTMPFS_MOUNT=PASS"
require_log rdinit-init "INIT_CMDLINE_READ=PASS"
require_log rdinit-init "INIT_TMPFS_MOUNT=PASS"
require_log rdinit-init "INIT_TMPFS_RW=PASS"
require_log rdinit-init "MINIC_LINUX_RUNTIME_INIT_END fail=0"
require_log rdinit-init "USER_SHELL_OK"
require_log rdinit-init "DONE_COMMANDS"
printf '%s\n' "LINUX_RUNTIME_RDINIT_INIT=PASS release=$expected_release profile=$profile"

if test "$profile" = fast; then
    printf '%s\n' "LINUX_RUNTIME_EXACT=PASS release=$expected_release profile=fast lanes=1/1"
    exit 0
fi
if test "$profile" = p1; then
    require_log rdinit-init "Kernel command line:.*minic_runtime=p1"
    require_log rdinit-init "RUNTIME_V2_SYSCALL_BEGIN"
    require_log rdinit-init "RUNTIME_V2_SYSCALL_PASS eventfd-rw"
    require_log rdinit-init "RUNTIME_V2_SYSCALL_PASS epoll-eventfd"
    require_log rdinit-init "RUNTIME_V2_SYSCALL_PASS timerfd-read"
    require_log rdinit-init "RUNTIME_V2_SYSCALL_PASS signalfd-read"
    require_log rdinit-init "RUNTIME_V2_SYSCALL_PASS inotify-create"
    require_log rdinit-init "RUNTIME_V2_SYSCALL_PASS socketpair-rw"
    require_log rdinit-init "RUNTIME_V2_SYSCALL_PASS ipv6-tcp-loopback"
    require_log rdinit-init "RUNTIME_V2_SYSCALL_PASS memfd-rw"
    require_log rdinit-init "RUNTIME_V2_SYSCALL_PASS pidfd-open"
    require_log rdinit-init "RUNTIME_V2_SYSCALL_PASS futex-wake"
    require_log rdinit-init "RUNTIME_V2_SYSCALL_PASS io-uring-setup"
    require_log rdinit-init "RUNTIME_V2_SYSCALL_PASS mmap-mprotect"
    require_log rdinit-init "RUNTIME_V2_SYSCALL_END pass=12 fail=0"
    require_log rdinit-init "INIT_RUNTIME_PROBE=PASS"
    printf '%s\n' "LINUX_RUNTIME_EXACT=PASS release=$expected_release profile=p1 lanes=1/1"
    exit 0
fi
if test "$profile" != full; then
    printf '%s\n' "LINUX_RUNTIME_ERROR unsupported profile=$profile" >&2
    exit 2
fi

run_boot rdinit-sh "$common rdinit=/bin/sh" shell
require_log rdinit-sh "Linux version $expected_release"
require_log rdinit-sh "Kernel command line:.*rdinit=/bin/sh"
require_log rdinit-sh "(Run /bin/sh as init process|Starting init: /bin/sh)"
require_log rdinit-sh "PROC_CMDLINE_BEGIN"
require_log rdinit-sh "console=ttyS0.*rdinit=/bin/sh"
require_log rdinit-sh "PROC_CMDLINE_END"
require_log rdinit-sh "RDINIT_SH_OK"
require_log rdinit-sh "DONE_RDINIT"
printf '%s\n' "LINUX_RUNTIME_RDINIT_SH=PASS release=$expected_release"

printf '%s\n' "LINUX_RUNTIME_EXACT=PASS release=$expected_release profile=full lanes=2/2"
