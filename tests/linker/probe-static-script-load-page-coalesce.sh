#!/usr/bin/env bash
# GNU BFD parity: scripted RW and RX output sections may occupy one load
# page.  They require a single RWE PT_LOAD with affine file/VMA/LMA layout.
set -Eeuo pipefail
: "$MINILD"
for tool in as ld python3 sed; do
    command -v "$tool" >/dev/null || exit 8
done
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-static-load-page.XXXXXX")
finish() {
    if [[ "$(printenv MINILD_LOAD_PAGE_KEEP || true)" == 1 ]]; then
        echo "MINILD_LOAD_PAGE_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap finish EXIT

cat >"$work/input.s" <<'ASM'
.section .text.start,"ax",@progbits
.globl _start
_start:
  mov $60,%eax
  xor %edi,%edi
  syscall
.section .data.init,"aw",@progbits
.balign 16
.globl writable_prelude
writable_prelude:
  .quad 0x123456789abcdef0
.section .init.text,"ax",@progbits
.globl executable_init
executable_init:
  .rept 1024
  nop
  .endr
.section .late.data,"aw",@progbits
.globl late_data
late_data:
  .quad 0x8877665544332211
ASM
cat >"$work/input.lds" <<'LDS'
ENTRY(_start)
SECTIONS {
  . = 0x400000;
  .text : { *(.text.start) }
  . = ALIGN(0x1000);
  .data.init : { *(.data.init) }
  /* The output owner starts at the end of .data, but its first WORK
     fragment begins later: its leading hole must be counted once. */
  .init.text : { . = ALIGN(0x800); *(.init.text) }
  . = ALIGN(0x1000);
  .late.data : { *(.late.data) }
}
LDS
cat >"$work/input-nonaffine.lds" <<'LDS'
ENTRY(_start)
SECTIONS {
  . = 0x400000;
  .text : { *(.text.start) }
  . = ALIGN(0x1000);
  .data.init : { *(.data.init) }
  .init.text : AT(0x500000) { . = ALIGN(0x800); *(.init.text) }
  . = ALIGN(0x1000);
  .late.data : { *(.late.data) }
}
LDS

as -o "$work/input.o" "$work/input.s"
ld -m elf_x86_64 -static --no-warn-rwx-segments -z max-page-size=0x1000 \
    -T "$work/input.lds" -o "$work/gnu.elf" "$work/input.o"
"$MINILD" -melf_x86_64 -static -z max-page-size=0x1000 \
    -T "$work/input.lds" -o "$work/mini.elf" "$work/input.o"
for mode in separate-code noseparate-code; do
    ld -m elf_x86_64 -static --no-warn-rwx-segments -z "$mode" \
        -z max-page-size=0x1000 -T "$work/input.lds" \
        -o "$work/gnu-$mode.elf" "$work/input.o"
    "$MINILD" -melf_x86_64 -static -z "$mode" \
        -z max-page-size=0x1000 -T "$work/input.lds" \
        -o "$work/mini-$mode.elf" "$work/input.o"
done

# Unlike an arbitrary giant script-address jump, a sub-page location-counter
# reservation after ALIGN(page) stays inside BFD's existing writable LOAD.
# Its *total* gap from the previous data end may exceed a page: omitting the
# zero-filled file hole would make the would-be combined PT_LOAD non-affine.
# A reservation of a full page instead starts a new LOAD and may compress
# the old file hole. Check both boundaries against genuine BFD.
for gap in small large; do
    if [[ "$gap" == small ]]; then
        bytes=0xe00
    else
        bytes=0x1000
    fi
    sed "s/  .late.data :/  . += $bytes; .late.data :/" \
        "$work/input.lds" >"$work/input-$gap.lds"
    ld -m elf_x86_64 -static --no-warn-rwx-segments -z noseparate-code \
        -z max-page-size=0x1000 -T "$work/input-$gap.lds" \
        -o "$work/gnu-noseparate-code-$gap.elf" "$work/input.o"
    "$MINILD" -melf_x86_64 -static -z noseparate-code \
        -z max-page-size=0x1000 -T "$work/input-$gap.lds" \
        -o "$work/mini-noseparate-code-$gap.elf" "$work/input.o"
done

if "$MINILD" -melf_x86_64 -static -z max-page-size=0x1000 \
    -T "$work/input-nonaffine.lds" -o "$work/nonaffine.elf" \
    "$work/input.o" >"$work/nonaffine.stdout" 2>"$work/nonaffine.stderr"; then
    echo 'MINILD_LOAD_PAGE=FAIL nonaffine-AT-accepted' >&2
    exit 1
fi
if ! grep -Eq 'unsafe-script-load-page-overlap|invalid-script-load' \
    "$work/nonaffine.stderr"; then
    echo 'MINILD_LOAD_PAGE=FAIL nonaffine-AT-unexpected-diagnostic' >&2
    exit 1
fi
if "$MINILD" -melf_x86_64 -static -z noseparate-code \
    -z max-page-size=0x1000 -T "$work/input-nonaffine.lds" \
    -o "$work/nonaffine-noseparate.elf" "$work/input.o" \
    >"$work/nonaffine-noseparate.stdout" \
    2>"$work/nonaffine-noseparate.stderr"; then
    echo 'MINILD_LOAD_PAGE=FAIL noseparate-nonaffine-AT-accepted' >&2
    exit 1
fi

python3 - "$work" <<'PY_ELF'
import pathlib
import struct
import sys

root = pathlib.Path(sys.argv[1])

def examine(path):
    raw = path.read_bytes()
    assert raw[:6] == b"\x7fELF\x02\x01", ("ELF64", path)
    phoff, shoff = struct.unpack_from("<QQ", raw, 32)
    phentsize, phnum = struct.unpack_from("<HH", raw, 54)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", raw, 58)
    assert phentsize == 56 and shentsize == 64
    ph = [struct.unpack_from("<IIQQQQQQ", raw, phoff + i*56)
          for i in range(phnum)]
    sh = [struct.unpack_from("<IIQQQQIIQQ", raw, shoff + i*64)
          for i in range(shnum)]
    name_sh = sh[shstrndx]
    names = raw[name_sh[4]:name_sh[4]+name_sh[5]]
    sections = {}
    for item in sh:
        end = names.find(b"\0", item[0])
        sections[names[item[0]:end].decode()] = item
    return raw, ph, sections

def covered(load, sec):
    return (load[0] == 1 and load[3] <= sec[3] and
            sec[3] + sec[5] <= load[3] + load[6])

for mode in ("", "-separate-code", "-noseparate-code",
             "-noseparate-code-small", "-noseparate-code-large"):
  gnu_loads = None
  gnu_allocated = None
  for label in ("gnu", "mini"):
    raw, loads, sec = examine(root / (label + mode + ".elf"))
    loads = [item for item in loads if item[0] == 1]
    rx = sec[".text"]
    rw = sec[".data.init"]
    init = sec[".init.text"]
    late = sec[".late.data"]
    assert rw[3] // 4096 == init[3] // 4096, (label, "not same page")
    # This is the exact AArch64 Linux failure class: the output section
    # starts at the previous section's end, while the first input member
    # begins after a nonzero internal ALIGN-created leading hole.
    prefix = (0x800 - init[3] % 0x800) % 0x800
    assert prefix != 0 and init[5] == prefix + 1024, (
        label, "nonzero output prefix absent", init)
    # GNU BFD fills x86 executable gaps with multi-byte NOP sequences,
    # while MiniLD's current non-PHDR script filler uses zero bytes.
    # These permissions/layout checks compare ELF geometry, not the
    # machine-dependent choice of unreachable executable padding bytes.
    assert len(raw[init[4]:init[4]+prefix]) == prefix, (
        label, "output-prefix storage", init)
    assert raw[init[4]+prefix:init[4]+prefix+1024] == bytes([0x90])*1024, (
        label, "first member relocated after prefix", init)
    page_loads = [p for p in loads if covered(p, rw) and covered(p, init)]
    assert len(page_loads) == 1 and page_loads[0][1] == 7, (
        label, "RW+RX requires RWE LOAD", loads)
    assert len(loads) == (2 if mode in ("-noseparate-code",
                                     "-noseparate-code-small") else 3), (
        label, mode, "incorrect GNU separate-code page grouping", loads)
    assert any(p[1] == 5 and covered(p, rx) for p in loads), (
        label, "independent RX page became writable", loads)
    assert all(p[2] % 4096 == p[3] % 4096 for p in loads), (
        label, "LOAD page congruence", loads)
    assert any((p[1] & 2) and covered(p, late) for p in loads), (
        label, "late writable data lost W", loads)
    allocated = {name: (item[1], item[2], item[3], item[4], item[5])
                 for name, item in sec.items() if item[2] & 2}
    if label == "gnu":
        gnu_loads, gnu_allocated = loads, allocated
    else:
        assert loads == gnu_loads, (
            "MiniLD PT_LOAD descriptors differ from GNU BFD",
            gnu_loads, loads)
        assert allocated == gnu_allocated, (
            "MiniLD allocated section geometry differs from GNU BFD",
            gnu_allocated, allocated)
    print("MINILD_LOAD_PAGE=PASS linker=%s loads=%s "
          "mode=%s init_perms=RWE standalone_text=RX leading=%#x" %
          (label, len(loads), mode or "default", prefix))
PY_ELF
