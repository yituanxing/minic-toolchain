#!/usr/bin/env bash
# Host-only PHDR ownership contract across ET_EXEC and ET_DYN policies.
# Inspect ELF64 bytes directly; GNU RISC-V binutils and QEMU are not required.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"
command -v python3 >/dev/null 2>&1 || {
    echo "MINILD_SEGMENTS=INCONCLUSIVE missing_tool=python3" >&2
    exit 8
}
if [[ ${MINILD_REQUIRE_QEMU:-0} == 1 ]] &&
   ! command -v qemu-riscv64 >/dev/null 2>&1; then
    echo 'MINILD_SEGMENTS=INCONCLUSIVE missing_tool=qemu-riscv64' >&2
    exit 8
fi
if [[ ${MINILD_REQUIRE_GNU:-0} == 1 ]] &&
   ! command -v riscv64-linux-gnu-ld >/dev/null 2>&1; then
    echo 'MINILD_SEGMENTS=INCONCLUSIVE missing_tool=riscv64-linux-gnu-ld' >&2
    exit 8
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-segment-plan.XXXXXX")
finish() {
    if [[ ${MINILD_SEGMENT_KEEP:-0} == 1 ]]; then
        echo "MINILD_SEGMENT_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap finish EXIT

cat >"$work/input.s" <<'ASM'
.section .text.start,"ax",@progbits
.globl _start
.type _start,@function
_start:
  addi a0,zero,17
  ret
.size _start,.-_start

.section .data.word,"aw",@progbits
.balign 8
  .dword 0x1122334455667788

.section .tdata.slot,"awT",@progbits
.balign 8
.globl tls_slot
.type tls_slot,@tls_object
tls_slot:
  .dword 0x12345678
.size tls_slot,8

.section .tbss.slot,"awT",@nobits
.balign 8
  .zero 16

.section .note.minild,"a",@note
.balign 4
  .long 4,4,1
  .ascii "GNU\0"
  .long 0x11223344
ASM

cat >"$work/static.lds" <<'LDS'
OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
  . = 0x20000;
  .text : { *(.text.start) }
  . = 0x30000;
  .data : AT(0x50000) { *(.data.word) }
  .tdata : { *(.tdata.slot) }
  .tbss : { *(.tbss.slot) }
  .note : { *(.note.minild) }
}
LDS

cat >"$work/vdso.lds" <<'LDS'
OUTPUT_ARCH(riscv)
SECTIONS {
  . = SIZEOF_HEADERS;
  .note : { *(.note.minild) } :text :note
  .hash : { *(.hash) } :text
  .gnu.hash : { *(.gnu.hash) } :text
  .dynsym : { *(.dynsym) } :text
  .dynstr : { *(.dynstr) } :text
  .gnu.version : { *(.gnu.version) } :text
  .gnu.version_d : { *(.gnu.version_d) } :text
  .rela.dyn : { *(.rela.dyn) } :text
  .text : { *(.text.start) } :text
  .dynamic : { *(.dynamic) } :text :dynamic
}
PHDRS {
  text PT_LOAD FLAGS(5) FILEHDR PHDRS;
  dynamic PT_DYNAMIC FLAGS(4);
  note PT_NOTE FLAGS(4);
  eh_frame_hdr PT_GNU_EH_FRAME;
}
VERSION {
  MINILD_1 {
    global: _start;
    local: *;
  };
}
LDS

"$MINIAS" -march=rv64i -mabi=lp64 -o "$work/input.o" "$work/input.s"
"$MINILD" -melf64lriscv -e _start -o "$work/static-default.elf" "$work/input.o"
"$MINILD" -melf64lriscv -T "$work/static.lds" -o "$work/static-script.elf" "$work/input.o"
"$MINILD" -melf64lriscv -shared -o "$work/shared.so" "$work/input.o"
"$MINILD" -melf64lriscv -pie --dynamic-linker=/lib/ld-musl-riscv64.so.1 \
    -e _start -o "$work/pie.elf" "$work/input.o"

