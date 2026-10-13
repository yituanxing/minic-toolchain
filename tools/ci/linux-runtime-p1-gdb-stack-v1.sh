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
import os
import sys

nm, commands_path, symbols_path, vmlinux, port, evidence_dir = sys.argv[1:]
sched_chain = os.environ.get('P1_GDB_SCHED_CHAIN') == '1'
irq_reentry = os.environ.get('P1_GDB_IRQ_REENTRY') == '1'
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
    "__tcp_transmit_skb", "ip6_output", "__neigh_create",
    "___neigh_create", "neigh_alloc", "__kmalloc",
    "__slab_alloc_node", "get_freepointer_safe",
    "__list_add_valid_or_report", "handle_bad_stack",
)
# Scheduler PELT entry instrumentation is opt-in. Capture caller RA and
# entry SP before callee prologue, not the faulting handler's emergency SP.
sched_targets = (
    "tick_sched_timer", "tick_sched_handle", "update_process_times",
    "scheduler_tick", "task_tick_fair", "entity_tick",
    "update_load_avg", "___update_load_avg", "___update_load_sum",
    "__accumulate_pelt_segments", "accumulate_sum", "decay_load",
    "__minic_inline_spec_1331_185", "__hrtimer_run_queues",
    "__run_hrtimer", "update_curr", "update_rq_clock",
    "enqueue_task_fair", "dequeue_task_fair", "__schedule",
)
if sched_chain:
    required_sched = ("accumulate_sum", "___update_load_sum", "decay_load")
    if any(x not in symbols for x in required_sched):
        raise SystemExit("P1_SCHED_CHAIN=INCONCLUSIVE missing_pelt_symbols")
    targets += tuple(name for name in sched_targets if name not in targets)
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
if sched_chain:
    emit("set $p1_sched_hits = 0")
if irq_reentry:
    emit("set $p1_low_irq_hits = 0")
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
        if sched_chain and name in sched_targets:
            # Only real IRQ-stack entries, with enough depth to matter.
            # Limit GDB logging overhead; do not imply all entries are nested.
            emit("if ($p1_sched_hits < 400 && $sp >= $p1_irq_base && "
                 "$sp < $p1_irq_base + 12288)")
            emit("set $p1_sched_hits = $p1_sched_hits + 1")
            emit(f'printf "P1_SCHED_FRAME index=%ld name={name} entry_sp=%#lx caller_ra=%#lx irq_remaining=%ld\\n", (long)$p1_sched_hits, (unsigned long)$sp, (unsigned long)$ra, (long)((unsigned long)$sp-(unsigned long)$p1_irq_base)')
            emit("end")
        # For frequently called slab/neighbor functions, only record
        # entries close to the stack bottom of the diagnosed task/IRQ.
        low_stack_target = name in ("__neigh_create", "___neigh_create",
            "neigh_alloc", "__kmalloc", "__slab_alloc_node",
            "get_freepointer_safe", "__list_add_valid_or_report")
        if low_stack_target:
            emit("if (($sp >= $p1_task_lo && $sp < $p1_task_lo + 8192) || "
                 "($sp >= $p1_irq_base && $sp < $p1_irq_base + 8192))")
        emit(f'printf "P1_GDB_FRAME name={name} pc=%#lx sp=%#lx ra=%#lx irq_remaining=%ld task_remaining=%ld\\n", (unsigned long)$pc, (unsigned long)$sp, (unsigned long)$ra, (long)((unsigned long)$sp-(unsigned long)$p1_irq_base), (long)((unsigned long)$sp-(unsigned long)$p1_task_lo)')
        if low_stack_target:
            emit("end")
        # The normal diagnostics disable these busy IRQ-entry breakpoints
        # after the first switch. Opt-in low IRQ probe must KEEP them to
        # distinguish a nested/reentrant timer IRQ from unrelated calls.
        if name in ("do_irq", "handle_riscv_irq"):
            if irq_reentry:
                emit("if ($p1_low_irq_hits < 80 && $sp >= $p1_irq_base && "
                     "$sp < $p1_irq_base + 12288)")
                emit("set $p1_low_irq_hits = $p1_low_irq_hits + 1")
                emit(f'printf "P1_IRQ_LOW_ENTRY name={name} sp=%#lx ra=%#lx irq_remaining=%ld\\n", (unsigned long)$sp, (unsigned long)$ra, (long)((unsigned long)$sp-(unsigned long)$p1_irq_base)')
                if name == "do_irq":
                    # do_irq(struct pt_regs *regs): a0 points to saved
                    # trap regs at function entry; RV64 pt_regs epc=+0,
                    # sp=+16. Saved PC tells which *actual* upstream
                    # function was interrupted before this nested IRQ.
                    # This is separate from do_irq's own return address
                    # (always _save_context+0x86).
                    emit("if ($a0 != 0 && (($a0 & 7) == 0))")
                    emit("set $p1_interrupt_epc = *(unsigned long*)$a0")
                    emit("set $p1_interrupt_sp = *(unsigned long*)($a0 + 16)")
                    emit('printf "P1_IRQ_INTERRUPTED epc=%#lx saved_sp=%#lx irq_sp=%#lx saved_irq_remaining=%ld\\n", (unsigned long)$p1_interrupt_epc, (unsigned long)$p1_interrupt_sp, (unsigned long)$sp, (long)((unsigned long)$p1_interrupt_sp-(unsigned long)$p1_irq_base)')
                    emit("end")
                emit("end")
            else:
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
if ! python3 "$PWD/tools/ci/linux-runtime-p1-stack-postmortem-v1.py" --dir "$dest"; then
  echo "P1_POSTMORTEM=INCONCLUSIVE parse_failed_not_runtime_certificate"
