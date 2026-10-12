#!/usr/bin/env bash
# Read-only P1 stack-boundary diagnosis on the EXACT certified failing Image.
# Record the CPU0 per-CPU IRQ-stack allocation independently of current->stack.
# Breakpoints are armed only after tcp_v6_connect to avoid boot-time storms.
# GDB changes execution timing; this is never a P1 PASS certificate.
# Evidence contract: docs/ci/linux-p1-task-irq-stack-diagnosis-2026-10-12.md
set -Eeuo pipefail
if (( $# != 6 )); then
  echo "usage: $0 <Image> <vmlinux> <initramfs> <dtb> <out-dir> <qemu-seconds>" >&2
  exit 64
fi
image=$1; vmlinux=$2; initramfs=$3; dtb=$4; dest=$5; seconds=$6
mkdir -p "$dest"
for file in "$image" "$vmlinux" "$initramfs" "$dtb"; do
  [[ -s "$file" ]] || { echo "P1_GDB=ERROR missing=$file"; exit 8; }
done
for tool in gdb-multiarch qemu-system-riscv64 riscv64-linux-gnu-nm python3; do
  command -v "$tool" >/dev/null
done
sha256sum "$image" "$vmlinux" >"$dest/input-sha256.txt"
riscv64-linux-gnu-nm -n --defined-only "$vmlinux" >"$dest/linked-symbols.nm"
port=$(python3 - <<'PY_PORT'
import socket
with socket.socket() as s:
    s.bind(("127.0.0.1", 0))
    print(s.getsockname()[1])
PY_PORT
)
python3 - "$dest/linked-symbols.nm" "$dest/commands.gdb" "$dest/symbols.txt" "$vmlinux" "$port" "$dest" <<'PY_GDB'
from pathlib import Path
import sys

nm, commands_path, symbols_path, vmlinux, port, evidence_dir = sys.argv[1:]
symbols = {}
for line in Path(nm).read_text(errors="replace").splitlines():
    parts = line.split()
    if len(parts) == 3 and len(parts[1]) == 1:
        try:
            symbols.setdefault(parts[2], (int(parts[0], 16), parts[1]))
        except ValueError:
            pass
# Required data addresses must be read through CPU0's per-CPU offset, not
# treated as ordinary absolute .data objects. QEMU runs with -smp 1.
required = ("tcp_v6_connect", "irq_stack_ptr", "__per_cpu_offset")
missing = [name for name in required if name not in symbols]
if missing:
    raise SystemExit("P1_GDB=INCONCLUSIVE missing_linked_symbols=" + ",".join(missing))
targets = (
    "tcp_v6_connect", "ip6_xmit", "ip6_finish_output2",
    "do_irq", "handle_riscv_irq", "riscv_timer_interrupt",
    "hrtimer_interrupt", "rb_erase", "__rb_change_child",
    "__list_add_valid_or_report", "handle_bad_stack",
)
bound = [(name, symbols[name][0]) for name in targets if name in symbols]
Path(symbols_path).write_text(
    "".join(f"{name}\t0x{addr:x}\n" for name, addr in bound)
)
lines = [
    "set confirm off", "set pagination off", "set height 0",
    "set print pretty off", "set architecture riscv:rv64",
    f"file {vmlinux}", f"target remote 127.0.0.1:{port}",
]
def emit(line):
    lines.append(line)
for name, addr in bound:
    emit(f"hbreak *0x{addr:x}")
# Only TCP entry is armed until the P1 IPv6 connection reaches the kernel.
for index in range(2, len(bound) + 1):
    emit(f"disable {index}")
emit("commands 1")
emit("silent")
emit("set $p1_task_lo = ((unsigned long)$sp & ~(unsigned long)16383)")
irq_ptr = symbols["irq_stack_ptr"][0]
percpu_offset = symbols["__per_cpu_offset"][0]
emit(f"set $p1_percpu_offset = *(unsigned long*)0x{percpu_offset:x}")
emit(f"set $p1_irq_ptr_slot = (unsigned long)0x{irq_ptr:x} + $p1_percpu_offset")
emit("set $p1_irq_base = *(unsigned long*)$p1_irq_ptr_slot")
emit("set $p1_irq_top = $p1_irq_base + 16384")
emit('printf "P1_GDB_IRQ_LAYOUT irq_base=%#lx irq_top=%#lx irq_ptr_slot=%#lx percpu_offset=%#lx tcp_sp=%#lx task_lo=%#lx\\n", (unsigned long)$p1_irq_base, (unsigned long)$p1_irq_top, (unsigned long)$p1_irq_ptr_slot, (unsigned long)$p1_percpu_offset, (unsigned long)$sp, (unsigned long)$p1_task_lo')
emit("enable " + " ".join(map(str, range(2, len(bound) + 1))))
emit("disable 1")
emit("continue")
emit("end")
for idx, (name, _) in enumerate(bound[1:], 2):
    emit(f"commands {idx}")
    emit("silent")
    if name == "handle_bad_stack":
        # Handler SP is the EMERGENCY stack and cannot identify the
        # original faulting SP. The serial pt_regs print holds the latter.
        emit('printf "P1_GDB_OVERFLOW_HANDLER emergency_sp=%#lx irq_base=%#lx irq_top=%#lx task_lo=%#lx\\n", (unsigned long)$sp, (unsigned long)$p1_irq_base, (unsigned long)$p1_irq_top, (unsigned long)$p1_task_lo')
        # At handler entry a0 points to a raw pt_regs on the EMERGENCY stack.
        # Linux 6.6 RV64: epc=0 ra=8 sp=16 status=256 badaddr=264 cause=272.
        emit("set $p1_regs = (unsigned long)$a0")
        for reg, offset in (("epc", 0), ("ra", 8), ("sp", 16),
                            ("status", 256), ("badaddr", 264), ("cause", 272)):
            emit(f"set $p1_fault_{reg} = *(unsigned long*)($p1_regs + {offset})")
        emit('printf "P1_FIRSTFAULT epc=%#lx ra=%#lx sp=%#lx status=%#lx badaddr=%#lx cause=%#lx irq_base=%#lx irq_top=%#lx task_lo=%#lx\\n", (unsigned long)$p1_fault_epc, (unsigned long)$p1_fault_ra, (unsigned long)$p1_fault_sp, (unsigned long)$p1_fault_status, (unsigned long)$p1_fault_badaddr, (unsigned long)$p1_fault_cause, (unsigned long)$p1_irq_base, (unsigned long)$p1_irq_top, (unsigned long)$p1_task_lo')
        emit("bt 32")
        emit("x/36gx $p1_regs")
        emit(f"dump binary memory {evidence_dir}/irq-stack.bin $p1_irq_base $p1_irq_top")
        emit(f"dump binary memory {evidence_dir}/task-stack.bin $p1_task_lo ($p1_task_lo + 16384)")
        emit(f"disable {idx}")
    else:
        emit(f'printf "P1_GDB_FRAME name={name} pc=%#lx sp=%#lx ra=%#lx irq_remaining=%ld task_remaining=%ld\\n", (unsigned long)$pc, (unsigned long)$sp, (unsigned long)$ra, (long)((unsigned long)$sp-(unsigned long)$p1_irq_base), (long)((unsigned long)$sp-(unsigned long)$p1_task_lo)')
        # Keep the record of the first post-TCP transition to IRQ stack,
        # but avoid GDB spending its whole budget at frequent IRQ entry.
        if name in ("do_irq", "handle_riscv_irq"):
            emit(f"disable {idx}")
    emit("continue")
    emit("end")
emit("continue")
Path(commands_path).write_text("\n".join(lines) + "\n")
print("P1_GDB_SYMBOLS=BOUND count=" + str(len(bound)) +
      " irq_stack_ptr=0x%x __per_cpu_offset=0x%x" % (irq_ptr, percpu_offset))
PY_GDB
printf 'P1_GDB=START image_sha=%s port=%s duration_s=%s read_only=true\n' \
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
sleep 2
rc=0
timeout --signal=TERM "$seconds"s gdb-multiarch -nx -q -batch \
  -x "$dest/commands.gdb" >"$dest/gdb.log" 2>&1 || rc=$?
echo "P1_GDB=DONE debugger_rc=$rc image_sha=$(sha256sum "$image" | cut -d' ' -f1)"
grep -aE 'P1_(GDB_(IRQ_LAYOUT|FRAME|OVERFLOW_HANDLER)|FIRSTFAULT)' "$dest/gdb.log" | tail -n 100 || :
grep -aE 'Insufficient stack space|Kernel panic|BUG:|Oops:' "$dest/qemu-serial.log" | tail -n 10 || :
if ! grep -aFq 'P1_GDB_IRQ_LAYOUT irq_base=' "$dest/gdb.log"; then
  echo "P1_GDB=INCONCLUSIVE no_percpu_irq_layout_observed"
  exit 8
fi
if ! grep -aFq 'P1_GDB_FRAME name=' "$dest/gdb.log"; then
  echo "P1_GDB=INCONCLUSIVE no_post_tcp_trace"
  exit 8
fi
echo "P1_GDB=CAPTURED diagnostic_only=true same_image=true irq_stack_verified_from_percpu=true no_P1_pass_inference=true"