# A scripted RX .text immediately followed by R .rodata occupies one host
# mapping page.  Separate PT_LOADs would revoke X permission on that page
# when QEMU/Linux maps the later R-only LOAD.  Verify both the ELF headers
# and actual Linux user-mode execution; compare the same objects with GNU ld
# whenever its RISC-V driver is available.
cat >"$work/script-page.s" <<'ASM'
.section .text.start,"ax",@progbits
.globl _start
_start:
  lla t0,script_ro_value
  lbu t1,0(t0)
  addi t2,zero,114
  bne t1,t2,script_bad_data
  lla t0,script_rw_value
  ld t1,0(t0)
  addi t2,zero,37
  bne t1,t2,script_bad_data
  lla t0,script_bss_value
  ld t1,0(t0)
  bne t1,zero,script_bad_data
  addi a0,zero,0
  addi a7,zero,93
  ecall
script_bad_data:
  addi a0,zero,1
  addi a7,zero,93
  ecall
.section .rodata.literal,"a",@progbits
script_ro_value:
  .ascii "read-only-data\0"
.section .data.word,"aw",@progbits
script_rw_value:
  .dword 37
.section .bss.value,"aw",@nobits
.balign 8
script_bss_value:
  .zero 8
ASM
cat >"$work/script-page.lds" <<'LDS'
OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : { *(.text.start) }
  .rodata : { *(.rodata.literal) }
  . = ALIGN(0x1000);
  .data : { *(.data.word) }
  . = ALIGN(0x1000);
  .bss : { *(.bss.value) }
}
LDS
cat >"$work/script-page-wx.lds" <<'LDS'
OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : { *(.text.start) }
  .data : { *(.data.word) }
  .rodata : { *(.rodata.literal) }
  .bss : { *(.bss.value) }
}
LDS
"$MINIAS" -march=rv64i -mabi=lp64 \
    -o "$work/script-page.o" "$work/script-page.s"
"$MINILD" -melf64lriscv -T "$work/script-page.lds" \
    -o "$work/script-page.elf" "$work/script-page.o"
"$MINILD" -melf64lriscv -T "$work/script-page-wx.lds" \
    -o "$work/script-page-wx.elf" "$work/script-page.o"

# Linux's AArch64 init range deliberately places writable .got metadata
# immediately before executable .init.text within the same loader page.
# GNU BFD merges that page into an RWE PT_LOAD, including cases where neither
# input section independently has W+X.  Disjoint PT_LOADs with conflicting
# flags would instead remap part of executable memory.
cat >"$work/script-page-existing-rwx.s" <<'ASM'
.section .pi.rw,"aw",@progbits
.globl pi_rw_value
pi_rw_value:
  .dword 0x12345678
.section .pi.rwx,"awx",@progbits
.globl pi_rwx_value
pi_rwx_value:
  .dword 0x88776655
ASM
cat >"$work/script-page-existing-rwx.lds" <<'LDS'
OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : { *(.text.start) }
  .rodata : { *(.rodata.literal) }
  . = ALIGN(0x1000);
  .data : { *(.data.word) }
  .pi_rw : { *(.pi.rw) }
  .pi_rwx : { *(.pi.rwx) }
  . = ALIGN(0x1000);
  .bss : { *(.bss.value) }
}
LDS
"$MINIAS" -march=rv64i -mabi=lp64 \
    -o "$work/script-page-existing-rwx.o" \
    "$work/script-page-existing-rwx.s"
"$MINILD" -melf64lriscv -T "$work/script-page-existing-rwx.lds" \
    -o "$work/script-page-existing-rwx.elf" \
    "$work/script-page.o" "$work/script-page-existing-rwx.o"
if command -v riscv64-linux-gnu-ld >/dev/null 2>&1; then
    riscv64-linux-gnu-ld -m elf64lriscv -static \
        -T "$work/script-page-existing-rwx.lds" \
        -o "$work/script-page-existing-rwx-gnu.elf" \
        "$work/script-page.o" "$work/script-page-existing-rwx.o"
fi
if command -v qemu-riscv64 >/dev/null 2>&1; then
    qemu-riscv64 "$work/script-page-existing-rwx.elf" || {
        echo 'MINILD_SEGMENT_CHECK=FAIL existing-rwx-MiniLD-qemu' >&2
        exit 1
    }
    if [[ -s "$work/script-page-existing-rwx-gnu.elf" ]]; then
        qemu-riscv64 "$work/script-page-existing-rwx-gnu.elf" || {
            echo 'MINILD_SEGMENT_CHECK=FAIL existing-rwx-GNU-qemu' >&2
            exit 1
        }
    fi
