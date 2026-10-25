#!/usr/bin/env bash
# Boot the attested RV32 MiniC Linux Image in the real RV32 system emulator.
# This is an early PID1/ELF32 syscall smoke, NOT the historical 12/12 P1 gate.
# The kernel Image is read-only; this helper only builds a tiny external
# initramfs from GNU assembler and standard GNU cpio tooling.
set -Eeuo pipefail

: "${RV32I_QEMU_IMAGE:?set the exact seven-shard MiniC Linux Image}"
: "${RV32I_QEMU_WORK:?set an isolated boot evidence directory}"

for cmd in qemu-system-riscv32 riscv64-linux-gnu-as riscv64-linux-gnu-ld \
    riscv64-linux-gnu-readelf cpio sudo mknod gzip sha256sum timeout \
    fdtput fdtget; do
  command -v "$cmd" >/dev/null || { echo "RV32I_QEMU_P0=BLOCKED missing=$cmd" >&2; exit 2; }
done
test -s "$RV32I_QEMU_IMAGE"
mkdir -p "$RV32I_QEMU_WORK"
image=$(realpath "$RV32I_QEMU_IMAGE")
work=$(realpath "$RV32I_QEMU_WORK")
image_sha=$(sha256sum "$image" | cut -d' ' -f1)
p1=${RV32I_QEMU_P1:-0}
shell=${RV32I_QEMU_SHELL:-0}
fast=${RV32I_QEMU_FAST:-0}
[[ "$fast" == 0 || "$fast" == 1 ]] || {
  echo 'RV32I_QEMU=FAIL invalid_fast_mode' >&2; exit 2;
}
[[ "$fast" == 0 || ( "$p1" == 1 && "$shell" == 0 ) ]] || {
  echo 'RV32I_QEMU=FAIL fast_requires_separate_p1_userspace_without_shell' >&2; exit 2;
}
[[ "$shell" == 0 || "$shell" == 1 ]] || {
  echo 'RV32I_QEMU=FAIL invalid_shell_mode' >&2; exit 2;
}
[[ "$shell" == 0 || "$p1" == 1 ]] || {
  echo 'RV32I_QEMU=FAIL shell_requires_p1_runtime_initramfs' >&2; exit 2;
}
[[ "$p1" == 0 || "$p1" == 1 ]] || {
  echo 'RV32I_QEMU=FAIL invalid_p1_mode' >&2; exit 2;
}
if [[ "$p1" == 1 ]]; then
  command -v riscv64-linux-gnu-gcc >/dev/null || {
    echo 'RV32I_QEMU_P1=BLOCKED missing_GNU_C_compiler' >&2; exit 2;
  }
fi
bios=${RV32I_QEMU_BIOS:-/usr/lib/riscv32-linux-gnu/opensbi/generic/fw_dynamic.bin}
if [[ ! -s "$bios" ]]; then
  echo "RV32I_QEMU_P0=BLOCKED missing_RV32_OpenSBI=$bios" >&2
  exit 2
fi

# The guest executes the real Linux RV32 syscalls (getpid=172, write=64).
# Check PID1 before issuing the sole success marker. No libc, host executables,
# pseudo-root filesystem, or RV64 ELF is allowed in this initramfs.
if [[ "$p1" == 1 ]]; then
  # Only GNU-built freestanding userspace changes; the original, pinned
  # 1741/1741 MiniC Linux C-object Image is never recompiled or relinked.
  build_guest() {
    local source="$1" target="$2"
    riscv64-linux-gnu-gcc -S -O1 -march=rv32i -mabi=ilp32 \
      -nostdinc -nostdlib -ffreestanding -fno-builtin -fno-pic -fno-pie \
      -fno-stack-protector -fno-tree-loop-distribute-patterns \
      -fno-asynchronous-unwind-tables -fno-unwind-tables \
      "$source" -o "$work/$target.S"
    riscv64-linux-gnu-as -march=rv32i -mabi=ilp32 \
      "$work/$target.S" -o "$work/$target.o"
    riscv64-linux-gnu-ld -m elf32lriscv -static -e _start \
      "$work/$target.o" -o "$work/$target"
    riscv64-linux-gnu-readelf -h "$work/$target" >"$work/$target-elf.txt"
    for contract in 'Class:[[:space:]]+ELF32' \
                    'Machine:[[:space:]]+RISC-V' \
                    'Type:[[:space:]]+EXEC' \
                    'Flags:[[:space:]]+0x0([[:space:]]|$)'; do
      grep -Eq "$contract" "$work/$target-elf.txt" || {
        echo "RV32I_GUEST_ELF=FAIL file=$target contract=$contract" >&2; exit 3;
      }
    done
    echo "RV32I_GUEST_ELF=PASS file=$target ELF32=true ILP32=true RV32I=true"
  }
  build_guest tests/external/linux/runtime_initramfs/rv32-init-raw.c init
  build_guest tests/external/linux/runtime_initramfs/rv32-p1-raw.c runtime-probe
  build_guest tests/external/linux/runtime_initramfs/rv32-sh-raw.c sh
