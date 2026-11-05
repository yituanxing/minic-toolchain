#!/usr/bin/env python3
"""Non-gating AArch64 GNU first-link dynsym ordinal diagnostic.

Reuses the authenticated frozen GNU Kbuild objects and recorded first-link
argv.  Only the linker-script copy and output filename differ.  The normal
GNU/MiniLD comparison remains untouched and is the sole acceptance gate.
"""

from __future__ import annotations

import hashlib
import pathlib
import re
import struct
import subprocess
import sys


DISCARDED_DYNSYM = re.compile(
    r"\*\(\s*\.dynsym\s+\.dynstr\s+\.hash\s+\.gnu\.hash\s*\)"
)
RELOC_NAMES = {1025: "R_AARCH64_GLOB_DAT", 257: "R_AARCH64_ABS64"}


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def rewrite_argv(
    original: list[str], cwd: pathlib.Path, shadow_script: pathlib.Path,
    shadow_output: pathlib.Path,
) -> tuple[list[str], pathlib.Path]:
    argv = original.copy()
    script = None
    script_count = output_count = 0
    i = 0
    while i < len(original):
        arg = original[i]
        if arg in ("-T", "--script"):
            if i + 1 >= len(original):
                raise ValueError("missing -T script")
            candidate = original[i + 1]
            script = (cwd / candidate).resolve()
            argv[i + 1] = str(shadow_script)
            script_count += 1
            i += 2
            continue
        if arg.startswith("-T") and arg != "-T":
            script = (cwd / arg[2:]).resolve()
            argv[i] = "-T" + str(shadow_script)
            script_count += 1
        elif arg.startswith("--script="):
            script = (cwd / arg.split("=", 1)[1]).resolve()
            argv[i] = "--script=" + str(shadow_script)
            script_count += 1
        elif arg in ("-o", "--output"):
            if i + 1 >= len(original):
                raise ValueError("missing -o output")
            argv[i + 1] = str(shadow_output)
            output_count += 1
            i += 2
            continue
        elif arg.startswith("--output="):
            argv[i] = "--output=" + str(shadow_output)
            output_count += 1
        elif arg.startswith("-o") and arg != "-o":
            argv[i] = "-o" + str(shadow_output)
            output_count += 1
        i += 1
    if script_count != 1 or output_count != 1 or script is None:
        raise ValueError(
            f"expected one GNU linker -T and -o, got {script_count}/{output_count}"
        )
    if script.name != "vmlinux.lds" or script.parent.name != "kernel" or \
            script.parent.parent.name != "arm64":
        raise ValueError(f"not the AArch64 Linux Kbuild script: {script}")
    return argv, script


def relocations(path: pathlib.Path) -> list[tuple[int, int, int]]:
    """Extract only symbol-bearing RELA entries; do not parse readelf labels.

    In a discarded-.dynsym ELF, readelf can resolve r_sym against an unrelated
    final .symtab.  Numeric r_info is the only trustworthy original oracle.
    """
    data = path.read_bytes()
    if data[:4] != b"\x7fELF" or data[4:6] != b"\x02\x01":
        raise ValueError(f"not little-endian ELF64: {path}")
    if struct.unpack_from("<H", data, 18)[0] != 183:
        raise ValueError(f"not AArch64: {path}")
    shoff = struct.unpack_from("<Q", data, 40)[0]
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 58)
    if shentsize != 64 or not 0 < shstrndx < shnum:
        raise ValueError("unsupported ELF section header encoding")
    headers = [struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * 64)
               for i in range(shnum)]
    names_header = headers[shstrndx]
    names = data[names_header[4]:names_header[4] + names_header[5]]
    for section in headers:
        name_offset = section[0]
        end = names.find(b"\0", name_offset)
        if end < 0 or names[name_offset:end] != b".rela.dyn":
            continue
        if section[1] != 4 or section[9] != 24 or section[5] % 24:
            raise ValueError("unexpected .rela.dyn encoding")
        start, size = section[4], section[5]
        result = []
        for offset in range(start, start + size, 24):
            place, info, _addend = struct.unpack_from("<QQq", data, offset)
            symbol = info >> 32
            typ = info & 0xFFFFFFFF
            if symbol:
                result.append((place, typ, symbol))
        return result
    raise ValueError("missing .rela.dyn")


