#!/usr/bin/env bash
# GNU ld.bfd independently specifies the source-file-qualified input section
# ordering contract. The MiniLD native path must preserve that provenance until
# the script chooses its input members (including without --gc-sections).
set -Eeuo pipefail

: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"
for command in python3 readelf as ld; do
    if ! command -v "$command" >/dev/null 2>&1; then
        echo "MINILD_INPUT_ORDER=INCONCLUSIVE missing_tool=$command" >&2
        exit 8
    fi
done

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-input-order.XXXXXX")
cleanup() {
    if [[ ${MINILD_INPUT_ORDER_KEEP:-0} == 1 ]]; then
        echo "MINILD_INPUT_ORDER_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap cleanup EXIT

# Same sections in different .o files, but the command line deliberately puts
# zeta before alpha. A filename-qualified script must be able to reverse them.
for target in riscv host; do
    if [[ $target == riscv ]]; then
        directive='.dword'
    else
        directive='.quad'
    fi
    cat > "$work/$target-entry.s" <<ASM
.section .text.entry,"ax",@progbits
.balign 8
.globl _start
_start:
    .byte 0xe1,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8
    $directive alpha_same
    $directive zeta_same
ASM
    cat > "$work/$target-alpha.s" <<'ASM'
.section .text.same,"ax",@progbits
.balign 8
.globl alpha_same
alpha_same:
    .byte 0xa1,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8
.section .text.rank.a,"ax",@progbits
.balign 8
.globl rank_a
rank_a:
    .byte 0xc1,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8
.section .text.keep,"ax",@progbits
.balign 8
.globl keep_root
keep_root:
    .byte 0xd1,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8
ASM
    cat > "$work/$target-zeta.s" <<'ASM'
.section .text.same,"ax",@progbits
.balign 8
.globl zeta_same
zeta_same:
    .byte 0xb1,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8
.section .text.rank.z,"ax",@progbits
.balign 8
.globl rank_z
rank_z:
    .byte 0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8
.section .text.dead,"ax",@progbits
.balign 8
.globl dead_symbol
dead_symbol:
    .byte 0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98
ASM
done

for name in entry zeta alpha; do
    "$MINIAS" -march=rv64i -mabi=lp64 \
        -o "$work/$name.o" "$work/riscv-$name.s"
    as -o "$work/host-$name.o" "$work/host-$name.s"
done

cat > "$work/direct.lds" <<'LDS'
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : {
    KEEP(*(.text.entry))
    *alpha.o(.text.same)
    *zeta.o(.text.same)
    KEEP(*(SORT_BY_NAME(.text.rank.*)))
    KEEP(*(.text.keep))
  }
  /DISCARD/ : { *(.text.dead) }
}
LDS
cat > "$work/after_r.lds" <<'LDS'
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : {
    KEEP(*(.text.entry))
    *(.text.same)
    KEEP(*(SORT_BY_NAME(.text.rank.*)))
    KEEP(*(.text.keep))
  }
  /DISCARD/ : { *(.text.dead) }
}
LDS

cat > "$work/check.py" <<'PY'
import struct
import sys
from pathlib import Path

case, machine_text, path_text = sys.argv[1:]
machine = int(machine_text)
path = Path(path_text)
blob = path.read_bytes()
if (blob[:6] != b"\x7fELF\x02\x01" or
        struct.unpack_from("<HH", blob, 16) != (2, machine)):
    raise SystemExit(f"{case}: not an executable ELF64 {machine} image")
shoff = struct.unpack_from("<Q", blob, 40)[0]
shentsize, shnum, shstrndx = struct.unpack_from("<HHH", blob, 58)
if shentsize != 64 or shstrndx >= shnum or shoff + shentsize * shnum > len(blob):
    raise SystemExit(f"{case}: bad ELF section table")
sections = [struct.unpack_from("<IIQQQQIIQQ", blob, shoff + 64 * i)
            for i in range(shnum)]
name_sh = sections[shstrndx]
strs = blob[name_sh[4]:name_sh[4] + name_sh[5]]

def text_at(data, offset):
    end = data.find(b"\0", offset)
    if offset >= len(data) or end == -1:
        raise SystemExit(f"{case}: bad string offset {offset}")
    return data[offset:end].decode("ascii")

names = [text_at(strs, sh[0]) for sh in sections]
match = [i for i, name in enumerate(names) if name == ".text"]
if len(match) != 1:
    raise SystemExit(f"{case}: expected exactly one .text, found {len(match)}")
ti = match[0]
text = sections[ti]
if text[1] != 1 or text[2] & 0x6 != 0x6 or text[4] + text[5] > len(blob):
    raise SystemExit(f"{case}: invalid output .text")