fi
if command -v riscv64-linux-gnu-ld >/dev/null 2>&1; then
    riscv64-linux-gnu-ld -m elf64lriscv -static \
        -T "$work/script-page.lds" -o "$work/script-page-gnu.elf" \
        "$work/script-page.o"
    if command -v qemu-riscv64 >/dev/null 2>&1; then
        qemu-riscv64 "$work/script-page-gnu.elf" || {
            echo 'MINILD_SEGMENT_CHECK=FAIL same-page-GNU-qemu' >&2
            exit 1
        }
    fi
fi
if command -v qemu-riscv64 >/dev/null 2>&1; then
    qemu-riscv64 "$work/script-page.elf" || {
        echo 'MINILD_SEGMENT_CHECK=FAIL same-page-MiniLD-qemu' >&2
        exit 1
    }
fi

# A surviving empty TLS fragment may start the TLS block before the first
# nonempty fragment.  Reserve a non-TLS prefix so the empty section starts
# before the next TLS section's stronger alignment boundary.  PT_TLS base and
# both static/dynamic STT_TLS values must use the same retained TLS origin.
cat >"$work/tls-empty-first.s" <<'ASM'
.section .text.start,"ax",@progbits
.globl _start
_start:
  ret

.section .data.prefix,"aw",@progbits
.balign 8
  .dword 0x12345678

.section .tdata.anchor,"awT",@progbits
.balign 8
.globl tls_anchor
.type tls_anchor,@tls_object
tls_anchor:
.size tls_anchor,0

.section .tdata.value,"awT",@progbits
.balign 64
.globl tls_value
.type tls_value,@tls_object
tls_value:
  .dword 0x8877665544332211
.size tls_value,8
ASM
"$MINIAS" -march=rv64i -mabi=lp64 \
    -o "$work/tls-empty-first.o" "$work/tls-empty-first.s"
"$MINILD" -melf64lriscv -e _start \
    -o "$work/static-tls-empty-first.elf" "$work/tls-empty-first.o"
"$MINILD" -melf64lriscv -shared \
    -o "$work/shared-tls-empty-first.so" "$work/tls-empty-first.o"

# Zero-length surviving TLS still defines the origin for STT_TLS, but by
# itself must not produce a PT_TLS program header.
cat >"$work/tls-all-empty.s" <<'ASM'
.section .text.start,"ax",@progbits
.globl _start
_start:
  ret
.section .data.prefix,"aw",@progbits
  .dword 0x12345678
.section .tdata.empty,"awT",@progbits
.balign 64
.globl tls_empty
.type tls_empty,@tls_object
tls_empty:
.size tls_empty,0
ASM
"$MINIAS" -march=rv64i -mabi=lp64 \
    -o "$work/tls-all-empty.o" "$work/tls-all-empty.s"
"$MINILD" -melf64lriscv -shared \
    -o "$work/shared-tls-all-empty.so" "$work/tls-all-empty.o"

# The vDSO script deliberately excludes writable/TLS inputs. It must preserve
# the fixed four PHDRs (including the empty explicit EH-frame header).
cat >"$work/vdso-input.s" <<'ASM'
.section .text.start,"ax",@progbits
.globl _start
_start:
  addi a0,zero,17
  ret
.section .note.minild,"a",@note
.balign 4
  .long 4,4,1
  .ascii "GNU\0"
  .long 0x11223344
ASM
"$MINIAS" -march=rv64i -mabi=lp64 -o "$work/vdso-input.o" "$work/vdso-input.s"
"$MINILD" -melf64lriscv -shared --hash-style=both -soname libsegment.so \
    -T "$work/vdso.lds" \
    -o "$work/vdso.so" "$work/vdso-input.o"

# Scripted ET_DYN prunes real input sections before dynamic metadata sizing.
# Allocated TLS sections removed by /DISCARD/ must also be invisible to the
# later PT_TLS segment planner, including SHT_NOBITS inputs whose size was
# reset to zero by the pre-prune pass.
sed '/^  .dynamic :/a\  /DISCARD/ : { *(.data.word) *(.tdata.slot) *(.tbss.slot) }' \
    "$work/vdso.lds" >"$work/vdso-tls-discard.lds"