def main() -> int:
    if len(sys.argv) != 3:
        raise ValueError("usage: oracle.py <ld-capture> <ld-evidence>")
    capture = pathlib.Path(sys.argv[1]).resolve()
    evidence = pathlib.Path(sys.argv[2]).resolve()
    out = evidence / "aarch64-shadow-dynsym"
    out.mkdir(parents=True, exist_ok=True)
    original_gnu = evidence / ".tmp_vmlinux.kallsyms1.gnu"
    if not original_gnu.is_file():
        raise ValueError("missing original GNU replay (do not run a replacement baseline)")
    cwd = pathlib.Path((capture / "first-kallsyms1.cwd").read_text().strip())
    gnu = pathlib.Path((capture / "first-kallsyms1.gnu-ld").read_text().strip())
    original = (capture / "first-kallsyms1.argv0").read_bytes().split(b"\0")
    if original[-1:] == [b""]:
        original.pop()
    argv = [arg.decode() for arg in original]
    if not cwd.is_dir() or not gnu.is_file():
        raise ValueError("frozen cwd or GNU linker missing")
    shadow_script = out / "vmlinux-shadow-dynsym.lds"
    shadow_output = out / "vmlinux-shadow-dynsym.gnu"
    argv, source_script = rewrite_argv(argv, cwd, shadow_script, shadow_output)
    original_script_sha = sha256(source_script)
    original_elf_sha = sha256(original_gnu)
    contents = source_script.read_text()
    contents, replaced = DISCARDED_DYNSYM.subn(
        "/* dynsym oracle: retain .dynsym/.dynstr/.hash/.gnu.hash */", contents
    )
    if replaced != 1:
        raise ValueError(f"expected exactly one dynamic DISCARD clause; got {replaced}")
    shadow_script.write_text(contents)
    (out / "shadow-argv0.txt").write_text("\n".join(argv) + "\n")
    with (out / "gnu.stdout").open("wb") as stdout, \
            (out / "gnu.stderr").open("wb") as stderr:
        process = subprocess.run(
            [str(gnu), *argv], cwd=cwd, stdout=stdout, stderr=stderr,
            timeout=240, check=False,
        )
    (out / "gnu.status").write_text(str(process.returncode) + "\n")
    if process.returncode != 0 or not shadow_output.is_file():
        raise ValueError(f"shadow GNU link failed, status={process.returncode}")
    if sha256(source_script) != original_script_sha or \
            sha256(original_gnu) != original_elf_sha:
        raise ValueError("the original script or GNU baseline ELF changed")
    manifest = evidence / "input-objects-and-scripts.sha256"
    subprocess.run(["sha256sum", "--quiet", "--check", str(manifest)],
                   cwd=cwd, timeout=120, check=True)
    with (out / "dynsym-readelf.txt").open("wb") as listing:
        subprocess.run(["readelf", "-W", "--dyn-syms", str(shadow_output)],
                       stdout=listing, check=True)
    listing = (out / "dynsym-readelf.txt").read_text()
    entries = []
    for line in listing.splitlines():
        match = re.match(r"\s*(\d+):\s+", line)
        if match and int(match.group(1)) <= 21:
            entries.append(line)
    (out / "dynsym-first-22.txt").write_text("\n".join(entries) + "\n")
    if len(entries) != 22:
        raise ValueError(f"expected dynsym indices 0..21, found {len(entries)}")
    baseline = relocations(original_gnu)
    shadow = relocations(shadow_output)
    def format_relocs(values: list[tuple[int, int, int]]) -> str:
        return "\n".join(
            f"place=0x{place:x} type={RELOC_NAMES.get(typ, typ)} r_sym={symbol}"
            for place, typ, symbol in values
        ) + "\n"
    (out / "baseline-symbolic-rela.txt").write_text(format_relocs(baseline))
    (out / "shadow-symbolic-rela.txt").write_text(format_relocs(shadow))
    same = [(typ, symbol) for _, typ, symbol in baseline] == [
        (typ, symbol) for _, typ, symbol in shadow]
    (out / "result.txt").write_text(
        f"same_symbolic_r_info={same}\n"
        f"baseline_symbolic_count={len(baseline)}\n"
        f"shadow_symbolic_count={len(shadow)}\n"
        f"GNU_C_sources_recompiled=false\nGNU_AS_objects_rebuilt=false\n"
        f"original_script_sha256={original_script_sha}\n"
    )
    if not same:
        raise ValueError("shadow retaining dynsym changed symbolic r_info ordinals")
    print("MINILD_AARCH64_DYNSYM_ORACLE=PASS original_symbolic_r_info_unchanged "
          f"symbolic={len(baseline)} first_dynsym_entries={len(entries)} "
          "GNU_C_recompiled=false")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        print(f"MINILD_AARCH64_DYNSYM_ORACLE=INCONCLUSIVE {exc}", file=sys.stderr)
        sys.exit(1)
