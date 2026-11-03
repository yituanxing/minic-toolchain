#!/usr/bin/env bash
# MiniAS -> MiniLD linker-script input selection / section-GC contract.
# Distinguishes KEEP from ordinary script inclusion, /DISCARD/ from GC,
# and rejects undefined relocations in dead input sections.  No cross-GNU
# toolchain or QEMU is needed to run the Mini-only fixture.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"
for tool in python3 readelf; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "MINILD_SCRIPT_GC=INCONCLUSIVE missing_tool=$tool" >&2
        exit 8
    }
done

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-script-gc.XXXXXX")
finish() {
    if [[ "${MINILD_SCRIPT_GC_KEEP:-0}" == 1 ]]; then
        echo "MINILD_SCRIPT_GC_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap finish EXIT

cat >"$work/core.s" <<'ASM'
.section .text.entry,"ax",@progbits
.globl _start
.type _start,@function
_start:
    call live
    lui t0,%hi(common_live)
    addi t0,t0,%lo(common_live)
    lui t1,%hi(bss_live)
    addi t1,t1,%lo(bss_live)
    addi a7,zero,93
    ecall
.size _start,.-_start

.section .text.live,"ax",@progbits
.globl live
.type live,@function
live:
    addi a0,zero,7
    ret
.size live,.-live

# This function is not referenced by any relocation; only KEEP saves it.
.section .text.keep,"ax",@progbits
.globl keep_fn
.type keep_fn,@function
keep_fn:
    addi a0,zero,44
    ret
.size keep_fn,.-keep_fn

# GNU's conventional .init section is a GC root even with no references.
.section .init,"ax",@progbits
.globl init_fn
.type init_fn,@function
init_fn:
    addi a0,zero,66
    ret
.size init_fn,.-init_fn

# A real input .bss coexists with COMMON allocation into the output .bss.
# Both are reachable through relocations from _start.
.section .bss,"aw",@nobits
.balign 8
.globl bss_live
.type bss_live,@object
bss_live:
    .zero 8
.size bss_live,8
.comm common_live,8,8

.section .text.orphan,"ax",@progbits
.globl orphan_fn
.type orphan_fn,@function
orphan_fn:
    addi a0,zero,33
    ret
.size orphan_fn,.-orphan_fn

# /DISCARD/ must remove this from a link *without* --gc-sections as well.
.section .discard.skip,"ax",@progbits
.globl drop_fn
.type drop_fn,@function
drop_fn:
    addi a0,zero,55
    ret
.size drop_fn,.-drop_fn
ASM

cat >"$work/dead.s" <<'ASM'
# This has exactly the same section name as core.o's *live* function.
# A GC implementation that links by merged section name will retain the
# undefined call even though this particular input section is unreachable.
.section .text.live,"ax",@progbits
.globl dead_fn
.type dead_fn,@function
dead_fn:
    call missing_dead
    .word 0xdec0adde
    ret
.size dead_fn,.-dead_fn
ASM

cat >"$work/dead_reloc.s" <<'ASM'
# Unlike dead.s, this has a distinct section name so -r can legally
# preserve the GC boundary. GNU ld -r itself merges same-named sections;
# GC after that merge cannot recover the original two fragments.
.section .text.dead,"ax",@progbits
.globl dead_fn
.type dead_fn,@function
dead_fn:
    call missing_dead
    .word 0xdec0adde
    ret
.size dead_fn,.-dead_fn
ASM

cat >"$work/script.lds" <<'LDS'
OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : { *(.text.entry) *(.text.live) }
  .keeper : { KEEP(*(.text.keep)) }
  .init : { *(.init) }
  .bss : { *(.bss) *(COMMON) }
  .texts : { *(.text.orphan) *(.text.dead) }
  /DISCARD/ : { *(.discard.*) }
}
LDS

"$MINIAS" -march=rv64i -mabi=lp64 -o "$work/core.o" "$work/core.s"
"$MINIAS" -march=rv64i -mabi=lp64 -o "$work/dead.o" "$work/dead.s"
"$MINIAS" -march=rv64i -mabi=lp64 -o "$work/dead_reloc.o" "$work/dead_reloc.s"

cat >"$work/check.py" <<'PY'
import struct
import subprocess
import sys
from pathlib import Path

mode, output = sys.argv[1:]
path = Path(output)
data = path.read_bytes()
if data[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<HH", data, 16) != (2, 243):
    raise SystemExit(f"{path}: expected RV64 ET_EXEC")
shoff = struct.unpack_from("<Q", data, 40)[0]
shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 58)
if shentsize != 64 or shstrndx >= shnum or shoff + shnum * 64 > len(data):
    raise SystemExit(f"{path}: malformed section headers")
headers = [struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * 64)
           for i in range(shnum)]
namesh = headers[shstrndx]
names = data[namesh[4]:namesh[4] + namesh[5]]
sections = {}
for sh in headers[1:]:
    end = names.find(b"\0", sh[0])
    if end < 0:
        raise SystemExit(f"{path}: invalid section name")
    name = names[sh[0]:end].decode()
    if sh[1] == 1:
        sections.setdefault(name, []).append(data[sh[4]:sh[4] + sh[5]])

