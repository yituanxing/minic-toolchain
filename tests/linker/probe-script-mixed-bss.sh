#!/usr/bin/env bash
# GNU BFD permits an early RISC-V Linux .init.pi output to combine file bytes,
# an input SHT_NOBITS .init.pi.bss, and later file bytes.  The final output
# remains SHT_PROGBITS; the BSS hole becomes real zeros in the file.  Verify
# both ELF section/symbol/segment semantics and actual executable reads.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"
for tool in as ld python3; do
    command -v "$tool" >/dev/null || {
        echo "MINILD_PI_BSS=INCONCLUSIVE missing=$tool" >&2
        exit 8
    }
done

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-pi-bss.XXXXXX")
finish() {
    if [[ ${MINILD_PI_BSS_KEEP:-0} == 1 ]]; then
        echo "MINILD_PI_BSS_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap finish EXIT

as -o "$work/gnu.o" tests/linker/script-mixed-pi-bss-x86.S
"$MINIAS" -march=rv64i -mabi=lp64 -o "$work/rv64.o" \
    tests/linker/script-mixed-pi-bss-rv64.S
ld -m elf_x86_64 -static \
    -T tests/linker/script-mixed-pi-bss-x86.lds \
    -o "$work/gnu.elf" "$work/gnu.o"
"$MINILD" -melf64lriscv -static \
    -T tests/linker/script-mixed-pi-bss-rv64.lds \
    -o "$work/minild.elf" "$work/rv64.o" \
    >"$work/minild.stdout" 2>"$work/minild.stderr" || {
        echo 'MINILD_PI_BSS=FAIL MiniLD_mixed_PROGBITS_NOBITS' >&2
        tail -n 8 "$work/minild.stderr" >&2
        exit 1
    }

cross_gnu=0
if command -v riscv64-linux-gnu-ld >/dev/null; then
    riscv64-linux-gnu-ld -m elf64lriscv -static \
        -T tests/linker/script-mixed-pi-bss-rv64.lds \
        -o "$work/rv64-gnu.elf" "$work/rv64.o"
    cross_gnu=1
elif [[ ${MINILD_REQUIRE_GNU:-0} == 1 ]]; then
    echo 'MINILD_PI_BSS=INCONCLUSIVE missing=riscv64-linux-gnu-ld' >&2
    exit 8
fi

if command -v timeout >/dev/null; then
    timeout 5s "$work/gnu.elf"
else
    "$work/gnu.elf"
fi
qemu_run=0
if command -v qemu-riscv64 >/dev/null; then
    qemu-riscv64 "$work/minild.elf"
    if (( cross_gnu )); then
        qemu-riscv64 "$work/rv64-gnu.elf"
    fi
    qemu_run=1
elif [[ ${MINILD_REQUIRE_QEMU:-0} == 1 ]]; then
    echo 'MINILD_PI_BSS=INCONCLUSIVE missing=qemu-riscv64' >&2
    exit 8
fi

python3 - "$work" "$cross_gnu" <<'PY_ELF'
from pathlib import Path
import struct
import sys

work = Path(sys.argv[1])
cross_gnu = sys.argv[2] == "1"
payload = b"Q" + b"\0" * 23 + struct.pack("<Q", 37)


def inspect(path, machine):
    blob = path.read_bytes()
    if blob[:6] != b"\x7fELF\x02\x01" or (
            struct.unpack_from("<H", blob, 18)[0] != machine):
        raise SystemExit(f"MINILD_PI_BSS=FAIL ELF_identity:{path}")
    phoff, shoff = struct.unpack_from("<QQ", blob, 32)
    phentsize, phnum = struct.unpack_from("<HH", blob, 54)
    shentsize, shnum, names_index = struct.unpack_from("<HHH", blob, 58)
    if phentsize != 56 or shentsize != 64 or names_index >= shnum or (
            phoff + phnum * 56 > len(blob) or shoff + shnum * 64 > len(blob)):
        raise SystemExit(f"MINILD_PI_BSS=FAIL ELF_headers:{path}")
    shdrs = [struct.unpack_from("<IIQQQQIIQQ", blob, shoff + i * 64)
             for i in range(shnum)]
    name_section = shdrs[names_index]
    names = blob[name_section[4]:name_section[4] + name_section[5]]

    def name(offset):
        end = names.find(b"\0", offset)
        if end < 0:
            raise SystemExit(f"MINILD_PI_BSS=FAIL shstrtab:{path}")
        return names[offset:end].decode("ascii")

    matches = [(i, s) for i, s in enumerate(shdrs) if name(s[0]) == ".init.pi"]
    if len(matches) != 1:
        raise SystemExit(f"MINILD_PI_BSS=FAIL output_section_count:{path}")
    section_index, section = matches[0]
    data = blob[section[4]:section[4] + section[5]]
    if (section[1], section[2], section[5], section[9], data) != (
            1, 0x7, 32, 0, payload):
        raise SystemExit(
            f"MINILD_PI_BSS=FAIL output_type_flags_or_zeros:{path} "
            f"type={section[1]} flags={section[2]:x} size={section[5]} "
            f"data={data.hex()}")

    symbols = [s for s in shdrs if s[1] == 2]
    if len(symbols) != 1:
        raise SystemExit(f"MINILD_PI_BSS=FAIL symtab:{path}")
    symtab = symbols[0]
    strtab = shdrs[symtab[6]]
    strings = blob[strtab[4]:strtab[4] + strtab[5]]
    offsets = {}
    for pos in range(symtab[4], symtab[4] + symtab[5], 24):
        str_off, _, _, shndx, value, _ = struct.unpack_from("<IBBHQQ", blob, pos)
        if shndx != section_index or str_off == 0:
            continue
        end = strings.find(b"\0", str_off)
        if end < 0:
            raise SystemExit(f"MINILD_PI_BSS=FAIL symbol_strtab:{path}")
        offsets[strings[str_off:end].decode("ascii")] = value - section[3]
    wanted = {"pi_code": 0, "pi_bss": 8, "pi_tail": 24}
    if any(offsets.get(symbol) != offset for symbol, offset in wanted.items()):
        raise SystemExit(f"MINILD_PI_BSS=FAIL symbol_positions:{path}:{offsets}")

    load = []
    for i in range(phnum):
        ph = struct.unpack_from("<IIQQQQQQ", blob, phoff + i * 56)
        if ph[0] == 1 and ph[3] <= section[3] and (
                section[3] + section[5] <= ph[3] + ph[6]):
            load.append(ph)
    if not load or not any(p[1] == 7 and p[5] >= 32 and (
            p[2] % 4096 == p[3] % 4096 and p[7] == 4096) for p in load):
        raise SystemExit(f"MINILD_PI_BSS=FAIL PT_LOAD:{path}:{load}")
    return data, section[2], section[1], tuple(offsets[s] for s in wanted)


expected = inspect(work / "gnu.elf", 62)
mini = inspect(work / "minild.elf", 243)
if expected != mini:
    raise SystemExit("MINILD_PI_BSS=FAIL native_GNU_vs_MiniLD_section")
if cross_gnu and inspect(work / "rv64-gnu.elf", 243) != mini:
    raise SystemExit("MINILD_PI_BSS=FAIL GNU_RV64_same_object_diff")
print("MINILD_PI_BSS=PASS output=PROGBITS flags=WAX "
      "input=PROGBITS,NOBITS,PROGBITS bytes=8+16_zero+8 "
      f"GNU_native=PASS GNU_RV64={cross_gnu}")
PY_ELF
echo "MINILD_PI_BSS_RUNTIME=PASS native=1 qemu_rv64=$qemu_run"
