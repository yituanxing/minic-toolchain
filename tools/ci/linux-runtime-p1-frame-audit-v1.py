#!/usr/bin/env python3
"""Locate real RV64 MiniC owners and frame prologues for P1 IPv6 stack overflow.

Consume the immutable 2064-object MiniC cache and the independent pure-GNU
objects from CI #38204602644. NO recompile, link, source rewrite or guessing
owner names from the inline-specialization symbol. Emit bounded evidence.
"""
from __future__ import annotations

import hashlib
import re
import subprocess
import sys
from pathlib import Path

if len(sys.argv) != 4:
    raise SystemExit("usage: linux-runtime-p1-frame-audit-v1.py MINIC_DIR GOLDEN_OUT OUTPUT_DIR")
mini, gold, dest = (Path(x) for x in sys.argv[1:])
dest.mkdir(parents=True, exist_ok=True)
names = sorted(p.relative_to(mini).as_posix() for p in mini.rglob("*.o"))
if len(names) != 2064:
    raise SystemExit(f"P1_FRAME_AUDIT=FAIL candidate_count={len(names)} expected=2064")
for name in names:
    if not (gold / name).is_file():
        raise SystemExit(f"P1_FRAME_AUDIT=FAIL missing_gnu_reference={name}")
print(f"P1_FRAME_AUDIT=PASS minic_candidates={len(names)} all_gnu_references_present=true", flush=True)

# Important: inline_spec_163_16339 is a generated TU-local symbol, NOT a
# source C name. Look up its EXACT owner(s) before blaming a TCP IPv6 TU.
wanted = (
    "__list_add_valid_or_report",
    "__minic_inline_spec_163_16339",
    "tcp_v6_connect",
    "__inet6_connect",
    "tcp_connect",
    "tcp_v6_syn_recv_sock",
    "tcp_v6_rcv",
)
records = []
for pool, directory in (("minic", mini), ("gnu", gold)):
    argv = ["riscv64-linux-gnu-nm", "-A", "-S", "--defined-only"] + [str(directory / x) for x in names]
    proc = subprocess.run(argv, capture_output=True, text=True)
    if proc.returncode:
        raise SystemExit(f"P1_FRAME_AUDIT=FAIL nm_{pool}_rc={proc.returncode} stderr={proc.stderr[-1000:]}")
    for line in proc.stdout.splitlines():
        cols = line.rsplit(None, 3)
        if len(cols) != 4:
            continue
        loc, size, kind, symbol = cols
        if symbol not in wanted:
            continue
        source, addr = loc.rsplit(":", 1)
        rel = Path(source).relative_to(directory).as_posix()
        try:
            size_num = int(size, 16)
            offset_num = int(addr, 16)
        except ValueError:
            continue
        records.append((symbol, pool, rel, offset_num, size_num, kind))
        print(
            f"P1_SYMBOL_OWNER symbol={symbol} pool={pool} object={rel} "
            f"size={size_num} type={kind}", flush=True
        )

lines = ["symbol\tpool\tobject\taddress\tbytes\ttype"]
for symbol, pool, rel, addr, size, kind in sorted(records):
    lines.append(f"{symbol}\t{pool}\t{rel}\t0x{addr:x}\t{size}\t{kind}")
(dest / "owners.tsv").write_text("\n".join(lines) + "\n")

for target in ("__list_add_valid_or_report", "__minic_inline_spec_163_16339"):
    matched = [x for x in records if x[0] == target and x[1] == "minic"]
    if not matched:
        raise SystemExit(f"P1_FRAME_AUDIT=FAIL missing_crash_symbol={target}")
    print(f"P1_SYMBOL_MATCH_COUNT symbol={target} minic_owners={len(matched)}", flush=True)

# Dump only the first instructions and every detected stack adjust for each
# crash site. Entire disassembly is retained as an artifact, not spooled to
# the Actions log.
disasm_rows = []
for symbol, pool, rel, _, _, _ in records:
    if symbol not in ("__list_add_valid_or_report", "__minic_inline_spec_163_16339", "tcp_v6_connect"):
        continue
    p = (mini if pool == "minic" else gold) / rel
    out = subprocess.run(["riscv64-linux-gnu-objdump", "-dr", str(p)], text=True, capture_output=True)
    if out.returncode:
        raise SystemExit(f"P1_FRAME_AUDIT=FAIL objdump_rc={out.returncode} object={rel}")
    label = re.compile(r"^[0-9a-f]+ <" + re.escape(symbol) + r">:")
    lines_asm = out.stdout.splitlines()
    starts = [i for i, line in enumerate(lines_asm) if label.match(line)]
    if not starts:
        print(f"P1_DISASM=INCONCLUSIVE missing={symbol} object={rel}", flush=True)
        continue
    start = starts[0]
    end = next((j for j in range(start + 1, len(lines_asm)) if
                re.match(r"^[0-9a-f]+ <[^>]+>:$", lines_asm[j])), len(lines_asm))
    insns = lines_asm[start:end]
    head = insns[:95]
    sp_uses = [x for x in insns if re.search(r"\b(?:addi|add|sub|sd|ld)\s+sp\s*,\s*sp\b|\bsp\s*,\s*-", x)]
    # "addi sp,sp,-N" is the common frame setup; other forms are shown
    # raw rather than pretending to have inferred the whole stack usage.
    prologue = [x for x in insns[:45] if re.search(r"(?:addi|add|sub)\s+sp\s*,\s*sp\b", x)]
    prefix = f"{pool}:{rel}:{symbol}"
    disasm_rows.append(f"===== {prefix} =====\n" + "\n".join(head) +
                       "\n[ALL SP ADJUSTMENTS]\n" + "\n".join(sp_uses[:90]) + "\n")
    print(f"P1_FRAME_PROLOGUE site={prefix} inst_count={len(insns)} "
          f"stack_adjust={repr(prologue[:5])}", flush=True)
(dest / "disassembly.txt").write_text("\n".join(disasm_rows))
print("P1_FRAME_AUDIT=COMPLETE runtime_oracle=NOT_RUN objects_recompiled=0 object_linked=0", flush=True)