symbols = subprocess.check_output(["readelf", "-W", "-s", str(path)], text=True)
def defined(name):
    return [fields for line in symbols.splitlines()
            if len(fields := line.split()) >= 8 and fields[-1] == name
            and fields[6] != "UND"]

for name in ("_start", "live", "keep_fn", "init_fn", "bss_live", "common_live"):
    if len(defined(name)) != 1:
        raise SystemExit(f"{path}: missing/duplicate live symbol {name}")
if defined("drop_fn"):
    raise SystemExit(f"{path}: /DISCARD/ kept drop_fn")

def payload(name):
    return b"".join(sections.get(name, []))

# The per-section test catches a stale orphan blob even when symbol pruning
# hides it from the ELF symbol table.
keep_data = payload(".keeper") if mode == "gc" else \
    payload(".keeper") + payload(".text.keep")
if b"\x13\x05\xc0\x02" not in keep_data:
    raise SystemExit(f"{path}: KEEP content missing")
if b"\x13\x05\x20\x04" not in payload(".init"):
    raise SystemExit(f"{path}: unreferenced .init GC root was lost")
# COMMON can extend a .bss placeholder that was emptied during source-section
# GC restoration.  It must be revived as a real NOBITS output, not a NULL
# header with a dangling defined symbol.
bss_sections = [sh for sh in headers[1:]
                if names[sh[0]:names.find(b"\0", sh[0])] == b".bss"
                and sh[1] == 8]
if not bss_sections or sum(sh[5] for sh in bss_sections) < 16:
    raise SystemExit(f"{path}: COMMON and input .bss were not both allocated")
allocated_addresses = []
for name in ("bss_live", "common_live"):
    candidates = defined(name)
    if not candidates or candidates[0][6] in ("COM", "ABS", "UND"):
        raise SystemExit(f"{path}: {name} not placed in an allocated section")
    address = int(candidates[0][1], 16)
    if address % 8 != 0 or not any(
            sh[3] <= address and address + 8 <= sh[3] + sh[5]
            for sh in bss_sections):
        raise SystemExit(f"{path}: {name} address is outside aligned .bss")
    allocated_addresses.append(address)
if allocated_addresses[0] == allocated_addresses[1]:
    raise SystemExit(f"{path}: COMMON overlaps input .bss")
if mode == "gc":
    if payload(".text.keep") or not payload(".text"):
        raise SystemExit(f"{path}: script output classes were not applied")
    if b"\x13\x05\x70\x00" not in payload(".text"):
        raise SystemExit(f"{path}: linked live function missing")
if any(b"\x13\x05\x70\x03" in part
       for parts in sections.values() for part in parts):
    raise SystemExit(f"{path}: discarded bytes survived")
if mode == "gc":
    if defined("orphan_fn") or defined("dead_fn"):
        raise SystemExit(f"{path}: unreferenced section/symbol survived GC")
    if payload(".texts") or payload(".text.orphan") or payload(".text.dead"):
        raise SystemExit(f"{path}: dead script-selected sections survived GC")
    if b"\xde\xad\xc0\xde" in data:
        raise SystemExit(f"{path}: dead relocation section bytes survived")
else:
    if not defined("orphan_fn") or b"\x13\x05\x10\x02" not in \
            (payload(".texts") + payload(".text.orphan")):
        raise SystemExit(f"{path}: normal script inclusion failed")

print(f"MINILD_SCRIPT_GC_CHECK=PASS mode={mode} file={path.name}")
PY

if ! "$MINILD" -melf64lriscv -static -T "$work/script.lds" \
    -o "$work/nogc.elf" "$work/core.o" >"$work/nogc.stdout" \
    2>"$work/nogc.stderr"; then
    echo "MINILD_SCRIPT_GC=BLOCKED stage=script-without-gc" >&2
    sed -n '1,16p' "$work/nogc.stderr" >&2
    exit 1
fi
python3 "$work/check.py" nogc "$work/nogc.elf"

for stage in direct after_r; do
    inputs=("$work/core.o" "$work/dead.o")
    if [[ $stage == after_r ]]; then
        inputs=("$work/core.o" "$work/dead_reloc.o")
        "$MINILD" -melf64lriscv -r -o "$work/combined.o" "${inputs[@]}"
        inputs=("$work/combined.o")
    fi
    if ! "$MINILD" -melf64lriscv -static --gc-sections \
        -T "$work/script.lds" -o "$work/$stage.elf" "${inputs[@]}" \
        >"$work/$stage.stdout" 2>"$work/$stage.stderr"; then
        echo "MINILD_SCRIPT_GC=BLOCKED stage=$stage" >&2
        sed -n '1,16p' "$work/$stage.stderr" >&2
        exit 1
    fi
    python3 "$work/check.py" gc "$work/$stage.elf"
done

echo 'MINILD_SCRIPT_GC=PASS stages=3 keep=1 discard=1 dead_undefined_reloc=1'
