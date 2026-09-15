#!/usr/bin/env python3
from pathlib import Path

p = Path("src/target/riscv64/core_codegen.c")
text = p.read_text()
marker = "M196_LINUX_STATIC_PCREL_GLOBAL_ADDRESS"
function_marker = "M197_LINUX_STATIC_PCREL_FUNCTION_ADDRESS"

if marker in text and function_marker in text:
    print("MINIC_RV64_STATIC_PCREL_GLOBAL_ADDRESS_V0=ALREADY")
    print("MINIC_RV64_STATIC_PCREL_FUNCTION_ADDRESS_V0=ALREADY")
else:
    # Patch the unique assembly-emitting GLOBAL_ADDRESS case.  The verifier has
    # a case with the same enum value, so require fprintf(file) to distinguish
    # the emitter instead of relying on the first textual match.
    global_case = "    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:"
    function_case = "    case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS:"
    block_case = "    case MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS:"

    global_regions = []
    pos = 0
    while True:
        start = text.find(global_case, pos)
        if start < 0:
            break
        end = text.find(function_case, start)
        if end < 0:
            raise SystemExit("rv64 pcrel global: global-address case has no following function-address case")
        region = text[start:end]
        if "fprintf(file" in region:
            global_regions.append((start, end, region))
        pos = start + len(global_case)

    print(f"MINIC_RV64_STATIC_PCREL_GLOBAL_ADDRESS_V0_EMITTERS={len(global_regions)}")
    if len(global_regions) != 1:
        raise SystemExit(f"rv64 pcrel global: expected one emitter case, found {len(global_regions)}")
    start, end, region = global_regions[0]
    old_prefix = '"  la t0, %s'
    new_prefix = '"  lla t0, %s'
    count = region.count(old_prefix)
    print(f"MINIC_RV64_STATIC_PCREL_GLOBAL_ADDRESS_V0_MATCHES={count}")
    if count != 1:
        raise SystemExit(f"rv64 pcrel global: expected one la prefix in emitter, found {count}")
    region = region.replace(
        global_case,
        "    /* M196_LINUX_STATIC_PCREL_GLOBAL_ADDRESS: the Linux kernel is statically\n"
        "       linked and executes early global accesses before the MMU is enabled.\n"
        "       Use the explicit PC-relative pseudo-op so GAS cannot choose GOT form. */\n"
        + global_case,
        1,
    )
    region = region.replace(old_prefix, new_prefix, 1)
    text = text[:start] + region + text[end:]

    # Function designators are equally important before relocation.  In
    # set_satp_mode(), Linux takes the address of set_satp_mode itself to build
    # a temporary 1:1 page-table mapping.  A GOT/high-half function address
    # therefore makes the very next fetch fault as soon as SATP is written.
    function_regions = []
    pos = 0
    while True:
        start = text.find(function_case, pos)
        if start < 0:
            break
        # The emitter's FUNCTION_ADDRESS case is followed later by another case.
        # Bound it at the next switch case and again require fprintf(file).
        next_case = text.find("    case MINIC_CORE_INSTRUCTION_", start + len(function_case))
        if next_case < 0:
            raise SystemExit("rv64 pcrel function: function-address case has no following instruction case")
        region = text[start:next_case]
        if "fprintf(file" in region:
            function_regions.append((start, next_case, region))
        pos = start + len(function_case)

    print(f"MINIC_RV64_STATIC_PCREL_FUNCTION_ADDRESS_V0_EMITTERS={len(function_regions)}")
    if len(function_regions) != 1:
        raise SystemExit(f"rv64 pcrel function: expected one emitter case, found {len(function_regions)}")
    start, end, region = function_regions[0]
    count = region.count(old_prefix)
    print(f"MINIC_RV64_STATIC_PCREL_FUNCTION_ADDRESS_V0_MATCHES={count}")
    if count != 1:
        raise SystemExit(f"rv64 pcrel function: expected one la prefix in emitter, found {count}")
    region = region.replace(
        function_case,
        "    /* M197_LINUX_STATIC_PCREL_FUNCTION_ADDRESS: early static-kernel code may\n"
        "       take function addresses while MMU-off (for example set_satp_mode).\n"
        "       Keep those addresses PC-relative rather than GOT/high-half based. */\n"
        + function_case,
        1,
    )
    region = region.replace(old_prefix, new_prefix, 1)
    text = text[:start] + region + text[end:]

    p.write_text(text)
    print("MINIC_RV64_STATIC_PCREL_GLOBAL_ADDRESS_V0=APPLIED")
    print("MINIC_RV64_STATIC_PCREL_FUNCTION_ADDRESS_V0=APPLIED")
