#!/usr/bin/env python3
"""Read-only, fail-closed ELF section-size diagnostics for all-C Linux links.

The size report is intentionally outside the build's acceptance gates: a
diagnostic failure must not conceal the original GNU linker exit code, and a
successful diagnostic must never certify a kernel that failed to link.
"""

from __future__ import annotations

import json
from pathlib import Path, PurePosixPath
import shutil
import struct
import subprocess
import tempfile


class Incomplete(ValueError):
    pass


def elf_sections(path: Path, machine: int) -> dict[str, int]:
    """Only section headers and names are read; huge debug data stays on disk."""
    with path.open("rb") as source:
        header = source.read(64)
        if (len(header) != 64 or header[:6] != b"\x7fELF\x02\x01" or
                struct.unpack_from("<H", header, 18)[0] != machine or
                struct.unpack_from("<H", header, 16)[0] not in (1, 2)):
            raise Incomplete(f"invalid target ELF64 object: {path}")
        offset, entry_size, count, names_index = (
            struct.unpack_from("<Q", header, 40)[0],
            struct.unpack_from("<H", header, 58)[0],
            struct.unpack_from("<H", header, 60)[0],
            struct.unpack_from("<H", header, 62)[0],
        )
        source.seek(0, 2)
        file_size = source.tell()
        if (entry_size < 64 or count == 0 or names_index >= count or
                offset > file_size or count * entry_size > file_size - offset):
            raise Incomplete(f"invalid ELF section-header table: {path}")
        source.seek(offset)
        headers = source.read(count * entry_size)
        strtab = headers[names_index * entry_size:(names_index + 1) * entry_size]
        str_offset = struct.unpack_from("<Q", strtab, 24)[0]
        str_size = struct.unpack_from("<Q", strtab, 32)[0]
        if str_offset > file_size or str_size > file_size - str_offset:
            raise Incomplete(f"invalid ELF section names: {path}")
        source.seek(str_offset)
        names = source.read(str_size)
    executable: dict[str, int] = {}
    for idx in range(count):
        entry = headers[idx * entry_size:(idx + 1) * entry_size]
        name_offset, _kind, flags, _address, _data_offset, size = (
            struct.unpack_from("<IIQQQQ", entry, 0))
        if flags & 0x6 != 0x6 or size == 0:
            continue
        if name_offset >= len(names):
            raise Incomplete(f"invalid executable section name: {path}")
        end = names.find(b"\0", name_offset)
        if end == -1:
            raise Incomplete(f"unterminated ELF section name: {path}")
        name = names[name_offset:end].decode("utf-8", errors="replace")
        executable[name] = executable.get(name, 0) + size
    return executable


def totals(sections: dict[str, int]) -> dict[str, int]:
    return {
        "executable_bytes": sum(sections.values()),
        "text_family_bytes": sum(size for name, size in sections.items()
                                 if name == ".text" or name.startswith(".text.")),
        "main_text_bytes": sections.get(".text", 0),
        "ref_text_bytes": sections.get(".ref.text", 0),
        "init_text_bytes": sections.get(".init.text", 0),
        "noinstr_text_bytes": sections.get(".noinstr.text", 0),
    }


def _sum_sizes(items: list[dict], side: str) -> dict[str, int]:
    keys = tuple(totals({}))
    return {key: sum(item[side][key] for item in items) for key in keys}


def capture_comparison(original: Path, staged: Path, rows: list[dict],
                       machine: int, diagnostic: Path) -> dict:
    """Compare the same frozen GNU C owners, never unrelated GNU ASM owners."""
    report: dict = {"schema": "minic-three-isa-c-section-diff-v1",
                    "verdict": "SECTION_DIAGNOSTIC_INCOMPLETE",
                    "owners_expected": len(rows)}
    try:
        if len(rows) == 0:
            raise Incomplete("no frozen GNU C owners")
        owners = []
        for row in rows:
            name = PurePosixPath(row["object"])
            if name.is_absolute() or ".." in name.parts or name.suffix != ".o":
                raise Incomplete(f"unsafe C owner: {name}")
            gnu_sections = elf_sections(original / name, machine)
            minic_sections = elf_sections(staged / name, machine)
            gnu, minic = totals(gnu_sections), totals(minic_sections)
            owners.append({"object": str(name), "gnu": gnu, "minic": minic,
                           "executable_delta_bytes":
                               minic["executable_bytes"] - gnu["executable_bytes"],
                           "text_delta_bytes":
                               minic["text_family_bytes"] - gnu["text_family_bytes"]})
        gnu, minic = _sum_sizes(owners, "gnu"), _sum_sizes(owners, "minic")
        report.update(verdict="SECTION_DIAGNOSTIC_COMPLETE",
                      owners_measured=len(owners), gnu_total=gnu, minic_total=minic,
                      executable_delta_bytes=(minic["executable_bytes"] -
                                              gnu["executable_bytes"]),
                      executable_size_ratio=(
                          round(minic["executable_bytes"] / gnu["executable_bytes"], 4)
                          if gnu["executable_bytes"] else None),
                      largest_executable_growth=sorted(
                          owners, key=lambda x: x["executable_delta_bytes"],
                          reverse=True)[:60],
                      largest_minic_executable=sorted(
                          owners, key=lambda x: x["minic"]["executable_bytes"],
                          reverse=True)[:60], owners=owners)
    except (OSError, Incomplete, KeyError, TypeError, struct.error) as exc:
        report["error"] = str(exc)
    diagnostic.write_text(json.dumps(report, sort_keys=True, indent=2) + "\n")
    return report