else
cat >"$work/init.S" <<'ASM'
    .section .text
    .globl _start
_start:
    li a7, 172
    ecall
    li t0, 1
    bne a0, t0, failed
    li a0, 1
    la a1, success
    la t0, success_end
    sub a2, t0, a1
    mv s0, a2
    li a7, 64
    ecall
    bne a0, s0, failed
hold:
    j hold
failed:
    li a0, 1
    la a1, failure
    la t0, failure_end
    sub a2, t0, a1
    li a7, 64
    ecall
    li a0, 10
    li a7, 93
    ecall
    j failed
    .section .rodata
success:
    .ascii "RV32I_LINUX_PID1=PASS syscall=getpid,write elf=ELF32 abi=ilp32\n"
success_end:
failure:
    .ascii "RV32I_LINUX_PID1=FAIL syscall=getpid_or_write\n"
failure_end:
ASM
riscv64-linux-gnu-as -march=rv32i -mabi=ilp32 \
  "$work/init.S" -o "$work/init.o"
riscv64-linux-gnu-ld -m elf32lriscv -static -e _start \
  "$work/init.o" -o "$work/init"
riscv64-linux-gnu-readelf -h "$work/init" >"$work/init-elf.txt"
fi
for contract in \
    'Class:[[:space:]]+ELF32' \
    'Machine:[[:space:]]+RISC-V' \
    'Type:[[:space:]]+EXEC' \
    'Flags:[[:space:]]+0x0([[:space:]]|$)'; do
  if ! grep -Eq "$contract" "$work/init-elf.txt"; then
    echo "RV32I_QEMU_P0=FAIL invalid_init_elf expected=$contract" >&2
    sed -n '1,45p' "$work/init-elf.txt" >&2
    exit 3
  fi
done
# ELF RISC-V e_flags 0x0 means soft-float ABI and no RVC extension. GNU
# binutils 2.42 prints just "Flags: 0x0", without a textual ABI suffix.
echo 'RV32I_QEMU_P0_INIT=PASS ELF32=1 riscv=1 ET_EXEC=1 e_flags=0x0 soft_float=1 no_RVC=1'

mkdir -p "$work/guest/dev" "$work/guest/tmp" "$work/guest/proc" \
         "$work/guest/sys" "$work/guest/bin"
chmod 1777 "$work/guest/tmp"
install -m 755 "$work/init" "$work/guest/init"
if [[ "$p1" == 1 ]]; then
  install -m 755 "$work/runtime-probe" "$work/guest/bin/runtime-probe"
  install -m 755 "$work/sh" "$work/guest/bin/sh"
fi
if [[ ! -e "$work/guest/dev/console" ]]; then
  sudo mknod -m 600 "$work/guest/dev/console" c 5 1
fi
if [[ ! -e "$work/guest/dev/null" ]]; then
  sudo mknod -m 666 "$work/guest/dev/null" c 1 3
fi
test -c "$work/guest/dev/console"
test -c "$work/guest/dev/null"
(
  cd "$work/guest"
  find . -print0 | LC_ALL=C sort -z | cpio --null -o --format=newc --quiet
) | gzip -9n >"$work/initramfs.cpio.gz"
test -s "$work/initramfs.cpio.gz"

# Reuse the already certified RV64 large-Image QEMU 8.2 workaround:
# -initrd defaults to Image load address + 128 MiB, which OVERLAPS this
# genuine ~269 MiB RV32 Image. Keep the Image and initramfs bytes untouched;
# encode a separate initrd region in QEMU's own /chosen DTB, and preload
# precisely those bytes with QEMU's generic loader.
dtb="$work/virt-rv32-large-image.dtb"
qemu-system-riscv32 -M "virt,dumpdtb=$dtb" -cpu max -m 512M -smp 1 \
  -nographic -no-reboot -bios "$bios" -kernel "$image" \
  >"$work/virt-dumpdtb.log" 2>&1