if ! "$MINILD" -melf64lriscv -shared --hash-style=both \
    -soname libsegment-tls-discard.so -T "$work/vdso-tls-discard.lds" \
    -o "$work/vdso-tls-discard.so" "$work/input.o" \
    >"$work/vdso-tls-discard.stdout" \
    2>"$work/vdso-tls-discard.stderr"; then
    echo 'MINILD_SEGMENT_CHECK=FAIL mode=vdso-tls-discard unexpected-rejection' >&2
    sed -n '1,12p' "$work/vdso-tls-discard.stderr" >&2
    exit 1
fi

# A surviving TLS input cannot be represented by this constrained Linux
# vDSO script: its PHDRS require a single read/execute PT_LOAD and have no
# PT_TLS entry or writable PT_LOAD.  Do not accept live TLS accidentally while
# fixing the discarded-only case.  Exercise both initialized and NOBITS TLS.
sed '/^  .dynamic :/a\  .tdata : { *(.tdata.slot) } :text\
  /DISCARD/ : { *(.data.word) *(.tbss.slot) }' \
    "$work/vdso.lds" >"$work/vdso-tls-live-data.lds"
sed '/^  .dynamic :/a\  .tbss : { *(.tbss.slot) } :text\
  /DISCARD/ : { *(.data.word) *(.tdata.slot) }' \
    "$work/vdso.lds" >"$work/vdso-tls-live-bss.lds"
for kind in data bss; do
    if "$MINILD" -melf64lriscv -shared --hash-style=both \
        -soname libsegment-tls-live.so -T "$work/vdso-tls-live-$kind.lds" \
        -o "$work/vdso-tls-live-$kind.so" "$work/input.o" \
        >"$work/vdso-tls-live-$kind.stdout" \
        2>"$work/vdso-tls-live-$kind.stderr"; then
        echo "MINILD_SEGMENT_CHECK=FAIL mode=vdso-tls-live-$kind unexpectedly-linked" >&2
        exit 1
    fi
    if ! grep -q 'invalid-shared-tls-segment-layout' \
        "$work/vdso-tls-live-$kind.stderr"; then
        echo "MINILD_SEGMENT_CHECK=FAIL mode=vdso-tls-live-$kind missing-diagnostic" >&2
        sed -n '1,12p' "$work/vdso-tls-live-$kind.stderr" >&2
        exit 1
    fi
done

python3 - "$work" <<'PY'
import struct
import sys
from pathlib import Path

base = Path(sys.argv[1])


def inspect(filename, etype):
    raw = (base / filename).read_bytes()
    assert raw[:6] == b'\x7fELF\x02\x01', (filename, 'ELF64 little endian')
    assert struct.unpack_from('<HH', raw, 16) == (etype, 243), (filename, 'ELF type/arch')
    phoff = struct.unpack_from('<Q', raw, 32)[0]
    phentsize, phnum = struct.unpack_from('<HH', raw, 54)
    assert phentsize == 56 and phoff + phnum * 56 <= len(raw), (filename, 'PHDR bounds')
    ph = [struct.unpack_from('<IIQQQQQQ', raw, phoff + i * 56)
          for i in range(phnum)]
    shoff = struct.unpack_from('<Q', raw, 40)[0]
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', raw, 58)
    assert shentsize == 64 and shstrndx < shnum and shoff + shnum * 64 <= len(raw)
    sh = [struct.unpack_from('<IIQQQQIIQQ', raw, shoff + i * 64)
          for i in range(shnum)]
    strings = raw[sh[shstrndx][4]:sh[shstrndx][4] + sh[shstrndx][5]]
    names = {}
    for sec in sh:
        start = sec[0]
        end = strings.find(b'\0', start)
        assert end >= start, (filename, 'section name')
        names.setdefault(strings[start:end].decode(), []).append(sec)
    for segment in ph:
        typ, flags, off, vaddr, paddr, filesz, memsz, align = segment
        # GNU RISC-V emits PT_RISCV_ATTRIBUTES (0x70000003) with a
        # file-backed payload and p_memsz=0.  Only memory-mapped LOAD/TLS
        # segments require p_filesz <= p_memsz; all program-header file
        # ranges must still stay within the ELF image.
        if typ in (1, 7):
            assert filesz <= memsz, (filename, 'mapped segment extent', segment)
        assert filesz == 0 or off + filesz <= len(raw), (
            filename, 'segment file extent', segment)
        assert align in (0, 1) or (align & (align - 1)) == 0, (filename, 'alignment')
        if align > 1:
            assert (vaddr - off) % align == 0, (filename, 'segment congruence')
    return raw, ph, names


