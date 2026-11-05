#!/usr/bin/env python3
"""Read script/GC admission from the single, named-field TargetProfile table.

GNU Linux replay workflows assert that every Linux target admits both modes.
The five-target boundary test instead reads those same authoritative fields
and tests either the positive or explicitly gated runtime path. Neither
consumer depends on the order or number of target descriptor fields.
"""

import re
import sys
from pathlib import Path


TARGETS = (
    ("rv32", "elf32lriscv"),
    ("rv64", "elf64lriscv"),
    ("x86_64", "elf_x86_64"),
    ("aarch64", "aarch64linux"),
    ("loongarch64", "elf64loongarch"),
)
TABLE_MARKER = "static const MiniLdTargetProfile minild_target_profiles[] = {"
ENTRY = re.compile(r"(?ms)^[ \t]*\{[ \t]*\n(.*?)^[ \t]*\}[ \t]*,?[ \t]*$")
FIELD = re.compile(r"\.([a-z_][a-z_0-9]*)[ \t]*=[ \t]*([^,\n]+)")


def parse_profiles(content):
    """Return exact C designated initializer values, without defaults."""
    _, marker, after = content.partition(TABLE_MARKER)
    if not marker:
        raise ValueError("missing_target_profile_table")
    end = re.search(r"(?m)^\};[ \t]*$", after)
    if end is None:
        raise ValueError("unterminated_target_profile_table")
    profiles = {}
    entries = ENTRY.findall(after[:end.start()])
    if not entries:
        raise ValueError("empty_target_profile_table")
    for entry in entries:
        fields = {}
        for field, raw in FIELD.findall(entry):
            if field in fields:
                raise ValueError(f"duplicate_profile_field:{field}")
            fields[field] = raw.strip()
        name = fields.get("emulation", "")
        if re.fullmatch(r'"[a-zA-Z0-9_]+"', name) is None:
            raise ValueError(f"invalid_profile_emulation:{name}")
        name = name[1:-1]
        if name in profiles:
            raise ValueError(f"duplicate_profile:{name}")
        profiles[name] = fields
    return profiles


def policy_rows(content):
    profiles = parse_profiles(content)
    rows = []
    for arch, emulation in TARGETS:
        profile = profiles.get(emulation)
        if profile is None:
            raise ValueError(f"missing_profile:{emulation}")
        page = profile.get("page_size", "")
        if re.fullmatch(r"[0-9]+U", page) is None:
            raise ValueError(f"invalid_page_size:{emulation}")
        page_size = int(page[:-1])
        if page_size < 4096 or (page_size & (page_size - 1)) != 0:
            raise ValueError(f"invalid_page_size:{emulation}")
        for field in ("supports_relocatable", "supports_static"):
            if profile.get(field) != "true":
                raise ValueError(f"unsupported_target_mode:{emulation}:{field}")
        script = profile.get("supports_static_script", "false")
        gc = profile.get("supports_static_gc", "false")
        if script not in ("true", "false") or gc not in ("true", "false"):
            raise ValueError(f"invalid_script_gc_policy:{emulation}")
        if script != gc:
            raise ValueError(f"split_script_gc_policy:{emulation}")
        rows.append((arch, emulation, script))
    return rows


def main():
    if len(sys.argv) == 4 and sys.argv[1] == "--emit-gates":
        source, destination = sys.argv[2:]
        prefix = "MINILD_SCRIPT_GC_5ARCH"
        emit = True
    elif len(sys.argv) == 3:
        source, prefix = sys.argv[1:]
        emit = False
    else:
        raise SystemExit(
            "usage: minild-check-target-policies.py [--emit-gates] SOURCE PREFIX_OR_GATES"
        )
    try:
        rows = policy_rows(Path(source).read_text(encoding="utf-8"))
        if emit:
            Path(destination).write_text(
                "".join(f"{arch} {emulation} {policy}\n" for arch, emulation, policy in rows),
                encoding="utf-8",
            )
            return
        for _, emulation, policy in rows:
            if policy != "true":
                raise ValueError(f"unsupported_script_gc:{emulation}")
    except (OSError, ValueError) as exc:
        raise SystemExit(f"{prefix}=FAIL {exc}") from exc
    print(f"{prefix}=POLICY_PASS targets={len(rows)} "
          "static_script_gc=true production_policy_unchanged=true")


if __name__ == "__main__":
    main()