test -s "$dtb"
fdtget -p "$dtb" /chosen >/dev/null 2>&1 || fdtput -c "$dtb" /chosen
initrd_addr=0x98000000
initrd_bytes=$(stat -c '%s' "$work/initramfs.cpio.gz")
image_bytes=$(stat -c '%s' "$image")
image_end=$((0x80400000 + image_bytes))
initrd_end=$((initrd_addr + initrd_bytes))
if (( image_end > initrd_addr || initrd_end >= 0xa0000000 )); then
  echo "RV32I_QEMU_P0=FAIL memory_overlap image_end=$image_end initrd_addr=$initrd_addr initrd_end=$initrd_end" >&2
  exit 3
fi
printf -v start_hex '%x' "$initrd_addr"
printf -v end_hex '%x' "$initrd_end"
fdtput -t x "$dtb" /chosen linux,initrd-start 0 "$start_hex"
fdtput -t x "$dtb" /chosen linux,initrd-end 0 "$end_hex"
test "$(fdtget -t x "$dtb" /chosen linux,initrd-start)" = "0 $start_hex"
test "$(fdtget -t x "$dtb" /chosen linux,initrd-end)" = "0 $end_hex"
echo "RV32I_QEMU_P0_DTB=PASS image_bytes=$image_bytes image_end=$image_end initrd_start=$start_hex initrd_end=$end_hex no_overlap=true"

rv32i_timeout=${RV32I_QEMU_TIMEOUT_SECONDS:-45}
[[ "$rv32i_timeout" =~ ^[0-9]+$ ]] && (( rv32i_timeout >= 10 && rv32i_timeout <= 120 )) || {
  echo 'RV32I_QEMU_P0=FAIL invalid_timeout_seconds' >&2
  exit 2
}
printf 'RV32I_QEMU_P0=START image_sha256=%s guest=rv32i/ilp32 pid1=ELF32 firmware=%s timeout=%ss p1=%s shell=%s\n' \
  "$image_sha" "$bios" "$rv32i_timeout" "$p1" "$shell"
rdinit=/init
append_p1=
if [[ "$p1" == 1 && "$fast" != 1 ]]; then append_p1=' minic_runtime=p1'; fi
if [[ "$shell" == 1 ]]; then rdinit=/bin/sh; append_p1=; fi
qemu_args=(qemu-system-riscv32 -M virt -cpu max -m 512M -smp 1
  -nographic -no-reboot -bios "$bios" -kernel "$image" -dtb "$dtb"
  -device "loader,file=$work/initramfs.cpio.gz,addr=$initrd_addr,force-raw=on"
  -append "console=ttyS0 earlycon=${RV32I_QEMU_EARLYCON:-sbi} loglevel=8 panic=-1 rdinit=$rdinit$append_p1")
set +e
if [[ "$shell" == 1 ]]; then
  # Like RV64 runtime_boot.sh shell profile: send actual commands over ttyS0.
  # The first command arrives after the real kernel has initialized serial.
  (
    sleep "${RV32I_QEMU_SHELL_INPUT_DELAY_SECONDS:-13}"
    printf '%s\n' 'mount -t proc proc /proc' 'echo PROC_CMDLINE_BEGIN' \
      'cat /proc/cmdline' 'echo PROC_CMDLINE_END' \
      'echo RDINIT_SH_OK' 'echo DONE_RDINIT' 'sync' 'poweroff -f'
  ) | timeout --signal=TERM "${rv32i_timeout}s" "${qemu_args[@]}" \
    >"$work/qemu-serial.log" 2>&1
  status=$?
else
  timeout --signal=TERM "${rv32i_timeout}s" "${qemu_args[@]}" \
    </dev/null >"$work/qemu-serial.log" 2>&1
  status=$?
fi
set -e
printf 'qemu_status=%s\nimage_sha256=%s\n' "$status" "$image_sha" \
  >"$work/provenance.txt"
if [[ "$p1" == 1 ]]; then
  # The historical P0/P1 contract requires CLEAN guest powerdown, not an
  # allowed timeout.  A guest that prints PASS and then hangs must FAIL.
  [[ "$status" == 0 ]] || {
    echo "RV32I_QEMU_P1=FAIL qemu_exit=$status" >&2
    tail -n 110 "$work/qemu-serial.log" >&2
    exit 1
  }
