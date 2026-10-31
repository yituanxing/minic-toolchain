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
p2=${RV32I_QEMU_P2:-0}
c11=${RV32I_QEMU_P2_CANONICAL11:-0}
f18=${RV32I_QEMU_P2_FULL18:-0}
shell=${RV32I_QEMU_SHELL:-0}
[[ "$p2" == 0 || "$p2" == 1 ]] || {
  echo 'RV32I_QEMU_P2=FAIL invalid_p2_mode' >&2; exit 2;
}
[[ "$c11" == 0 || "$c11" == 1 ]] || {
  echo 'RV32I_QEMU_P2=FAIL invalid_canonical11_mode' >&2; exit 2;
}
[[ "$c11" == 0 || "$p2" == 1 ]] || {
  echo 'RV32I_QEMU_P2=FAIL canonical11_requires_original_p2_nine_gate' >&2; exit 2;
}
[[ "$f18" == 0 || "$f18" == 1 ]] || {
  echo 'RV32I_QEMU_P2=FAIL invalid_full18_mode' >&2; exit 2;
}
[[ "$f18" == 0 || ( "$p2" == 1 && "$c11" == 1 ) ]] || {
  echo 'RV32I_QEMU_P2=FAIL full18_requires_certified_nine_and_canonical11' >&2; exit 2;
}
[[ "$p2" == 0 || ( "$p1" == 1 && "$shell" == 0 && "${RV32I_QEMU_FAST:-0}" == 0 ) ]] || {
  echo 'RV32I_QEMU_P2=FAIL requires_genuine_ELF32_userland_without_other_profiles' >&2; exit 2;
}
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
    # CRT-less _start intentionally has no GP initializer. GNU RISC-V
    # linker relaxation may otherwise rewrite absolute BSS accesses to
    # lw/sw offset(gp), where gp=0: we observed a real SIGSEGV at
    # mark+0x8c on core_ok (-2031(gp)) in CI #40209961682.
    # --no-relax is REQUIRED for this standalone freestanding guest.
    riscv64-linux-gnu-ld --no-relax -m elf32lriscv -static -e _start \
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
  if [[ "$p2" == 1 ]]; then
    build_guest tests/external/linux/runtime_initramfs/rv32-p2-raw.c runtime-p2
    # Exact guest PC -> function proof, no host recompilation of the Image.
    riscv64-linux-gnu-objdump -dr "$work/runtime-p2" >"$work/runtime-p2-disassembly.txt"
    riscv64-linux-gnu-nm -n "$work/runtime-p2" >"$work/runtime-p2-symbols.txt"
    if grep -Eq '[-0-9]+\(gp\)' "$work/runtime-p2-disassembly.txt"; then
      echo 'RV32I_GUEST_P2=FAIL no_CRT_GP_register_was_uninitialized' >&2
      exit 3
    fi
    if [[ "$c11" == 1 ]]; then
      build_guest tests/external/linux/runtime_initramfs/rv32-p2-canonical11-raw.c runtime-p2-canonical11
      riscv64-linux-gnu-objdump -dr "$work/runtime-p2-canonical11" >"$work/runtime-p2-canonical11-disassembly.txt"
      if grep -Eq '[-0-9]+\(gp\)' "$work/runtime-p2-canonical11-disassembly.txt"; then
        echo 'RV32I_GUEST_C11=FAIL freestanding_gp_relative_access' >&2
        exit 3
      fi
    fi
    if [[ "$f18" == 1 ]]; then
      build_guest tests/external/linux/runtime_initramfs/rv32-p2-full18-raw.c runtime-p2-full18
      riscv64-linux-gnu-objdump -dr "$work/runtime-p2-full18" >"$work/runtime-p2-full18-disassembly.txt"
      if grep -Eq '[-0-9]+\(gp\)' "$work/runtime-p2-full18-disassembly.txt"; then
        echo 'RV32I_GUEST_F18=FAIL freestanding_gp_relative_access' >&2
        exit 3
      fi
    fi
  fi
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
  if [[ "$p2" == 1 ]]; then
    install -m 755 "$work/runtime-p2" "$work/guest/bin/runtime-p2"
    if [[ "$c11" == 1 ]]; then
      install -m 755 "$work/runtime-p2-canonical11" "$work/guest/bin/runtime-p2-canonical11"
    fi
    if [[ "$f18" == 1 ]]; then
      install -m 755 "$work/runtime-p2-full18" "$work/guest/bin/runtime-p2-full18"
    fi
    : "${RV32I_QEMU_P2_MODULE_DIR:?P2 requires six real GNU Kbuild ELF32 modules built from exact Image .config}"
    module_dir=$(realpath "$RV32I_QEMU_P2_MODULE_DIR")
    mkdir -p "$work/guest/lib/modules"
    : >"$work/rv32-p2-module-sha256.txt"
    for name in libsha256 sha256_generic sha512_generic sha3_generic blake2b_generic overlay; do
      src="$module_dir/$name.ko"
      [[ -s "$src" ]] || { echo "RV32I_QEMU_P2=FAIL missing_module=$src" >&2; exit 3; }
      riscv64-linux-gnu-readelf -h "$src" >"$work/module-$name-elf.txt"
      for contract in 'Class:[[:space:]]+ELF32' \
                      'Machine:[[:space:]]+RISC-V' \
                      'Type:[[:space:]]+REL' \
                      'Flags:[[:space:]]+0x0([[:space:]]|$)'; do
        grep -Eq "$contract" "$work/module-$name-elf.txt" || {
          echo "RV32I_QEMU_P2=FAIL module_elf=$name contract=$contract" >&2
          exit 3
        }
      done
      riscv64-linux-gnu-readelf -p .modinfo "$src" >"$work/module-$name-modinfo.txt"
      grep -Eq 'vermagic=6\.6\.143 mod_unload riscv([[:space:]]|$)' \
        "$work/module-$name-modinfo.txt" || {
          echo "RV32I_QEMU_P2=FAIL module_vermagic=$name expected=6.6.143_mod_unload_riscv" >&2
          exit 3
        }
      install -m 644 "$src" "$work/guest/lib/modules/$name.ko"
      sha256sum "$src" >>"$work/rv32-p2-module-sha256.txt"
      echo "RV32I_QEMU_P2_MODULE=STAGED name=$name real_GNU_ELF32=true"
    done
    if [[ "$c11" == 1 ]]; then
      for name in x_tables xt_tcpudp ip_tables iptable_filter \
        ip6_tables ip6table_filter libcrc32c xor raid6_pq \
        zlib_deflate lzo_compress btrfs dm-mod; do
        src="$module_dir/$name.ko"
        [[ -s "$src" ]] || { echo "RV32I_QEMU_C11=FAIL missing_module=$src" >&2; exit 3; }
        riscv64-linux-gnu-readelf -h "$src" >"$work/module-$name-elf.txt"
        for contract in 'Class:[[:space:]]+ELF32' \
                        'Machine:[[:space:]]+RISC-V' \
                        'Type:[[:space:]]+REL' \
                        'Flags:[[:space:]]+0x0([[:space:]]|$)'; do
          grep -Eq "$contract" "$work/module-$name-elf.txt" || {
            echo "RV32I_QEMU_C11=FAIL module_elf=$name contract=$contract" >&2
            exit 3
          }
        done
        riscv64-linux-gnu-readelf -p .modinfo "$src" >"$work/module-$name-modinfo.txt"
        grep -Eq 'vermagic=6\.6\.143 mod_unload riscv([[:space:]]|$)' \
          "$work/module-$name-modinfo.txt" || {
            echo "RV32I_QEMU_C11=FAIL module_vermagic=$name" >&2
            exit 3
          }
        install -m 644 "$src" "$work/guest/lib/modules/$name.ko"
        sha256sum "$src" >>"$work/rv32-p2-module-sha256.txt"
        echo "RV32I_QEMU_C11_MODULE=STAGED name=$name real_GNU_ELF32=true"
      done
    fi
    if [[ "$f18" == 1 ]]; then
      for name in nf_defrag_ipv4 nf_defrag_ipv6 nf_conntrack \
        llc stp bridge br_netfilter veth kvm drm drm_kms_helper \
        drm_shmem_helper virtio-gpu nvme-core nvme; do
        src="$module_dir/$name.ko"
        [[ -s "$src" ]] || { echo "RV32I_QEMU_F18=FAIL missing_module=$src" >&2; exit 3; }
        riscv64-linux-gnu-readelf -h "$src" >"$work/module-$name-elf.txt"
        for contract in 'Class:[[:space:]]+ELF32' \
                        'Machine:[[:space:]]+RISC-V' \
                        'Type:[[:space:]]+REL' \
                        'Flags:[[:space:]]+0x0([[:space:]]|$)'; do
          grep -Eq "$contract" "$work/module-$name-elf.txt" || {
            echo "RV32I_QEMU_F18=FAIL module_elf=$name contract=$contract" >&2
            exit 3
          }
        done
        riscv64-linux-gnu-readelf -p .modinfo "$src" >"$work/module-$name-modinfo.txt"
        grep -Eq 'vermagic=6\.6\.143 mod_unload riscv([[:space:]]|$)' \
          "$work/module-$name-modinfo.txt" || {
            echo "RV32I_QEMU_F18=FAIL module_vermagic=$name" >&2
            exit 3
          }
        install -m 644 "$src" "$work/guest/lib/modules/$name.ko"
        sha256sum "$src" >>"$work/rv32-p2-module-sha256.txt"
        echo "RV32I_QEMU_F18_MODULE=STAGED name=$name real_GNU_ELF32=true"
      done
    fi
  fi
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
[[ "$rv32i_timeout" =~ ^[0-9]+$ ]] &&
  (( rv32i_timeout >= 10 && rv32i_timeout <= ( f18 == 1 ? 300 : 120 ) )) || {
  echo 'RV32I_QEMU_P0=FAIL invalid_timeout_seconds' >&2
  exit 2
}
printf 'RV32I_QEMU_P0=START image_sha256=%s guest=rv32i/ilp32 pid1=ELF32 firmware=%s timeout=%ss p1=%s shell=%s\n' \
  "$image_sha" "$bios" "$rv32i_timeout" "$p1" "$shell"
