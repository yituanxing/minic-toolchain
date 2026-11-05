#!/usr/bin/env bash
# GNU/BFD ELF symbol-section provenance is part of the x86 Linux
# --emit-relocs contract.  A script assignment to `.` must not turn a
# section-relative relocation into an ABS relocation in the final ELF:
# arch/x86/tools/relocs rejects those ABS records during kernel postlink.
set -Eeuo pipefail

: "${MINILD:?MINILD must identify the linker under test}"
for tool in as ld readelf python3; do
    command -v "$tool" >/dev/null || exit 8
done

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-x86-script-provenance.XXXXXXXX")
finish() {
    if [[ ${MINILD_SCRIPT_PROVENANCE_KEEP:-0} == 1 ]]; then
        echo "MINILD_SCRIPT_PROVENANCE_ARTIFACTS=$work"
    else
        rm -r -- "$work"
    fi
}
trap finish EXIT

cat >"$work/entry.s" <<'ASM'
.section .text,"ax",@progbits
.globl _start
_start:
    mov $_etext,%rax
    mov $_sdata,%rbx
    mov $__start_orc_unwind,%rcx
    mov $__vvar_page,%rdx
    ret
.section .data,"aw",@progbits
    .long 0xaabbeeff
.section .orc_unwind,"a",@progbits
    .long 0x12345678
.section .tracedata,"a",@progbits
    .long 0x89abcdef
ASM
cat >"$work/layout.lds" <<'LDS'
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)
PHDRS {
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}
SECTIONS {
    . = 0x400000;
    .text : { _stext = .; *(.text) } :text
    _etext = .;
    _text_alias = _stext;
    _text_size = _etext - _stext;
    _absolute_here = ABSOLUTE(.);
    . = ALIGN(0x1000);
    /* An input-section anchor can begin after an output-section prefix.
     * GNU still binds _sdata to the output's start, not to SHN_ABS. */
    .data : { _sdata = .; . += 0x20; *(.data) _edata = .; } :data
    . = ALIGN(4);
    .orc_unwind : {
        __start_orc_unwind = .;
        KEEP(*(.orc_unwind))
        __stop_orc_unwind = .;
    } :data
    /* Empty output sections are omitted by GNU; a boundary symbol at
     * their location inherits the next existing output-section owner. */
    .builtin_fw : {
        __start_builtin_fw = .;
        KEEP(*(.builtin_fw))
        __end_builtin_fw = .;
    } :data
    .tracedata : { *(.tracedata) } :data
    . = ALIGN(0x1000);
    __vvar_page = .;
    .vvar : { . += 16; } :data
}
LDS

as -o "$work/entry.o" "$work/entry.s"
ld -m elf_x86_64 --emit-relocs -static -T "$work/layout.lds" \
    -o "$work/gnu.elf" "$work/entry.o" >"$work/gnu.log" 2>&1
"$MINILD" -m elf_x86_64 --emit-relocs -static -T "$work/layout.lds" \
    -o "$work/mini.elf" "$work/entry.o" >"$work/mini.log" 2>&1

python3 - "$work/gnu.elf" "$work/mini.elf" <<'PY'
import re
import subprocess
import sys

NAMES = {
    "_stext": ".text",
    "_etext": ".text",
    "_sdata": ".data",
    "_edata": ".data",
    "__start_orc_unwind": ".orc_unwind",
    "__stop_orc_unwind": ".orc_unwind",
    "__vvar_page": ".vvar",
    "__start_builtin_fw": ".tracedata",
    "__end_builtin_fw": ".tracedata",
    "_text_alias": ".text",
    "_text_size": "ABS",
    "_absolute_here": "ABS",
}

def snapshot(path):
    sections = {}
    for line in subprocess.check_output(["readelf", "-W", "-S", path], text=True).splitlines():
        match = re.match(r"\s*\[\s*(\d+)\]\s+(\S+)", line)
        if match:
            sections[int(match.group(1))] = match.group(2)
    symbols = {}
    for line in subprocess.check_output(["readelf", "-W", "-s", path], text=True).splitlines():
        parts = line.split()
        if len(parts) >= 8 and parts[0].endswith(":") and parts[-1] in NAMES:
            symbols[parts[-1]] = sections.get(int(parts[6]), parts[6]) if parts[6].isdecimal() else parts[6]
    missing = set(NAMES) - set(symbols)
    if missing:
        raise SystemExit(f"MINILD_X86_SCRIPT_PROVENANCE=FAIL missing_symbols:{path}:{sorted(missing)}")
    rela = subprocess.check_output(["readelf", "-W", "-r", path], text=True)
    for name in ("_etext", "_sdata", "__start_orc_unwind", "__vvar_page"):
        if not re.search(r"R_X86_64_32S\s+[0-9a-fA-F]+\s+" + re.escape(name) + r"\s+\+", rela):
            raise SystemExit(f"MINILD_X86_SCRIPT_PROVENANCE=FAIL missing_R_X86_64_32S:{path}:{name}")
    return symbols

gnu, mini = (snapshot(path) for path in sys.argv[1:])
for name, expected in NAMES.items():
    if gnu[name] != expected:
        raise SystemExit(f"MINILD_X86_SCRIPT_PROVENANCE=FAIL unexpected_GNU:{name}:{gnu[name]} != {expected}")
    if mini[name] != gnu[name]:
        raise SystemExit(f"MINILD_X86_SCRIPT_PROVENANCE=FAIL symbol:{name}:gnu={gnu[name]}:mini={mini[name]}")
print("MINILD_X86_SCRIPT_PROVENANCE=PASS GNU_section_binding=10/10 ABS=2/2 final_R_X86_64_32S=4")
PY