elif [[ "$status" != 124 ]]; then
  echo "RV32I_QEMU_P0=FAIL qemu_exit=$status expected_bounded_timeout=124" >&2
  tail -n 110 "$work/qemu-serial.log" >&2
  if [[ "${RV32I_QEMU_SKIP_REPLAY:-0}" == 1 ]]; then
    echo 'RV32I_QEMU_P0_DIAG=SKIP_REPLAY cohort_mode=true first_serial_retained=true' >&2
    exit 1
  fi
  # A pre-console shutdown must not be mistaken for a passing Linux boot.
  # Replay ONLY the same immutable Image, now with UART earlycon and QEMU's
  # CPU exception/guest-error trace, so the next fix targets real evidence.
  set +e
  timeout --signal=TERM 20s qemu-system-riscv32 \
    -M virt -cpu max -m 512M -smp 1 -nographic -no-reboot \
    -bios "$bios" -kernel "$image" -dtb "$dtb" \
    -device "loader,file=$work/initramfs.cpio.gz,addr=$initrd_addr,force-raw=on" \
    -append 'console=ttyS0 earlycon=uart8250,mmio,0x10000000,115200n8 loglevel=8 panic=-1 rdinit=/init' \
    -d int,cpu_reset,guest_errors -D "$work/qemu-debug.log" \
    </dev/null >"$work/qemu-uart-serial.log" 2>&1
  uart_status=$?
  set -e
  printf 'diagnostic_uart_qemu_status=%s\n' "$uart_status" >>"$work/provenance.txt"
  echo "RV32I_QEMU_P0_DIAG uart_status=$uart_status exception_events=$(grep -c -E 'exception|riscv_cpu_do_interrupt|cpu_reset' "$work/qemu-debug.log" || true)" >&2
  tail -n 100 "$work/qemu-uart-serial.log" >&2
  tail -n 80 "$work/qemu-debug.log" >&2
  exit 1
fi
for pattern in \
    'Linux version 6\.6\.143' \
    "$(if [[ "$shell" == 1 ]]; then printf '%s' '(Run /bin/sh as init process|Starting init: /bin/sh)'; else printf '%s' '(Run /init as init process|Starting init: /init)'; fi)" \
    "$(if [[ "$shell" == 1 ]]; then printf '%s' '^MINIC_LINUX_RUNTIME_SH_READY'; elif [[ "$fast" == 1 ]]; then printf '%s' '^MINIC_LINUX_RUNTIME_INIT_BEGIN'; elif [[ "$p1" == 1 ]]; then printf '%s' '^RUNTIME_V2_SYSCALL_BEGIN'; else printf '%s' '^RV32I_LINUX_PID1=PASS syscall=getpid,write elf=ELF32 abi=ilp32'; fi)"; do
  if ! grep -Eq "$pattern" "$work/qemu-serial.log"; then
    echo "RV32I_QEMU_P0=FAIL missing=$pattern image=$image_sha" >&2
    tail -n 140 "$work/qemu-serial.log" >&2
    exit 1
  fi
done
if grep -Eiq '(Kernel panic|RV32I_LINUX_PID1=FAIL|RV32I_LINUX_P1=FAIL|RUNTIME_V2_SYSCALL_FAIL|Oops:|Unable to handle kernel|Insufficient stack space|(^|[^a-z])BUG:|soft lockup|workqueue lockup|Unknown symbol|invalid module format)' \
    "$work/qemu-serial.log"; then
  echo "RV32I_QEMU_P0=FAIL guest_panic_or_pid1_failure image=$image_sha" >&2
  tail -n 110 "$work/qemu-serial.log" >&2
  exit 1
