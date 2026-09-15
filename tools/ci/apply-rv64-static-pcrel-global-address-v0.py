#!/usr/bin/env python3
from pathlib import Path

p = Path("src/target/riscv64/core_codegen.c")
text = p.read_text()
marker = "M196_LINUX_STATIC_PCREL_GLOBAL_ADDRESS"
if marker in text:
    print("MINIC_RV64_STATIC_PCREL_GLOBAL_ADDRESS_V0=ALREADY")
    raise SystemExit(0)

case_marker = "    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:"
next_marker = "    case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS:"
regions = []
pos = 0
while True:
    start = text.find(case_marker, pos)
    if start < 0:
        break
    end = text.find(next_marker, start)
    if end < 0:
        raise SystemExit("rv64 pcrel global: global-address case has no following function-address case")
    region = text[start:end]
    if "fprintf(file" in region:
        regions.append((start, end, region))
    pos = start + len(case_marker)

print(f"MINIC_RV64_STATIC_PCREL_GLOBAL_ADDRESS_V0_EMITTERS={len(regions)}")
if len(regions) != 1:
    raise SystemExit(f"rv64 pcrel global: expected one emitter case, found {len(regions)}")
start, end, region = regions[0]
old_prefix = '"  la t0, %s'
new_prefix = '"  lla t0, %s'
count = region.count(old_prefix)
print(f"MINIC_RV64_STATIC_PCREL_GLOBAL_ADDRESS_V0_MATCHES={count}")
if count != 1:
    raise SystemExit(f"rv64 pcrel global: expected one la prefix in emitter, found {count}")
region = region.replace(
    case_marker,
    "    /* M196_LINUX_STATIC_PCREL_GLOBAL_ADDRESS: the Linux kernel is statically\n"
    "       linked and executes early global accesses before the MMU is enabled.\n"
    "       Use the explicit PC-relative pseudo-op so GAS cannot choose GOT form. */\n"
    + case_marker,
    1,
)
region = region.replace(old_prefix, new_prefix, 1)
p.write_text(text[:start] + region + text[end:])
print("MINIC_RV64_STATIC_PCREL_GLOBAL_ADDRESS_V0=APPLIED")