rdinit=/init
append_p1=
if [[ "$p1" == 1 && "$fast" != 1 ]]; then
  if [[ "$f18" == 1 ]]; then append_p1=' minic_runtime=p2full18 nf_conntrack.enable_hooks=1'
  elif [[ "$c11" == 1 ]]; then append_p1=' minic_runtime=p2canonical11'
  elif [[ "$p2" == 1 ]]; then append_p1=' minic_runtime=p2'
  else append_p1=' minic_runtime=p1'; fi
fi
if [[ "$shell" == 1 ]]; then rdinit=/bin/sh; append_p1=; fi
qemu_args=(qemu-system-riscv32 -M virt -cpu max -m 512M -smp 1
  -nographic -no-reboot -bios "$bios" -kernel "$image" -dtb "$dtb"
  -device "loader,file=$work/initramfs.cpio.gz,addr=$initrd_addr,force-raw=on"
  -append "console=ttyS0 earlycon=${RV32I_QEMU_EARLYCON:-sbi} loglevel=8 panic=-1 rdinit=$rdinit$append_p1")
if [[ "$c11" == 1 ]]; then
  : "${RV32I_QEMU_P2_EXT4_IMAGE:?canonical11 requires a real ext4 disk fixture}"
  disk=$(realpath "$RV32I_QEMU_P2_EXT4_IMAGE")
  [[ -s "$disk" ]] || { echo "RV32I_QEMU_C11=FAIL missing_ext4_disk=$disk" >&2; exit 3; }
  qemu_args+=( -drive "if=none,id=rv32p2ext4,file=$disk,format=raw,snapshot=on"
               -device 'virtio-blk-device,drive=rv32p2ext4' )
  echo "RV32I_QEMU_C11_DISK=STAGED sha256=$(sha256sum "$disk" | cut -d' ' -f1) genuine_virtio_blk=true"