fi
if [[ "$p1" == 1 ]]; then
  grep -Eq 'reboot: Power down' "$work/qemu-serial.log" || {
    echo "RV32I_QEMU_P1=FAIL missing_clean_powerdown image=$image_sha" >&2
    exit 1
  }
  if [[ "$fast" == 1 ]]; then
    python3 tests/external/linux/runtime_v2_log_check.py \
      --profile p0 --log "$work/qemu-serial.log"
    grep -Fq 'MINIC_LINUX_RUNTIME_INIT_END fail=0' "$work/qemu-serial.log"
    echo "RV32I_QEMU_P0=PASS profile=full_historical_fast image_sha256=$image_sha original_Kbuild=true proc_sys_dev_tmpfs=true clean_powerdown=true"
    exit 0
  fi
  if [[ "$shell" == 1 ]]; then
    for marker in MINIC_LINUX_RUNTIME_SH_READY PROC_CMDLINE_BEGIN \
                  PROC_CMDLINE_END RDINIT_SH_OK DONE_RDINIT; do
      grep -Fq "$marker" "$work/qemu-serial.log" || {
        echo "RV32I_QEMU_SHELL=FAIL missing=$marker" >&2; exit 1;
      }
    done
    grep -Eq 'Kernel command line:.*rdinit=/bin/sh' "$work/qemu-serial.log"
    # Serial TTY emits CRLF; normalize only for exact-result assertions.
    # Keep the raw byte-exact guest UART log separately as original evidence.
    tr -d '\r' <"$work/qemu-serial.log" >"$work/qemu-shell-normalized.log"
    # Serial also echoes INPUT. Do not confuse the literal input commands
    # ("echo X") with the executed guest output ("X").
    grep -Eq '^console=ttyS0 .*rdinit=/bin/sh
        echo "RV32I_QEMU_SHELL=FAIL no_executed_result=$line" >&2; exit 1;
      }
    done
    if grep -Fq 'RV32_SHELL_' "$work/qemu-serial.log"; then
      echo "RV32I_QEMU_SHELL=FAIL invalid_shell_commands" >&2; exit 1;
    fi
    echo "RV32I_QEMU_SHELL=PASS image_sha256=$image_sha genuine_ELF32=true real_QEMU=true clean_powerdown=true"
    exit 0
  fi
  # Use the SAME canonical manifest as RV64, including probe-exec.
  for pattern in 'INIT_PROC_MOUNT=PASS' 'INIT_SYSFS_MOUNT=PASS' \
                 'INIT_DEVTMPFS_MOUNT=PASS' 'INIT_CMDLINE_READ=PASS' \
                 'INIT_TMPFS_MOUNT=PASS' 'INIT_TMPFS_RW=PASS' \
                 'MINIC_LINUX_RUNTIME_INIT_END fail=0' \
                 'USER_SHELL_OK' 'DONE_COMMANDS'; do
    grep -Fq "$pattern" "$work/qemu-serial.log" || {
      echo "RV32I_QEMU_P0=FAIL missing=$pattern" >&2; exit 1;
    }
  done
  python3 tests/external/linux/runtime_v2_log_check.py \
    --profile p1 --log "$work/qemu-serial.log"
  mapfile -t manifest_p1 < <(awk -F '\t' '$1=="p1"{print $3}' tests/external/linux/runtime_v2_checks.tsv)
  [[ "${#manifest_p1[@]}" == 13 ]] || {
    echo "RV32I_QEMU_P1=FAIL official_P1_manifest_size=${#manifest_p1[@]}" >&2; exit 1;
  }
  for pattern in "${manifest_p1[@]}"; do
    grep -Eq "$pattern" "$work/qemu-serial.log" || {
      echo "RV32I_QEMU_P1=FAIL official_manifest_missing=$pattern" >&2; exit 1;
    }
  done
  [[ "$(grep -Ec '^RUNTIME_V2_SYSCALL_PASS ' "$work/qemu-serial.log")" == 12 ]] || {
    echo "RV32I_QEMU_P1=FAIL required_exactly_12_pass_markers" >&2; exit 1;
  }
  # Match all 12 historical RV64 P1 families and final count in the SAME
  # immutable-image boot; a timeout or incomplete log can never pass.
  for name in eventfd-rw epoll-eventfd timerfd-read signalfd-read \
              inotify-create socketpair-rw ipv6-tcp-loopback memfd-rw \
              pidfd-open futex-wake io-uring-setup mmap-mprotect; do
    grep -Fq "RUNTIME_V2_SYSCALL_PASS $name" "$work/qemu-serial.log" || {
      echo "RV32I_QEMU_P1=FAIL missing=$name image=$image_sha" >&2
      tail -n 100 "$work/qemu-serial.log" >&2
      exit 1
    }
  done
  grep -Fq 'RUNTIME_V2_SYSCALL_END pass=12 fail=0' "$work/qemu-serial.log"
  grep -Fq 'RV32I_LINUX_P1=PASS syscalls=12 child=ELF32 ilp32=true execve=true' "$work/qemu-serial.log"
  grep -Fq 'INIT_RUNTIME_PROBE=PASS' "$work/qemu-serial.log"
  echo "RV32I_QEMU_P1=PASS image_sha256=$image_sha real_QEMU=rv32i original_Kbuild=true minic_C=1741/1741 syscalls=12/12"
