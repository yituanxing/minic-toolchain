#!/usr/bin/env bash
# Observational QEMU/GDB stack-entry trace on an IMMUTABLE, already-linked
# Linux 6.6.143 Image. Defer downstream breakpoints until tcp_v6_connect:
# this avoids the boot-time __list_add_valid_or_report breakpoint storm.
set -Eeuo pipefail
if (( $# != 6 )); then
  echo "usage: $0 <Image> <vmlinux> <initramfs> <dtb> <out-dir> <seconds>" >&2
  exit 64
fi
image=$1; vmlinux=$2; initramfs=$3; dtb=$4; dest=$5; seconds=$6
mkdir -p "$dest"
for item in "$image" "$vmlinux" "$initramfs" "$dtb"; do
  [[ -s "$item" ]] || { echo "P1_CALLTRACE=ERROR missing=$item"; exit 8; }
done
for tool in gdb-multiarch qemu-system-riscv64 riscv64-linux-gnu-nm; do
  command -v "$tool" >/dev/null
done
sha256sum "$image" "$vmlinux" >"$dest/inputs.sha256"
port=$(python3 - <<'PY_PORT'
import socket
with socket.socket() as s:
    s.bind(("127.0.0.1",0))
    print(s.getsockname()[1])
PY_PORT
)
# The TCP entry opens a single P1 observation window; non-TCP early-boot
# list_debug calls must not be traced. The symbol list is a target, not a
# claim that all these functions will be visited on this kernel config.
symbols=(
  tcp_v6_connect __tcp_transmit_skb ip6_xmit
  ip6_output ip6_finish_output2
  __ipv6_neigh_lookup_noref __neigh_create
  neigh_resolve_output neigh_hh_output
  neigh_direct_output __dev_queue_xmit
  __list_add_valid_or_report handle_bad_stack
)
printf '%s\n' 'set confirm off' 'set pagination off' 'set height 0' \
  'set print pretty off' 'set architecture riscv:rv64' \
  >"$dest/commands.gdb"
printf 'file %s\ntarget remote 127.0.0.1:%s\n' "$vmlinux" "$port" >>"$dest/commands.gdb"
declare -a installed=() names=()
for name in "${symbols[@]}"; do
  addr=$(riscv64-linux-gnu-nm -n "$vmlinux" | awk -v s="$name" '$3==s && ($2=="T" || $2=="t") {print $1;exit}')
  if [[ -z "$addr" ]]; then
    echo "P1_CALLTRACE_SYMBOL=ABSENT name=$name" >>"$dest/symbols.txt"
    continue
  fi
  printf 'hbreak *0x%s\n' "$addr" >>"$dest/commands.gdb"
  installed+=("${#installed[@]}")
  names+=("$name")
  printf 'P1_CALLTRACE_SYMBOL=BOUND name=%s address=0x%s\n' "$name" "$addr" >>"$dest/symbols.txt"
done
[[ "${#names[@]}" -gt 2 && "${names[0]}" == tcp_v6_connect ]] || {
  echo 'P1_CALLTRACE=ERROR missing_tcp_v6_connect_entry'; exit 8;
}
# GDB breakpoint numbers start at 1 (not array's zero-based indexes).
for ((idx=1;idx<${#names[@]};idx++)); do
  printf 'disable %s\n' "$((idx+1))" >>"$dest/commands.gdb"
done
# Keep original stack boundary based on the actual current task SP at
# tcp_v6_connect, not a magic address hard-coded from a previous boot.
{
  printf '%s\n' 'commands 1' 'silent'
  printf '%s\n' 'set $p1_stack_lo = ((unsigned long)$sp & ~(unsigned long)16383)'
  printf '%s\n' 'printf "P1_CALLTRACE_ENTRY name=tcp_v6_connect pc=%#lx sp=%#lx task_stack_lo=%#lx ra=%#lx\n", (unsigned long)$pc, (unsigned long)$sp, (unsigned long)$p1_stack_lo, (unsigned long)$ra'
  printf 'enable'
  for ((idx=1;idx<${#names[@]};idx++)); do
    printf ' %s' "$((idx+1))"
  done
  printf '\n'
  printf '%s\n' 'disable 1' 'continue' 'end'
} >>"$dest/commands.gdb"
for ((idx=1;idx<${#names[@]};idx++)); do
  name=${names[idx]}
  {
    printf 'commands %s\n' "$((idx+1))"
    printf '%s\n' 'silent'
    if [[ "$name" == __list_add_valid_or_report ]]; then
      # This function can execute thousands of times. Include only
      # critically low SP entries; still costs debugger breakpoints.
      printf '%s\n' 'if ((unsigned long)$sp >= $p1_stack_lo - 512 && (unsigned long)$sp < $p1_stack_lo + 4096)'
    fi
    if [[ "$name" == handle_bad_stack ]]; then
      # RISC-V enters the emergency overflow stack: its SP cannot be
      # interpreted relative to the original task's 16 KiB stack.
      printf '%s\\n' 'printf "P1_CALLTRACE_OVERFLOW_HANDLER pc=%#lx emergency_sp=%#lx original_task_stack_lo=%#lx\\n", (unsigned long)$pc, (unsigned long)$sp, (unsigned long)$p1_stack_lo'
    else
      printf 'printf "P1_CALLTRACE_FRAME name=%s pc=%%#lx sp=%%#lx remaining=%%ld ra=%%#lx\\n", (unsigned long)$pc, (unsigned long)$sp, (long)((unsigned long)$sp-(unsigned long)$p1_stack_lo), (unsigned long)$ra\\n' "$name"
    fi
    if [[ "$name" == __list_add_valid_or_report ]]; then printf '%s\n' 'end'; fi
    printf '%s\n' 'continue' 'end'
  } >>"$dest/commands.gdb"
done
printf '%s\n' 'continue' >>"$dest/commands.gdb"
qemu-system-riscv64 -M virt -cpu max -m 2G -smp 1 -nographic -no-reboot \
  -bios default -kernel "$image" -dtb "$dtb" \
  -device "loader,file=$initramfs,addr=0xa0000000,force-raw=on" \
  -append 'console=ttyS0 earlycon=sbi loglevel=8 panic=-1 rdinit=/init minic_runtime=p1' \
  -S -gdb "tcp:127.0.0.1:$port" </dev/null >"$dest/qemu-serial.log" 2>&1 &
qpid=$!
cleanup(){ kill "$qpid" >/dev/null 2>&1 || :; wait "$qpid" >/dev/null 2>&1 || :; }
trap cleanup EXIT
sleep 2
rc=0
timeout --signal=TERM "${seconds}s" gdb-multiarch -nx -q -batch \
  -x "$dest/commands.gdb" >"$dest/gdb.log" 2>&1 || rc=$?
python3 - "$dest/gdb.log" "$dest/summary.tsv" <<'PY_SUMMARY'
from collections import Counter
from pathlib import Path
import re,sys
text=Path(sys.argv[1]).read_text(errors="replace")
matches=re.findall(r"P1_CALLTRACE_FRAME name=([^ ]+) pc=([^ ]+) sp=([^ ]+) remaining=(-?\d+) ra=([^ ]+)",text)
with open(sys.argv[2],"w") as f:
    f.write("function\thits\tminimum_remaining_bytes\tlast_return_address\n")
    for name in dict.fromkeys(row[0] for row in matches):
        entries=[row for row in matches if row[0]==name]
        f.write(f"{name}\t{len(entries)}\t{min(int(x[3]) for x in entries)}\t{entries[-1][4]}\n")
print(f"P1_CALLTRACE_SUMMARY=CAPTURED rows={len(matches)} unique_functions={len({x[0] for x in matches})}")
PY_SUMMARY
# GDB observation itself is NOT a P1 runtime certificate. Interrupt
# timing is perturbed by breakpoints; even 3/3 normally PASSING Images
# can hit a kernel panic while the guest is debugged. Detect this.
if grep -aEqi 'Insufficient stack space to handle exception|Kernel panic|Oops:|Unable to handle' "$dest/qemu-serial.log"; then
  echo "P1_CALLTRACE_DEBUG_GUEST=CRASHED no_P1_pass_inference=true"
else
  echo "P1_CALLTRACE_DEBUG_GUEST=NO_CRASH_MARKER no_P1_pass_inference=true"
fi
echo "P1_CALLTRACE=RESULT debugger_rc=$rc image_sha=$(sha256sum "$image" | cut -d' ' -f1)"
grep -E 'P1_CALLTRACE_(ENTRY|FRAME|OVERFLOW_HANDLER)' "$dest/gdb.log" | tail -n 60 || :
grep -aE 'Insufficient stack space|Kernel panic|BUG:|Oops:' "$dest/qemu-serial.log" | tail -n 8 || :
if ! grep -Fq 'P1_CALLTRACE_ENTRY name=tcp_v6_connect' "$dest/gdb.log"; then
  echo "P1_CALLTRACE=INCONCLUSIVE TCP entry never reached"
  exit 8
fi
echo "P1_CALLTRACE=CAPTURED same_image=true observational=true"
