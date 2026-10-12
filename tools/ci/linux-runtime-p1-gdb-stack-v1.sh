#!/usr/bin/env bash
# Reproduce the frozen P1 kernel Image under QEMU's RISC-V remote debugger.
# All symbols are resolved against the SAME linked vmlinux, no guessed PC offset.
# Observational: never modifies the Linux Image, MiniC objects or linker state.
set -Eeuo pipefail
if (( $# != 6 )); then
  echo "usage: $0 <Image> <vmlinux> <initramfs> <dtb> <out-dir> <qemu-seconds>" >&2
  exit 64
fi
image=$1
vmlinux=$2
initramfs=$3
dtb=$4
dest=$5
seconds=$6
mkdir -p "$dest"
for file in "$image" "$vmlinux" "$initramfs" "$dtb"; do
  [[ -s "$file" ]] || { echo "P1_GDB=ERROR missing=$file"; exit 8; }
done
command -v gdb-multiarch >/dev/null
command -v qemu-system-riscv64 >/dev/null
command -v riscv64-linux-gnu-nm >/dev/null
sha256sum "$image" "$vmlinux" >"$dest/input-sha256.txt"
# Unique runner-local port, all connections loopback-only.
port=$(python3 - <<'PY_PORT'
import socket
s=socket.socket()
s.bind(("127.0.0.1",0))
print(s.getsockname()[1])
s.close()
PY_PORT
)
declare -a symbols=(
  tcp_v6_connect ip6_xmit ip6_finish_output2 ip6_output
  __dev_queue_xmit __tcp_transmit_skb
  handle_bad_stack
)
{
  printf '%s\n' 'set confirm off' 'set pagination off' 'set height 0'
  printf '%s\n' 'set breakpoint pending on' 'set print pretty off'
  printf '%s\n' 'set architecture riscv:rv64'
  printf 'file %s\n' "$vmlinux"
  printf 'target remote 127.0.0.1:%s\n' "$port"
  for symbol in "${symbols[@]}"; do
    # Do NOT rely on symbol names guessed from host nm: use the linked
    # guest kernel's own ELF; use an exact symbol address, no offset heuristics.
    addr=$(riscv64-linux-gnu-nm -n "$vmlinux" | awk -v name="$symbol" '$3 == name && ($2 == "T" || $2 == "t") {print $1;exit}')
    [[ -n "$addr" ]] || {
      printf 'P1_GDB_SYMBOL=MISSING name=%s\n' "$symbol" >&2
      continue
    }
    printf 'hbreak *0x%s\n' "$addr"
    printf '%s\n' 'commands'
    printf '%s\n' 'silent'
    printf 'printf "P1_GDB_FRAME name=%s pc=%%#lx sp=%%#lx ra=%%#lx sp_page_offset=%%#lx\\n", (unsigned long)$pc, (unsigned long)$sp, (unsigned long)$ra, ((unsigned long)$sp & 16383)\n' "$symbol"
    printf '%s\n' 'continue' 'end'
    printf 'P1_GDB_SYMBOL=BOUND name=%s addr=0x%s\n' "$symbol" "$addr" >&2
  done
  printf '%s\n' 'continue'
} >"$dest/commands.gdb"
printf 'P1_GDB=START image_sha=%s port=%s duration_s=%s\n' \
  "$(sha256sum "$image" | cut -d' ' -f1)" "$port" "$seconds"
qemu-system-riscv64 -M virt -cpu max -m 2G -smp 1 -nographic -no-reboot \
  -bios default -kernel "$image" -dtb "$dtb" \
  -device "loader,file=$initramfs,addr=0xa0000000,force-raw=on" \
  -append 'console=ttyS0 earlycon=sbi loglevel=8 panic=-1 rdinit=/init minic_runtime=p1' \
  -S -gdb "tcp:127.0.0.1:$port" \
  </dev/null >"$dest/qemu-serial.log" 2>&1 &
qpid=$!
cleanup() {
  kill "$qpid" >/dev/null 2>&1 || :
  wait "$qpid" >/dev/null 2>&1 || :
}
trap cleanup EXIT
# A bounded guest run is essential because a crashing kernel intentionally
# spins with panic=-1, and QEMU may remain paused at a debugger breakpoint.
sleep 2
rc=0
timeout --signal=TERM "${seconds}s" gdb-multiarch -nx -q -batch -x "$dest/commands.gdb" \
  >"$dest/gdb.log" 2>&1 || rc=$?
echo "P1_GDB=DONE debugger_rc=$rc image_sha=$(sha256sum "$image" | cut -d' ' -f1)"
grep -E 'P1_GDB_FRAME' "$dest/gdb.log" | tail -n 150 || :
grep -E 'Insufficient stack space|Kernel panic|BUG:|Oops:' "$dest/qemu-serial.log" | tail -n 15 || :
# Non-zero from timeout is expected; absence of frame hits is INCONCLUSIVE.
if ! grep -Eq 'P1_GDB_FRAME name=(ip6_xmit|ip6_finish_output2|tcp_v6_connect|handle_bad_stack) ' "$dest/gdb.log"; then
  echo "P1_GDB=INCONCLUSIVE no_ipv6_or_crash_frame_hits"
  exit 8
fi
echo "P1_GDB=CAPTURED diagnostic_only=true same_image=true"