else
  echo "RV32I_QEMU_P0=PASS image_sha256=$image_sha guest=rv32i/ilp32 original_Kbuild=true PID1=1 syscalls=getpid,write QEMU_P1=NOT_RUN"
fi
 "$work/qemu-shell-normalized.log"
    for line in PROC_CMDLINE_BEGIN PROC_CMDLINE_END RDINIT_SH_OK DONE_RDINIT; do
      grep -Fxq "$line" "$work/qemu-shell-normalized.log" || {
        echo "RV32I_QEMU_SHELL=FAIL no_executed_result=$line" >&2; exit 1;
      }
    done
    if grep -Fq 'RV32_SHELL_' "$work/qemu-serial.log"; then
      echo "RV32I_QEMU_SHELL=FAIL invalid_shell_commands" >&2; exit 1;
    fi
    echo "RV32I_QEMU_SHELL=PASS image_sha256=$image_sha genuine_ELF32=true real_QEMU=true clean_powerdown=true"
    exit 0
  fi
  # Use the SAME canonical manifest as RV64, including probe-exec.
  for pattern in 'INIT_PROC_MOUNT=PASS' 'INIT_SYSFS_MOUNT=PASS' \
                 'INIT_DEVTMPFS_MOUNT=PASS' 'INIT_CMDLINE_READ=PASS' \
                 'INIT_TMPFS_MOUNT=PASS' 'INIT_TMPFS_RW=PASS' \
                 'MINIC_LINUX_RUNTIME_INIT_END fail=0' \
                 'USER_SHELL_OK' 'DONE_COMMANDS'; do
    grep -Fq "$pattern" "$work/qemu-serial.log" || {
      echo "RV32I_QEMU_P0=FAIL missing=$pattern" >&2; exit 1;
    }
  done
  python3 tests/external/linux/runtime_v2_log_check.py \
    --profile p1 --log "$work/qemu-serial.log"
  mapfile -t manifest_p1 < <(awk -F '\t' '$1=="p1"{print $3}' tests/external/linux/runtime_v2_checks.tsv)
  [[ "${#manifest_p1[@]}" == 13 ]] || {
    echo "RV32I_QEMU_P1=FAIL official_P1_manifest_size=${#manifest_p1[@]}" >&2; exit 1;
  }
  for pattern in "${manifest_p1[@]}"; do
    grep -Eq "$pattern" "$work/qemu-serial.log" || {
      echo "RV32I_QEMU_P1=FAIL official_manifest_missing=$pattern" >&2; exit 1;
    }
  done
  [[ "$(grep -Ec '^RUNTIME_V2_SYSCALL_PASS ' "$work/qemu-serial.log")" == 12 ]] || {
    echo "RV32I_QEMU_P1=FAIL required_exactly_12_pass_markers" >&2; exit 1;
  }
  # Match all 12 historical RV64 P1 families and final count in the SAME
  # immutable-image boot; a timeout or incomplete log can never pass.
  for name in eventfd-rw epoll-eventfd timerfd-read signalfd-read \
              inotify-create socketpair-rw ipv6-tcp-loopback memfd-rw \
              pidfd-open futex-wake io-uring-setup mmap-mprotect; do
    grep -Fq "RUNTIME_V2_SYSCALL_PASS $name" "$work/qemu-serial.log" || {
      echo "RV32I_QEMU_P1=FAIL missing=$name image=$image_sha" >&2
      tail -n 100 "$work/qemu-serial.log" >&2
      exit 1
    }
  done
  grep -Fq 'RUNTIME_V2_SYSCALL_END pass=12 fail=0' "$work/qemu-serial.log"
  grep -Fq 'RV32I_LINUX_P1=PASS syscalls=12 child=ELF32 ilp32=true execve=true' "$work/qemu-serial.log"
  grep -Fq 'INIT_RUNTIME_PROBE=PASS' "$work/qemu-serial.log"
  echo "RV32I_QEMU_P1=PASS image_sha256=$image_sha real_QEMU=rv32i original_Kbuild=true minic_C=1741/1741 syscalls=12/12"
else
  echo "RV32I_QEMU_P0=PASS image_sha256=$image_sha guest=rv32i/ilp32 original_Kbuild=true PID1=1 syscalls=getpid,write QEMU_P1=NOT_RUN"
fi