def one(names, name):
    assert len(names.get(name, [])) == 1, (name, names.get(name))
    return names[name][0]


def covers(segment, sec):
    return (segment[3] <= sec[3] and sec[3] + sec[5] <= segment[3] + segment[6])


raw, ph, names = inspect('script-page.elf', 2)
loads = [p for p in ph if p[0] == 1]
text = one(names, '.text')
rodata = one(names, '.rodata')
data = one(names, '.data')
assert len(loads) == 3, ('same-page LOAD count', ph)
assert [p[1] for p in loads] == [5, 6, 6], ('same-page permissions', ph)
assert text[3] // 4096 == rodata[3] // 4096, ('same-page fixture', text, rodata)
assert covers(loads[0], text) and covers(loads[0], rodata), (
    'RX and R sections must share executable LOAD', loads[0], text, rodata)
assert covers(loads[1], data) and data[3] // 4096 != text[3] // 4096, (
    'writable section must retain its own page and LOAD', loads[1], data)
bss = one(names, '.bss')
assert bss[1] == 8 and covers(loads[2], bss) and (
    loads[2][5] == 0 and loads[2][6] == bss[5]), (
    'pure NOBITS page requires zero-file-size writable LOAD', loads[2], bss)
assert rodata[4] % 4096 == rodata[3] % 4096 and (
    data[4] % 4096 == data[3] % 4096) and (
    loads[2][2] % 4096 == loads[2][3] % 4096), (
    'script output sections must have mmap-compatible file addresses', rodata, data, bss)
print('MINILD_SEGMENT_CHECK=PASS mode=script-same-page RX+R=coalesced '
      'RW=separate-page BSS=NOBITS-only GNU+QEMU=if-available')


raw, ph, names = inspect('script-page-wx.elf', 2)
loads = [p for p in ph if p[0] == 1]
assert len(loads) == 1 and loads[0][1] == 7, (
    'same-page RX+RW needs one RWE mapping', ph)
for name in ('.text', '.data', '.rodata', '.bss'):
    assert covers(loads[0], one(names, name)), (
        'same-page RX+RW section not in unified LOAD', name, loads)
assert loads[0][2] % 4096 == loads[0][3] % 4096, (
    'same-page RWE file/VMA congruence', loads)
print('MINILD_SEGMENT_CHECK=PASS mode=script-same-page RX+RW=RWE '
      'one-mapping page-compatible')


raw, ph, names = inspect('script-page-existing-rwx.elf', 2)
loads = [p for p in ph if p[0] == 1]
assert len(loads) == 3, ('already-RWX scripted LOAD count', ph)
assert [p[1] for p in loads] == [5, 7, 6], (
    'already-RWX permissions should be preserved without overlapping pages', ph)
rw = one(names, '.pi_rw')
rwx = one(names, '.pi_rwx')
assert rw[3] // 4096 == rwx[3] // 4096 and (
    covers(loads[1], rw) and covers(loads[1], rwx)), (
    'existing RWX fragment must share its already-RWX load page', loads, rw, rwx)
assert loads[1][2] % 4096 == loads[1][3] % 4096, (
    'merged RWX load file/VMA congruence', loads[1])
assert raw[rw[4]:rw[4] + 8] == (0x12345678).to_bytes(8, 'little')
assert raw[rwx[4]:rwx[4] + 8] == (0x88776655).to_bytes(8, 'little')
if (base / 'script-page-existing-rwx-gnu.elf').is_file():
    gnu_raw, gnu_ph, gnu_names = inspect('script-page-existing-rwx-gnu.elf', 2)
    gnu_rw = one(gnu_names, '.pi_rw')
    gnu_rwx = one(gnu_names, '.pi_rwx')
    assert any(p[0] == 1 and p[1] == 7 and
               covers(p, gnu_rw) and covers(p, gnu_rwx)
               for p in gnu_ph), (
        'GNU must agree that same-page already-RWX sections occupy one RWX LOAD',
        gnu_ph, gnu_rw, gnu_rwx)
    assert gnu_raw[gnu_rw[4]:gnu_rw[4] + 8] == raw[rw[4]:rw[4] + 8]
    assert gnu_raw[gnu_rwx[4]:gnu_rwx[4] + 8] == raw[rwx[4]:rwx[4] + 8]