fi
if [[ "$f18" == 1 ]]; then
  : "${RV32I_QEMU_P2_NVME_IMAGE:?full18 requires real QEMU PCI NVMe namespace disk}"
  nvme_disk=$(realpath "$RV32I_QEMU_P2_NVME_IMAGE")
  [[ -s "$nvme_disk" ]] || { echo "RV32I_QEMU_F18=FAIL missing_nvme_disk=$nvme_disk" >&2; exit 3; }
  qemu_args+=( -device virtio-gpu-pci
               -drive "if=none,id=rv32p2nvmedisk,file=$nvme_disk,format=raw,snapshot=on"
               -device nvme,id=rv32p2nvme,serial=MINICP2
               -device nvme-ns,drive=rv32p2nvmedisk,bus=rv32p2nvme )
  echo "RV32I_QEMU_F18_DEVICES=STAGED virtio_gpu_pci=true nvme_pci=true nvme_disk_sha256=$(sha256sum "$nvme_disk" | cut -d' ' -f1)"
fi
set +e
if [[ "$shell" == 1 ]]; then
  # Like RV64 shell oracle, feed genuine commands over ttyS0. But RV32
  # kernel boot can vary from 6 to >11 guest seconds, so a guessed fixed
  # sleep races PID1, truncates early UART input and yields false failures.
  # Wait for a REAL userspace-ready marker in the serial log, then submit.
  (
    ready=0
    for attempt in $(seq 1 160); do
      if grep -Fq 'MINIC_LINUX_RUNTIME_SH_READY' "$work/qemu-serial.log" 2>/dev/null; then
        ready=1; break
      fi
      sleep 0.25
    done
    [[ "$ready" == 1 ]] || {
      echo 'RV32I_QEMU_SHELL=FAIL no_real_userspace_ready_before_commands' >&2
      exit 77
    }
    sleep 0.5
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
    "$(if [[ "$shell" == 1 ]]; then printf '%s' '^MINIC_LINUX_RUNTIME_SH_READY'; elif [[ "$fast" == 1 ]]; then printf '%s' '^MINIC_LINUX_RUNTIME_INIT_BEGIN'; elif [[ "$p2" == 1 ]]; then printf '%s' '^RV32I_P2_BEGIN'; elif [[ "$p1" == 1 ]]; then printf '%s' '^RUNTIME_V2_SYSCALL_BEGIN'; else printf '%s' '^RV32I_LINUX_PID1=PASS syscall=getpid,write elf=ELF32 abi=ilp32'; fi)"; do
  if ! grep -Eq "$pattern" "$work/qemu-serial.log"; then
    echo "RV32I_QEMU_P0=FAIL missing=$pattern image=$image_sha" >&2
    tail -n 140 "$work/qemu-serial.log" >&2
    exit 1
  fi
done
if grep -Eiq '(Kernel panic|RV32I_LINUX_PID1=FAIL|RV32I_LINUX_P1=FAIL|RV32I_P2_FAIL|RV32I_P2_FRONTIER=FAIL|RUNTIME_V2_SYSCALL_FAIL|Oops:|Unable to handle kernel|Insufficient stack space|(^|[^a-z])BUG:|soft lockup|workqueue lockup|Unknown symbol|invalid module format)' \
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
    # Serial TTY echoes INPUT. Do not mistake echoed input commands for
    # executed shell output: require the actual command-result lines.
    tr -d '\r' <"$work/qemu-serial.log" >"$work/qemu-shell-normalized.log"
    grep -Eq '^console=ttyS0 .*rdinit=/bin/sh$' "$work/qemu-shell-normalized.log"
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
  if [[ "$p2" == 1 ]]; then
    # This is a SCOPED nine-operation P2 frontier, not the complete 18-row
    # historical P2 or 145/203 P3 oracle. Every listed function is REQUIRED:
    # missing kernel capabilities are UNAVAILABLE and fail this gate.
    # Match whole normalized lines exactly once; a prefix or duplicated PASS
    # marker cannot certify a missing or unexecuted operation.
    tr -d '\r' <"$work/qemu-serial.log" >"$work/qemu-p2-normalized.log"
    for marker in 'INIT_PROC_MOUNT=PASS' 'INIT_SYSFS_MOUNT=PASS' \
      'INIT_DEVTMPFS_MOUNT=PASS' 'INIT_CMDLINE_READ=PASS' \
      'INIT_TMPFS_RW=PASS' 'RV32I_P2_CHILD=PASS' \
      'RV32I_P2_MODULE_PASS libsha256' \
      'RV32I_P2_MODULE_PASS sha256_generic' \
      'RV32I_P2_MODULE_PASS sha512_generic' \
      'RV32I_P2_MODULE_PASS sha3_generic' \
      'RV32I_P2_MODULE_PASS blake2b_generic' \
      'RV32I_P2_MODULE_PASS overlay' \
      'RV32I_P2_PASS tmpfs-rename-rw' \
      'RV32I_P2_PASS ipv4-udp-loopback' \
      'RV32I_P2_PASS ipv4-tcp-loopback' \
      'RV32I_P2_PASS ipv6-tcp-loopback' \
      'RV32I_P2_PASS afalg-sha256-abc' \
      'RV32I_P2_PASS afalg-sha512-abc' \
      'RV32I_P2_PASS afalg-sha3-256-abc' \
      'RV32I_P2_PASS afalg-blake2b-512-abc' \
      'RV32I_P2_PASS overlay-copy-up' \
      'RV32I_P2_FRONTIER=PASS core=4 optional=5 unavailable=0' \
      'MINIC_LINUX_RUNTIME_INIT_END fail=0'; do
      seen=$(grep -Fxc "$marker" "$work/qemu-p2-normalized.log" || true)
      [[ "$seen" == 1 ]] || {
        echo "RV32I_QEMU_P2=FAIL marker_occurrences=$seen expected=1 marker=$marker" >&2
        tail -n 120 "$work/qemu-serial.log" >&2
        exit 1
      }
    done
    if grep -Eq '^RV32I_P2_(UNAVAILABLE|FAIL|MODULE_FAIL)' "$work/qemu-p2-normalized.log" ||
       [[ "$(grep -Ec '^RV32I_P2_PASS ' "$work/qemu-p2-normalized.log")" != 9 ]] ||
       [[ "$(grep -Ec '^RV32I_P2_MODULE_PASS ' "$work/qemu-p2-normalized.log")" != 6 ]]; then
      echo 'RV32I_QEMU_P2=FAIL negative_or_wrong_number_of_distinct_function_results' >&2
      exit 1
    fi
    if [[ "$c11" == 1 ]]; then
      for marker in 'RV32I_P2_C11_CHILD=PASS' \
        'RV32I_P2_C11_MODULE_PASS x_tables' \
        'RV32I_P2_C11_MODULE_PASS xt_tcpudp' \
        'RV32I_P2_C11_MODULE_PASS ip_tables' \
        'RV32I_P2_C11_MODULE_PASS iptable_filter' \
        'RV32I_P2_C11_MODULE_PASS ip6_tables' \
        'RV32I_P2_C11_MODULE_PASS ip6table_filter' \
        'RV32I_P2_C11_MODULE_PASS libcrc32c' \
        'RV32I_P2_C11_MODULE_PASS xor' \
        'RV32I_P2_C11_MODULE_PASS raid6_pq' \
        'RV32I_P2_C11_MODULE_PASS zlib_deflate' \
        'RV32I_P2_C11_MODULE_PASS lzo_compress' \
        'RV32I_P2_C11_MODULE_PASS btrfs' \
        'RV32I_P2_C11_MODULE_PASS dm-mod' \
        'RV32I_P2_C11_PASS rootfs-rw' \
        'RV32I_P2_C11_PASS iptables4' \
        'RV32I_P2_C11_PASS iptables6' \
        'RV32I_P2_C11_PASS btrfs' \
        'RV32I_P2_C11_PASS dm' \
        'RUNTIME_V2_ROOTFS_RW=PASS' \
        'RV32I_P2_C11_FRONTIER=PASS old_canonical=6 new_canonical=5 canonical=11/18'; do
        seen=$(grep -Fxc "$marker" "$work/qemu-p2-normalized.log" || true)
        [[ "$seen" == 1 ]] || {
          echo "RV32I_QEMU_C11=FAIL marker_occurrences=$seen expected=1 marker=$marker" >&2
          tail -n 120 "$work/qemu-serial.log" >&2
          exit 1
        }
      done
      if grep -Eq '^RV32I_P2_C11_(FAIL|MODULE_FAIL|UNAVAILABLE)' "$work/qemu-p2-normalized.log" ||
         [[ "$(grep -Ec '^RV32I_P2_C11_PASS ' "$work/qemu-p2-normalized.log")" != 5 ]] ||
         [[ "$(grep -Ec '^RV32I_P2_C11_MODULE_PASS ' "$work/qemu-p2-normalized.log")" != 13 ]]; then
        echo 'RV32I_QEMU_C11=FAIL missing_duplicate_or_negative_canonical_function_or_module' >&2
        exit 1
      fi
    fi
    if [[ "$f18" == 1 ]]; then
      for marker in 'INIT_RUNTIME_PROBE=PASS' \
        'RUNTIME_V2_SYSCALL_END pass=12 fail=0' \
        'RV32I_P2_F18_CHILD=PASS' \
        'RV32I_P2_F18_MODULE_PASS nf_defrag_ipv4' \
        'RV32I_P2_F18_MODULE_PASS nf_defrag_ipv6' \
        'RV32I_P2_F18_MODULE_PASS nf_conntrack' \
        'RV32I_P2_F18_MODULE_PASS llc' \
        'RV32I_P2_F18_MODULE_PASS stp' \
        'RV32I_P2_F18_MODULE_PASS bridge' \
        'RV32I_P2_F18_MODULE_PASS br_netfilter' \
        'RV32I_P2_F18_MODULE_PASS veth' \
        'RV32I_P2_F18_MODULE_PASS kvm' \
        'RV32I_P2_F18_MODULE_PASS drm' \
        'RV32I_P2_F18_MODULE_PASS drm_kms_helper' \
        'RV32I_P2_F18_MODULE_PASS drm_shmem_helper' \
        'RV32I_P2_F18_MODULE_PASS virtio-gpu' \
        'RV32I_P2_F18_MODULE_PASS nvme-core' \
        'RV32I_P2_F18_MODULE_PASS nvme' \
        'RV32I_P2_F18_CONNTRACK_TRACE before=0 after=1' \
        'RV32I_P2_F18_PASS conntrack' \
        'RV32I_P2_F18_PASS bridge' \
        'RV32I_P2_F18_PASS br-netfilter' \
        'RV32I_P2_F18_PASS veth' \
        'RV32I_P2_F18_PASS kvm' \
        'RV32I_P2_F18_PASS virtio-gpu' \
        'RV32I_P2_F18_PASS nvme' \
        'RV32I_P2_F18_FRONTIER=PASS new_canonical=7 old_canonical=11 full=18/18 fail=0 skip=0' \
        'RUNTIME_V2_P2_FULL_END pass=18 fail=0 skip=0'; do
        seen=$(grep -Fxc "$marker" "$work/qemu-p2-normalized.log" || true)
        [[ "$seen" == 1 ]] || {
          echo "RV32I_QEMU_F18=FAIL marker_occurrences=$seen expected=1 marker=$marker" >&2
          tail -n 150 "$work/qemu-serial.log" >&2
          exit 1
        }
      done
      if grep -Eq '^RV32I_P2_F18_(FAIL|MODULE_FAIL|UNAVAILABLE)' "$work/qemu-p2-normalized.log" ||
         [[ "$(grep -Ec '^RV32I_P2_F18_PASS ' "$work/qemu-p2-normalized.log")" != 7 ]] ||
         [[ "$(grep -Ec '^RV32I_P2_F18_MODULE_PASS ' "$work/qemu-p2-normalized.log")" != 15 ]] ||
         [[ "$(grep -Ec '^RUNTIME_V2_SYSCALL_PASS ' "$work/qemu-p2-normalized.log")" != 12 ]]; then
        echo 'RV32I_QEMU_F18=FAIL missing_duplicate_or_negative_full18_function_or_module' >&2
        exit 1
      fi
      # Reuse the historical RV64 canonical P2 manifest and checker *verbatim*;
      # RV32 emits its own genuine ELF32 syscall outcomes for all 18 rows.
      python3 tests/external/linux/runtime_v2_log_check.py \
        --profile p2 --qemu-rc "$status" --log "$work/qemu-serial.log"
      echo "RV32I_QEMU_P2_FULL18=PASS image_sha256=$image_sha real_QEMU=true original_GNU_Kbuild=true minic_C=1741/1741 canonical_operations=18/18 unavailable=0 skip=0"
    else
      python3 tests/external/linux/runtime_v2_log_check.py \
        --profile p0 --log "$work/qemu-serial.log"
    fi
    if [[ "$f18" == 0 && "$c11" == 1 ]]; then
      echo "RV32I_QEMU_P2_CANONICAL11_FRONTIER=PASS image_sha256=$image_sha real_QEMU=true original_GNU_Kbuild=true minic_C=1741/1741 initial_p2_operations=9/9 canonical_operations=11/18 unavailable=0 scope=NOT_FULL_P2"
    elif [[ "$f18" == 0 ]]; then
      echo "RV32I_QEMU_P2_FRONTIER=PASS image_sha256=$image_sha real_QEMU=true original_GNU_Kbuild=true minic_C=1741/1741 p2_operations=9/9 unavailable=0 scope=NOT_FULL_P2"
    fi
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