def capture_link_layout(out: Path, diagnostic: Path, target: str,
                        machine: int) -> dict:
    """Preserve any original GNU LD maps and surviving ELF section layout.

    Never rerun/modify Kbuild or add linker flags merely to create a map.
    The baseline's Kconfig may not have enabled CONFIG_VMLINUX_MAP.
    """
    diagnostic.mkdir(parents=True, exist_ok=True)
    result: dict = {"schema": "minic-three-isa-link-layout-v1",
                    "target": target,
                    "verdict": "SECTION_LAYOUT_DIAGNOSTIC_INCOMPLETE",
                    "gnu_ld_map": "NOT_EMITTED_BY_ORIGINAL_KBUILD",
                    "maps": [], "linker_script": None, "objects": {}}
    try:
        arch = {"x86_64": "x86", "aarch64": "arm64",
                "loongarch64": "loongarch"}[target]
        script = out / "arch" / arch / "kernel" / "vmlinux.lds"
        if script.is_file() and not script.is_symlink():
            shutil.copyfile(script, diagnostic / "gnu-vmlinux.lds")
            result["linker_script"] = script.relative_to(out).as_posix()
        for name in (".tmp_vmlinux.kallsyms1.map",
                     ".tmp_vmlinux.kallsyms2.map", "vmlinux.map"):
            source = out / name
            if source.is_file() and not source.is_symlink():
                # GitHub artifact upload ignores hidden files by default.
                # Keep map evidence public to the artifact while recording
                # the unchanged original source filename in the receipt.
                saved = "gnu-" + name.lstrip(".")
                shutil.copyfile(source, diagnostic / saved)
                result["maps"].append({"original": name, "saved": saved})
        if result["maps"]:
            result["gnu_ld_map"] = "PRESERVED_ORIGINAL_GNU_KBUILD_MAP"
        names = ("vmlinux.o", "arch/loongarch/kernel/head.o",
                 "arch/loongarch/kernel/smp.o") if target == "loongarch64" else ("vmlinux.o",)
        for name in names:
            obj = out / name
            if obj.is_file() and not obj.is_symlink():
                sections = elf_sections(obj, machine)
                result["objects"][name] = {"totals": totals(sections),
                                           "executable_sections": sections}
        if not result["maps"] and not result["objects"] and not result["linker_script"]:
            raise Incomplete("no original GNU map or surviving ELF objects")
        result["verdict"] = "SECTION_LAYOUT_DIAGNOSTIC_COMPLETE"
    except (OSError, Incomplete, struct.error) as exc:
        result["error"] = str(exc)
    (diagnostic / "section-layout.json").write_text(
        json.dumps(result, sort_keys=True, indent=2) + "\n")
    return result


def self_test() -> None:
    with tempfile.TemporaryDirectory() as location:
        work = Path(location)
        first, second = work / "gnu", work / "minic"
        first.mkdir()
        second.mkdir()
        source = work / "probe.c"
        source.write_text("int f(int x) { return x + 1; }\n")
        for flags, destination in ((["-O2"], first), (["-O0"], second)):
            process = subprocess.run(
                ["gcc", *flags, "-c", str(source), "-o", str(destination / "probe.o")],
                text=True, capture_output=True)
            if process.returncode:
                raise AssertionError(process.stderr)
        result = capture_comparison(first, second, [{"object": "probe.o"}],
                                    62, work / "size.json")
        assert result["verdict"] == "SECTION_DIAGNOSTIC_COMPLETE"
        assert result["owners_measured"] == 1
        assert result["gnu_total"]["executable_bytes"] > 0
        assert result["minic_total"]["executable_bytes"] > 0
        bad = capture_comparison(first, second, [{"object": "missing.o"}],
                                 62, work / "bad.json")
        assert bad["verdict"] == "SECTION_DIAGNOSTIC_INCOMPLETE"
        assert "error" in bad
        layout = capture_link_layout(work, work / "layout", "x86_64", 62)
        assert layout["verdict"] == "SECTION_LAYOUT_DIAGNOSTIC_INCOMPLETE"
        assert layout["gnu_ld_map"] == "NOT_EMITTED_BY_ORIGINAL_KBUILD"
        script = work / "arch" / "x86" / "kernel" / "vmlinux.lds"
        script.parent.mkdir(parents=True)
        script.write_text("SECTIONS { .text : { *(.text) } }\n")
        layout = capture_link_layout(work, work / "layout", "x86_64", 62)
        assert layout["verdict"] == "SECTION_LAYOUT_DIAGNOSTIC_COMPLETE"
        assert layout["gnu_ld_map"] == "NOT_EMITTED_BY_ORIGINAL_KBUILD"
        assert (work / "layout" / "gnu-vmlinux.lds").read_text() == script.read_text()
        (work / "vmlinux.map").write_text("GNU LD MAP\n")
        layout = capture_link_layout(work, work / "layout", "x86_64", 62)
        assert layout["verdict"] == "SECTION_LAYOUT_DIAGNOSTIC_COMPLETE"
        assert layout["gnu_ld_map"] == "PRESERVED_ORIGINAL_GNU_KBUILD_MAP"
        assert (work / "layout" / "gnu-vmlinux.map").read_text() == "GNU LD MAP\n"
    print("THREE_ISA_LINK_SECTION_DIAGNOSTIC_SELFTEST=PASS")


if __name__ == "__main__":
    self_test()