fi
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
if [[ "${P1_GDB_SCHED_CHAIN:-0}" == 1 ]]; then
  sched_samples=$(grep -ac 'P1_SCHED_FRAME index=' "$dest/gdb.log" || true)
  echo "P1_SCHED_CHAIN=CAPTURED samples=$sched_samples entry_sp_before_prologue=1 caller_ra=1 same_image_irq_base=1 gdb_perturbed=1"
  if [[ "${P1_GDB_IRQ_REENTRY:-0}" == 1 ]]; then
    low_entries=$(grep -ac 'P1_IRQ_LOW_ENTRY name=' "$dest/gdb.log" || true)
    echo "P1_IRQ_REENTRY=OBSERVED low_irq_entries=$low_entries same_image=1 timing_perturbed=1"
  fi
  [[ "$sched_samples" -gt 0 ]] || exit 8
  # Symbolize caller return sites and read emitted RV64 frame prologues
  # from the linked ELF, never from an inferred source-level stack size.
  python3 - "$dest/gdb.log" "$dest/linked-symbols.nm" "$vmlinux" "$dest/scheduler-chain.tsv" <<'PY_SCHED'
import bisect, pathlib, re, subprocess, sys
log, nm, elf, output = map(pathlib.Path, sys.argv[1:])
rx = re.compile(r"P1_SCHED_FRAME index=(\d+) name=(\S+) entry_sp=(0x[0-9a-f]+) caller_ra=(0x[0-9a-f]+) irq_remaining=(\d+)")
events = [m.groups() for line in log.read_text(errors="replace").splitlines()
          if (m := rx.search(line))]
by_name = {}
pairs = []
for line in nm.read_text(errors="replace").splitlines():
    col = line.split()
    if len(col) == 3 and col[1].upper() == "T" and not col[2].startswith(".L"):
        addr = int(col[0], 16)
        pairs.append((addr, col[2]))
        by_name.setdefault(col[2], addr)
pairs.sort()
addresses = [row[0] for row in pairs]
def caller(addr):
    i = bisect.bisect_right(addresses, addr-1)-1
    return f"{pairs[i][1]}+0x{addr-pairs[i][0]:x}" if i >= 0 else "unknown"