print('MINILD_SEGMENT_CHECK=PASS mode=script-existing-RWX '
      'RW+RWX=same-page-coalesced GNU+QEMU=if-available')


raw, ph, names = inspect('static-default.elf', 2)
loads = [p for p in ph if p[0] == 1]
tls = [p for p in ph if p[0] == 7]
notes = [p for p in ph if p[0] == 4]
assert len(loads) == 2 and len(tls) == 1 and len(notes) == 1 and len(ph) == 4, ('ET_EXEC default PHDRs', ph)
assert notes[0][2] == one(names, '.note.minild')[4] and notes[0][5] == one(names, '.note.minild')[5], ('ET_EXEC PT_NOTE payload', notes)
assert loads[0][1] == 5 and loads[1][1] == 6 and all(p[7] == 4096 for p in loads)
# A plain ET_EXEC with a static CRT maps the ELF and PHDR bytes in its
# initial LOAD.  The linker-synthesized __ehdr_start must identify that
# actual mapped header, while the Linux-scripted path below retains its
# separate explicit section/segment placement contract.
assert loads[0][2] == 0 and loads[0][5] >= 64 + len(ph) * 56, (
    'static default ELF+PHDR must be mapped', loads[0])
assert loads[0][3] + 64 + len(ph) * 56 <= loads[0][3] + loads[0][6], (
    'static default ELF+PHDR memory extent', loads[0])
assert struct.unpack_from('<Q', raw, 32)[0] == 64, 'static default PHDR offset'
symtab = one(names, '.symtab')
strtab = one(names, '.strtab')
symnames = raw[strtab[4]:strtab[4] + strtab[5]]
ehdr_symbols = []
for offset in range(symtab[4], symtab[4] + symtab[5], 24):
    name, info, other, shndx, value, size = struct.unpack_from(
        '<IBBHQQ', raw, offset)
    if not name:
        continue
    end = symnames.find(b'\0', name)
    assert end > name, ('static default symbol string bounds', name)
    if symnames[name:end] == b'__ehdr_start':
        ehdr_symbols.append((shndx, value))
assert ehdr_symbols == [(0xfff1, loads[0][3])], (
    'linker-synthesized __ehdr_start must equal mapped ELF header',
    ehdr_symbols, loads[0])
assert covers(loads[0], one(names, '.text.start'))
assert covers(loads[1], one(names, '.data.word'))
assert tls[0][2] == one(names, '.tdata.slot')[4]
assert tls[0][3] == one(names, '.tdata.slot')[3]
assert tls[0][4] == tls[0][3] and tls[0][5] == 8 and tls[0][6] == 24 and tls[0][7] == 8
assert covers(loads[1], one(names, '.tdata.slot'))
assert covers(tls[0], one(names, '.tbss.slot'))
print('MINILD_SEGMENT_CHECK=PASS mode=static-default loads=2 '
      'PT_TLS=true mapped_ehdr_phdr=true ehdr_start=verified')

raw, ph, names = inspect('static-script.elf', 2)
loads = [p for p in ph if p[0] == 1]
tls = [p for p in ph if p[0] == 7]
notes = [p for p in ph if p[0] == 4]
assert len(loads) == 2 and all(
    p[7] == 4096 and p[2] % 4096 == p[3] % 4096 for p in loads
), ('script LOAD page congruence', ph)
assert len(tls) == 1 and len(notes) == 1 and len(ph) == 4, ('scripted PT_TLS+PT_NOTE', ph)
assert notes[0][2] == one(names, '.note')[4] and notes[0][5] == one(names, '.note')[5], ('scripted PT_NOTE payload', notes)
data = one(names, '.data')
assert any(covers(p, data) and p[4] != p[3] for p in loads), ('AT() LMA lost', ph)
assert any(covers(p, one(names, '.text')) for p in loads)
# The script places NOTE after .tbss in the same page. A distinct R-only
# PT_LOAD would remap the preceding RW .data/.tdata page read-only. The
# combined RW segment must materialize zero file padding for the SHT_NOBITS
# hole, without changing the section type or the independent PT_TLS filesz.
tbss = one(names, '.tbss')
assert tbss[1] == 8 and covers(loads[1], tbss), ('NOBITS section preserved', ph)
assert loads[1][1] == 6 and covers(loads[1], one(names, '.note')), (
    'same-page RW + NOTE merge', ph)