content = blob[text[4]:text[4] + text[5]]
sym_indices = [i for i, name in enumerate(names) if name == ".symtab"]
if len(sym_indices) != 1:
    raise SystemExit(f"{case}: expected one .symtab")
symtab = sections[sym_indices[0]]
strtab = sections[symtab[6]]
symbol_names = blob[strtab[4]:strtab[4] + strtab[5]]
symbols = {}
for p in range(symtab[4], symtab[4] + symtab[5], 24):
    name_pos, info, other, section, value, size = struct.unpack_from("<IBBHQQ", blob, p)
    if name_pos:
        symbols[text_at(symbol_names, name_pos)] = (section, value)

def offset_of(name):
    if name not in symbols:
        raise SystemExit(f"{case}: missing {name}")
    shndx, value = symbols[name]
    offset = value - text[3]
    if shndx != ti or not 0 <= offset < len(content):
        raise SystemExit(f"{case}: wrong shndx/value for {name}")
    return offset

order = ["_start", "alpha_same", "zeta_same", "rank_a", "rank_z", "keep_root"]
if case == "after_r":
    # ELF -r cannot retain original input *filenames*. The preceding -r link
    # deliberately concatenates same-named fragments in input order instead.
    order[1], order[2] = order[2], order[1]
markers = {
    "_start": 0xe1,
    "alpha_same": 0xa1,
    "zeta_same": 0xb1,
    "rank_a": 0xc1,
    "rank_z": 0xf1,
    "keep_root": 0xd1,
}
offsets = [offset_of(name) for name in order]
if offsets != sorted(offsets):
    raise SystemExit(f"{case}: filename/SORT section order incorrect: {list(zip(order, offsets))}")
for name, offset in zip(order, offsets):
    expected = bytes(range(markers[name], markers[name] + 8))
    if content[offset:offset + 8] != expected:
        raise SystemExit(f"{case}: corrupted/misplaced input bytes for {name}")
if b"\x91\x92\x93\x94\x95\x96\x97\x98" in content:
    raise SystemExit(f"{case}: discarded input .text.dead is present")
if "dead_symbol" in symbols and symbols["dead_symbol"][0] == ti:
    raise SystemExit(f"{case}: discarded symbol is defined in .text")
start_off = offset_of("_start")
if start_off + 24 > len(content):
    raise SystemExit(f"{case}: truncated entry references")
alpha_ref, zeta_ref = struct.unpack_from("<QQ", content, start_off + 8)
if (alpha_ref != text[3] + offset_of("alpha_same") or
        zeta_ref != text[3] + offset_of("zeta_same")):
    raise SystemExit(f"{case}: absolute relocation values wrong")
print(f"MINILD_INPUT_ORDER_CHECK=PASS case={case} machine={machine}"
      f" order={'/'.join(order[1:5])} gc_keep=1 discard=1")
PY

native=("$work/entry.o" "$work/zeta.o" "$work/alpha.o")
reference=("$work/host-entry.o" "$work/host-zeta.o" "$work/host-alpha.o")
failures=0
for stage in plain gc after_r; do
    script="$work/direct.lds"
    if [[ $stage == after_r ]]; then
        script="$work/after_r.lds"
        "$MINILD" -melf64lriscv -r -o "$work/combined.o" "${native[@]}"
        native_stage=("$work/combined.o")
        ld -m elf_x86_64 -r -o "$work/host-combined.o" "${reference[@]}"
        reference_stage=("$work/host-combined.o")
    else
        native_stage=("${native[@]}")
        reference_stage=("${reference[@]}")
    fi
    options=()
    if [[ $stage != plain ]]; then options+=(--gc-sections); fi
    ld -m elf_x86_64 -static -T "$script" "${options[@]}" \
        -o "$work/$stage.host.elf" "${reference_stage[@]}"
    python3 "$work/check.py" "$stage" 62 "$work/$stage.host.elf"
    if ! "$MINILD" -melf64lriscv -static -T "$script" "${options[@]}" \
        -o "$work/$stage.elf" "${native_stage[@]}" \
        > "$work/$stage.stdout" 2> "$work/$stage.stderr"; then
        echo "MINILD_INPUT_ORDER=BLOCKED case=$stage" >&2
        sed -n '1,12p' "$work/$stage.stderr" >&2
        failures=$((failures + 1))
        continue
    fi
    if ! python3 "$work/check.py" "$stage" 243 "$work/$stage.elf"; then
        echo "MINILD_INPUT_ORDER=FAIL case=$stage reason=source-identity" >&2
        failures=$((failures + 1))
    fi
done
if [[ $failures != 0 ]]; then
    echo "MINILD_INPUT_ORDER=FAIL failures=$failures gnu_oracle=3" >&2
    exit 1
fi
echo "MINILD_INPUT_ORDER=PASS cases=3 gnu_oracle=3"