frames = {}
for name in set(e[1] for e in events):
    if name not in by_name:
        continue
    start = by_name[name]
    run = subprocess.run(
        ["riscv64-linux-gnu-objdump", "-d",
         "--start-address="+hex(start), "--stop-address="+hex(start+16),
         str(elf)], check=True, text=True, capture_output=True)
    m = re.search(r"\baddi\s+sp,\s*sp,\s*(-?(?:0x[0-9a-f]+|\d+))", run.stdout)
    frames[name] = max(0, -int(m.group(1), 0)) if m else -1
with output.open("w") as out:
    out.write("event\tfunction\tentry_sp\tirq_bytes_left\tframe_bytes\tafter_prologue_bytes_left\tcaller_ra\tcaller_function\n")
    for seq,name,sp,ra,budget in events:
        frame = frames.get(name,-1)
        remaining = int(budget)-frame if frame >= 0 else "unresolved"
        out.write(f"{seq}\t{name}\t{sp}\t{budget}\t{frame}\t{remaining}\t{ra}\t{caller(int(ra,16))}\n")
print(f"P1_SCHED_CHAIN_TIMELINE=PASS samples={len(events)} resolved_frames={sum(v>=0 for v in frames.values())} lowest_entry={min((int(e[4]) for e in events),default=-1)} gdb_timing_perturbed=1")
for e in sorted(events,key=lambda e:int(e[4]))[:12]:
    print(f"P1_SCHED_LOW_ENTRY name={e[1]} entry_left={e[4]} frame={frames.get(e[1],-1)} caller={caller(int(e[3],16))}")
PY_SCHED
  if [[ "${P1_GDB_IRQ_REENTRY:-0}" == 1 ]]; then
    python3 - "$dest/gdb.log" "$dest/linked-symbols.nm" "$dest/irq-interrupted.tsv" <<'PY_IRQ_PARENT'
from pathlib import Path
from bisect import bisect_right
import re
import sys
log, nm, report = map(Path, sys.argv[1:])
pairs = []
for line in nm.read_text(errors="replace").splitlines():
    cols = line.split()
    if len(cols) != 3 or cols[1].lower() not in ("t", "w"):
        continue
    try:
        pairs.append((int(cols[0], 16), cols[2]))
    except ValueError:
        pass
pairs.sort()
addresses = [x[0] for x in pairs]
def symbolize(pc):
    j = bisect_right(addresses, pc) - 1
    return "%s+0x%x" % (pairs[j][1], pc - pairs[j][0]) if j >= 0 else "UNKNOWN"
rx = re.compile(r"P1_IRQ_INTERRUPTED epc=(0x[0-9a-f]+) saved_sp=(0x[0-9a-f]+) irq_sp=(0x[0-9a-f]+) saved_irq_remaining=(-?\d+)")
records = [match.groups() for line in log.read_text(errors="replace").splitlines()
           if (match := rx.search(line))]
with report.open("w") as out:
    out.write("saved_epc\tinterrupted_function\tsaved_sp\tirq_entry_sp\tprevious_irq_stack_bytes_left\n")
    for pc, saved_sp, entry_sp, remain in records:
        out.write(f"{pc}\t{symbolize(int(pc,16))}\t{saved_sp}\t{entry_sp}\t{remain}\n")
print(f"P1_IRQ_PARENT_TRACE=CAPTURED interrupt_contexts={len(records)} read_only=1 same_image=1")
for pc, saved_sp, entry_sp, remain in records[:20]:
    print(f"P1_IRQ_INTERRUPTED_SYMBOLIZED function={symbolize(int(pc,16))} saved_sp={saved_sp} irq_sp={entry_sp} irq_left_before_entry={remain}")
PY_IRQ_PARENT
  fi
fi
echo "P1_GDB=CAPTURED diagnostic_only=true same_image=true irq_stack_verified_from_percpu=true no_P1_pass_inference=true"