tbss_file = loads[1][2] + tbss[3] - loads[1][3]
assert raw[tbss_file:tbss_file + tbss[5]] == b'\0' * tbss[5], (
    'scripted BSS file padding', ph)
assert one(names, '.note')[5] == 20
assert tls[0][3] == one(names, '.tdata')[3]
assert tls[0][2] == one(names, '.tdata')[4]
assert tls[0][5] == 8 and tls[0][6] == 24 and tls[0][7] == 8
assert covers(tls[0], one(names, '.tbss'))
assert any(p[1] == 6 and covers(p, one(names, '.tdata')) and
           tls[0][4] - tls[0][3] == p[4] - p[3] for p in loads), (
    'scripted PT_TLS LMA must retain AT() mapping', ph)
print(f'MINILD_SEGMENT_CHECK=PASS mode=static-script loads={len(loads)} '
      'AT_LMA=true PT_TLS=true same_page_NOBITS_file_padding=true')

raw, ph, names = inspect('shared.so', 3)
loads = [p for p in ph if p[0] == 1]
dyn = [p for p in ph if p[0] == 2]
tls = [p for p in ph if p[0] == 7]
assert len(loads) == 2 and len(dyn) == 1 and len(tls) == 1, ('ET_DYN PHDRs', ph)
assert loads[0][2] == 0 and loads[0][3] == 0 and loads[0][5] >= 64 + len(ph) * 56
assert dyn[0][2] == one(names, '.dynamic')[4] and dyn[0][3] == one(names, '.dynamic')[3]
assert tls[0][5] > 0 and tls[0][6] > tls[0][5], ('TLS .tdata + .tbss', tls[0])
assert covers(tls[0], one(names, '.tdata.slot')) and covers(tls[0], one(names, '.tbss.slot'))
print('MINILD_SEGMENT_CHECK=PASS mode=shared PT_DYNAMIC=true PT_TLS=true')

raw, ph, names = inspect('shared-tls-empty-first.so', 3)
tls = [p for p in ph if p[0] == 7]
assert len(tls) == 1, ('empty-first TLS segment count', ph)
anchor = one(names, '.tdata.anchor')
value = one(names, '.tdata.value')
assert anchor[5] == 0 and value[5] == 8, ('TLS fixture section sizes', anchor, value)
assert anchor[3] < value[3], ('empty TLS must precede aligned nonempty TLS', anchor, value)
assert tls[0][3] == anchor[3], ('PT_TLS must begin at retained empty TLS section', tls[0], anchor)
rw_loads = [p for p in ph if p[0] == 1 and p[1] == 6]
assert len(rw_loads) == 1, ('TLS writable LOAD missing', ph)
# A zero-length section may have sh_offset=0 in the section table, even
# though its virtual address is inside the mapped TLS template.
assert tls[0][2] == rw_loads[0][2] + anchor[3] - rw_loads[0][3], (
    'PT_TLS file base must be derived from writable LOAD', tls[0], rw_loads[0])
assert tls[0][5] == tls[0][6] == value[3] + value[5] - anchor[3], (
    'PT_TLS span must include alignment gap', tls[0], anchor, value)


def tls_symbols(section_name, str_name):
    table = one(names, section_name)
    strings = one(names, str_name)
    names_blob = raw[strings[4]:strings[4] + strings[5]]
    records = {}
    for offset in range(table[4], table[4] + table[5], 24):
        name, info, _, shndx, address, size = struct.unpack_from('<IBBHQQ', raw, offset)
        if not name:
            continue
        end = names_blob.find(b'\0', name)
        assert end > name, ('TLS symbol name bounds', section_name, name)
        records[names_blob[name:end].decode()] = (info, shndx, address, size)
    return records


expected_tls_offsets = {'tls_anchor': 0, 'tls_value': value[3] - anchor[3]}
for table_name, str_name in (('.symtab', '.strtab'), ('.dynsym', '.dynstr')):
    records = tls_symbols(table_name, str_name)
    for name, expected in expected_tls_offsets.items():
        assert name in records, ('TLS symbol missing', table_name, name)
        info, shndx, offset, size = records[name]
        assert info & 0xf == 6 and shndx != 0, ('not a defined STT_TLS', table_name, name)
        assert offset == expected, ('TLS relative value mismatch', table_name, name,
                                     offset, expected)
print('MINILD_SEGMENT_CHECK=PASS mode=shared-tls-empty-first PT_TLS=retained-zero-origin'
      ' STT_TLS=static+dynamic')

