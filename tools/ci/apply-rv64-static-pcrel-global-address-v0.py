#!/usr/bin/env python3
from pathlib import Path

p = Path("src/target/riscv64/core_codegen.c")
text = p.read_text()
marker = "M196_LINUX_STATIC_PCREL_GLOBAL_ADDRESS"
if marker in text:
    print("MINIC_RV64_STATIC_PCREL_GLOBAL_ADDRESS_V0=ALREADY")
    raise SystemExit(0)

start = text.find("    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:")
end = text.find("    case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS:", start)
if start < 0 or end < 0:
    raise SystemExit("rv64 pcrel global: cannot locate global-address case")
region = text[start:end]
old = 'fprintf(file, "  la t0, %s\\n", function->globals[instruction->value.global_id].name)'
if region.count(old) != 1:
    raise SystemExit(f"rv64 pcrel global: expected one la emitter, found {region.count(old)}")
region = region.replace(
    "    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:",
    "    /* M196_LINUX_STATIC_PCREL_GLOBAL_ADDRESS: the Linux kernel is statically\n"
    "       linked and executes early global accesses before the MMU is enabled.\n"
    "       Use the explicit PC-relative pseudo-op so GAS cannot choose GOT form. */\n"
    "    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:",
    1,
)
region = region.replace(old, 'fprintf(file, "  lla t0, %s\\n", function->globals[instruction->value.global_id].name)', 1)
p.write_text(text[:start] + region + text[end:])
print("MINIC_RV64_STATIC_PCREL_GLOBAL_ADDRESS_V0=APPLIED")
