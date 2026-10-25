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
bios=${RV32I_QEMU_BIOS:-/usr/lib/riscv32-linux-gnu/opensbi/generic/fw_dynamic.bin}
if [[ ! -s "$bios" ]]; then
  echo "RV32I_QEMU_P0=BLOCKED missing_RV32_OpenSBI=$bios" >&2
  exit 2
fi

# The guest executes the real Linux RV32 syscalls (getpid=172, write=64).
# Check PID1 before issuing the sole success marker. No libc, host executables,
# pseudo-root filesystem, or RV64 ELF is allowed in this initramfs.
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

mkdir -p "$work/guest/dev"
install -m 755 "$work/init" "$work/guest/init"
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

printf 'RV32I_QEMU_P0=START image_sha256=%s guest=rv32i/ilp32 pid1=ELF32 firmware=%s timeout=45s\n' \
  "$image_sha" "$bios"
set +e
timeout --signal=TERM 45s qemu-system-riscv32 \
  -M virt -cpu max -m 512M -smp 1 -nographic -no-reboot \
  -bios "$bios" -kernel "$image" -dtb "$dtb" \
  -device "loader,file=$work/initramfs.cpio.gz,addr=$initrd_addr,force-raw=on" \
  -append 'console=ttyS0 earlycon=sbi loglevel=8 panic=-1 rdinit=/init' \
  </dev/null >"$work/qemu-serial.log" 2>&1
status=$?
set -e
printf 'qemu_status=%s\nimage_sha256=%s\n' "$status" "$image_sha" \
  >"$work/provenance.txt"
if [[ "$status" != 124 ]]; then
  echo "RV32I_QEMU_P0=FAIL qemu_exit=$status expected_bounded_timeout=124" >&2
  tail -n 110 "$work/qemu-serial.log" >&2
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
    '(Run /init as init process|Starting init: /init)' \
    '^RV32I_LINUX_PID1=PASS syscall=getpid,write elf=ELF32 abi=ilp32'; do
  if ! grep -Eq "$pattern" "$work/qemu-serial.log"; then
    echo "RV32I_QEMU_P0=FAIL missing=$pattern image=$image_sha" >&2
    tail -n 140 "$work/qemu-serial.log" >&2
    exit 1
  fi
done
if grep -Eq '(Kernel panic|RV32I_LINUX_PID1=FAIL|Oops:|Unable to handle kernel)' \
    "$work/qemu-serial.log"; then
  echo "RV32I_QEMU_P0=FAIL guest_panic_or_pid1_failure image=$image_sha" >&2
  tail -n 110 "$work/qemu-serial.log" >&2
  exit 1
fi
echo "RV32I_QEMU_P0=PASS image_sha256=$image_sha guest=rv32i/ilp32 original_Kbuild=true PID1=1 syscalls=getpid,write QEMU_P1=NOT_RUN"