raw, ph, names = inspect('static-tls-empty-first.elf', 2)
anchor = one(names, '.tdata.anchor')
value = one(names, '.tdata.value')
assert anchor[3] < value[3], ('static empty-first TLS section VMA order', anchor, value)
tls = [p for p in ph if p[0] == 7]
assert len(tls) == 1 and tls[0][3] == anchor[3] and tls[0][7] == 64, (
    'ET_EXEC empty-first PT_TLS origin/alignment', ph)
assert tls[0][5] == tls[0][6] == value[3] + value[5] - anchor[3]
for name, expected in {'tls_anchor': 0, 'tls_value': value[3] - anchor[3]}.items():
    info, shndx, offset, size = tls_symbols('.symtab', '.strtab')[name]
    assert info & 0xf == 6 and shndx != 0 and offset == expected, (
        'ET_EXEC TLS origin mismatch', name, offset, expected)
print('MINILD_SEGMENT_CHECK=PASS mode=static-tls-empty-first PT_TLS=retained-zero-origin STT_TLS=retained-zero-origin')

raw, ph, names = inspect('shared-tls-all-empty.so', 3)
assert not any(p[0] == 7 for p in ph), ('empty-only TLS incorrectly produced PT_TLS', ph)
assert one(names, '.tdata.empty')[5] == 0, 'empty TLS section not retained'
for table_name, strings_name in (('.symtab', '.strtab'), ('.dynsym', '.dynstr')):
    info, shndx, offset, size = tls_symbols(table_name, strings_name)['tls_empty']
    assert info & 0xf == 6 and shndx != 0 and offset == size == 0, (
        'empty-only TLS symbol mismatch', table_name, offset, size)
print('MINILD_SEGMENT_CHECK=PASS mode=shared-tls-all-empty PT_TLS=absent STT_TLS=defined')

raw, ph, names = inspect('pie.elf', 3)
loads = [p for p in ph if p[0] == 1]
phdr = [p for p in ph if p[0] == 6]
interp = [p for p in ph if p[0] == 3]
assert len(loads) == 2 and len(phdr) == 1 and len(interp) == 1
assert phdr[0][2] == 64 and phdr[0][5] == len(ph) * 56
assert phdr[0][3] == loads[0][3] + 64 and loads[0][2] == 0
assert interp[0][2] == one(names, '.interp')[4] and interp[0][3] == one(names, '.interp')[3]
assert raw[interp[0][2]:interp[0][2] + interp[0][5]] == b'/lib/ld-musl-riscv64.so.1\0'
print('MINILD_SEGMENT_CHECK=PASS mode=pie PT_PHDR=true PT_INTERP=true')

raw, ph, names = inspect('vdso.so', 3)
assert [p[0] for p in ph] == [1, 2, 4, 0x6474e550], ('vDSO PHDR order', ph)
assert ph[0][1] == 5 and ph[0][2] == ph[0][3] == 0
assert ph[0][5] >= 64 + len(ph) * 56
assert ph[1][1] == ph[2][1] == 4
assert ph[1][2] == one(names, '.dynamic')[4] and ph[1][3] == one(names, '.dynamic')[3]
assert ph[2][2] == one(names, '.note')[4] and ph[2][3] == one(names, '.note')[3]
assert ph[3][5] == ph[3][6] == 0, ('empty explicit EH header', ph[3])
print('MINILD_SEGMENT_CHECK=PASS mode=vdso PHDRS=4 empty_eh=true')

raw, ph, names = inspect('vdso-tls-discard.so', 3)
assert [p[0] for p in ph] == [1, 2, 4, 0x6474e550], (
    'discarded TLS must not synthesize PT_TLS', ph)
assert not any(p[0] == 7 for p in ph), ('unexpected discarded PT_TLS', ph)
for discarded in ('.tdata.slot', '.tbss.slot', '.data.word', '.tdata', '.tbss'):
    assert discarded not in names, ('discarded TLS/data leaked', discarded)
assert len([p for p in ph if p[0] == 1]) == 1
print('MINILD_SEGMENT_CHECK=PASS mode=vdso-tls-discard PT_TLS=absent')
print('MINILD_SEGMENT_CHECK=PASS mode=vdso-tls-live-mixed fail_closed=2')
PY

echo 'MINILD_SEGMENTS=PASS modes=11'
