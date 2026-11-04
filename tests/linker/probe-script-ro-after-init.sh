#!/usr/bin/env bash
# The Linux RO_DATA output includes writable .data..ro_after_init input.
# GNU BFD preserves SHF_WRITE on the resulting .rodata output section.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"
for tool in as ld python3; do
    command -v "$tool" >/dev/null || {
        echo "MINILD_RO_AFTER_INIT=INCONCLUSIVE missing=$tool" >&2
        exit 8
    }
done

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-ro-after-init.XXXXXX")
finish() {
    if [[ ${MINILD_RO_AFTER_INIT_KEEP:-0} == 1 ]]; then
        echo "MINILD_RO_AFTER_INIT_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap finish EXIT

as -o "$work/gnu.o" tests/linker/script-mixed-ro-after-init-x86.S
"$MINIAS" -march=rv64i -mabi=lp64 -o "$work/rv64.o" \
    tests/linker/script-mixed-ro-after-init-rv64.S

ld -m elf_x86_64 -static \
    -T tests/linker/script-mixed-ro-after-init-x86.lds \
    -o "$work/gnu.elf" "$work/gnu.o"
"$MINILD" -melf64lriscv -static \
    -T tests/linker/script-mixed-ro-after-init-rv64.lds \
    -o "$work/minild.elf" "$work/rv64.o" \
    >"$work/minild.stdout" 2>"$work/minild.stderr"

cross_gnu=0
if command -v riscv64-linux-gnu-ld >/dev/null; then
    riscv64-linux-gnu-ld -m elf64lriscv -static \
        -T tests/linker/script-mixed-ro-after-init-rv64.lds \
        -o "$work/rv64-gnu.elf" "$work/rv64.o"
    cross_gnu=1
elif [[ ${MINILD_REQUIRE_GNU:-0} == 1 ]]; then
    echo 'MINILD_RO_AFTER_INIT=INCONCLUSIVE missing=riscv64-linux-gnu-ld' >&2
    exit 8
fi

if command -v timeout >/dev/null; then
    timeout 5s "$work/gnu.elf"
else
    "$work/gnu.elf"
fi
if command -v qemu-riscv64 >/dev/null; then
    qemu-riscv64 "$work/minild.elf"
fi

python3 - "$work" "$cross_gnu" <<'PY_ELF'
from pathlib import Path
import struct
import sys

work = Path(sys.argv[1])
cross_gnu = sys.argv[2] == "1"
expected_bytes = struct.pack("<QQ", 0x0123456789ABCDEF, 0xABCDEF0123456789)


def inspect(path, machine):
    blob = path.read_bytes()
    if blob[:6] != b"\x7fELF\x02\x01":
        raise SystemExit(f"MINILD_RO_AFTER_INIT=FAIL ELF_class:{path}")
    if struct.unpack_from("<H", blob, 18)[0] != machine:
        raise SystemExit(f"MINILD_RO_AFTER_INIT=FAIL ELF_machine:{path}")
    phoff, shoff = struct.unpack_from("<QQ", blob, 32)
    phentsize, phnum = struct.unpack_from("<HH", blob, 54)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", blob, 58)
    if shentsize != 64 or shstrndx >= shnum or shoff + shnum * 64 > len(blob):
        raise SystemExit(f"MINILD_RO_AFTER_INIT=FAIL ELF_shdr:{path}")
    if phentsize != 56 or phoff + phnum * phentsize > len(blob):
        raise SystemExit(f"MINILD_RO_AFTER_INIT=FAIL ELF_phdr:{path}")
    sections = [struct.unpack_from("<IIQQQQIIQQ", blob, shoff + i * 64)
                for i in range(shnum)]
    names_sh = sections[shstrndx]
    names = blob[names_sh[4]:names_sh[4] + names_sh[5]]

    def section_name(offset):
        end = names.find(b"\0", offset)
        if end < 0:
            raise SystemExit(f"MINILD_RO_AFTER_INIT=FAIL shstrtab:{path}")
        return names[offset:end].decode("ascii")

    matches = [(i, sh) for i, sh in enumerate(sections)
               if section_name(sh[0]) == ".rodata"]
    if len(matches) != 1:
        raise SystemExit(f"MINILD_RO_AFTER_INIT=FAIL rodata_count:{path}")
    ro_index, ro = matches[0]
    contents = blob[ro[4]:ro[4] + ro[5]]
    if ro[1] != 1 or ro[2] != 0x3 or ro[9] != 0 or contents != expected_bytes:
        raise SystemExit(f"MINILD_RO_AFTER_INIT=FAIL section:{path} "
                         f"type={ro[1]} flags={ro[2]:x} entsize={ro[9]} "
                         f"data={contents.hex()}")

    covered = False
    for i in range(phnum):
        ptype, flags, offset, vaddr, paddr, filesz, memsz, align = (
            struct.unpack_from("<IIQQQQQQ", blob, phoff + i * phentsize))
        if ptype == 1 and vaddr <= ro[3] and ro[3] + ro[5] <= vaddr + memsz:
            covered = covered or (flags & 0x2) != 0
    if not covered:
        raise SystemExit(f"MINILD_RO_AFTER_INIT=FAIL missing_writable_PT_LOAD:{path}")

    symtabs = [s for s in sections if s[1] == 2]
    if len(symtabs) != 1:
        raise SystemExit(f"MINILD_RO_AFTER_INIT=FAIL symtab:{path}")
    tab = symtabs[0]
    strtab = sections[tab[6]]
    strings = blob[strtab[4]:strtab[4] + strtab[5]]
    observed = {}
    for pos in range(tab[4], tab[4] + tab[5], 24):
        string_off, _, _, index, value, _ = struct.unpack_from("<IBBHQQ", blob, pos)
        if index != ro_index or string_off == 0:
            continue
        end = strings.find(b"\0", string_off)
        if end < 0:
            raise SystemExit(f"MINILD_RO_AFTER_INIT=FAIL strtab:{path}")
        observed[strings[string_off:end].decode("ascii")] = value - ro[3]
    if observed.get("ro_const") != 0 or observed.get("late_ro_const") != 8:
        raise SystemExit(f"MINILD_RO_AFTER_INIT=FAIL symbols:{path}:{observed}")
    return contents, ro[2], ro[9]


reference = inspect(work / "gnu.elf", 62)
candidate = inspect(work / "minild.elf", 243)
if candidate != reference:
    raise SystemExit("MINILD_RO_AFTER_INIT=FAIL GNU_vs_MiniLD")
if cross_gnu and inspect(work / "rv64-gnu.elf", 243) != candidate:
    raise SystemExit("MINILD_RO_AFTER_INIT=FAIL GNU_RV64_same_object")
print(f"MINILD_RO_AFTER_INIT=PASS GNU_BFD=WA MiniLD_RV64=WA "
      f"PT_LOAD=PF_W payload_bytes={len(expected_bytes)} "
      f"same_ISA_GNU={cross_gnu}")
PY_ELF
