#!/usr/bin/env python3
"""Check checked-in MiniLD script/GC target policy without modifying source.

Used by all three GNU Linux replay/trial workflows. The MiniLdTargetProfile
initializer has three target-specific alignment scalars after image_base;
do not assume a 4096-byte page (AArch64 uses 65536 bytes here).
"""

import re
import sys
from pathlib import Path


EMULATIONS = (
    "elf32lriscv", "elf_x86_64", "aarch64linux", "elf64loongarch",
    "elf64lriscv",
)
INITIALIZER = re.compile(r'\{"([^"]+)",([^{}]*)\}', re.S)
SCALAR_FLAGS = re.compile(
    r'UINT64_C\([^)]*\),\s*\d+U,\s*\d+U,\s*\d+U,\s*'
    r'((?:(?:true|false),\s*){8})'
)


def inspect_profiles(content):
    profiles = {}
    for initializer in INITIALIZER.finditer(content):
        policy = SCALAR_FLAGS.search(initializer.group(2))
        if policy is None:
            continue
        name = initializer.group(1)
        if name in profiles:
            raise ValueError(f"duplicate_profile:{name}")
        profiles[name] = re.findall(r"true|false", policy.group(1))
    for emulation in EMULATIONS:
        policy = profiles.get(emulation)
        if policy is None:
            raise ValueError(f"missing_profile:{emulation}")
        # Field order: relocatable, static, static_script, static_gc.
        if policy[:4] != ["true"] * 4:
            raise ValueError(f"unsupported_script_gc:{emulation}")


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: minild-check-target-policies.py SOURCE PREFIX")
    source, prefix = sys.argv[1:]
    try:
        inspect_profiles(Path(source).read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise SystemExit(f"{prefix}=FAIL {exc}") from exc
    print(f"{prefix}=POLICY_PASS targets={len(EMULATIONS)} "
          "static_script_gc=true production_policy_unchanged=true")


if __name__ == "__main__":
    main()
