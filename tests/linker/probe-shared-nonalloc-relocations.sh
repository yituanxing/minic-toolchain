#!/usr/bin/env bash
# ET_DYN non-SHF_ALLOC contents are final file bytes, not runtime relocation
# targets. Resolve their local references at link time without growing
# .rela.dyn or leaving an unpatched PC-relative instruction pair.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"
for tool in python3 readelf as ld; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "MINILD_SHARED_NONALLOC_RELOC=INCONCLUSIVE missing_tool=$tool" >&2
        exit 8
    fi
done

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-shared-nonalloc-relocs.XXXXXX")
finish() {
    if [[ ${MINILD_SHARED_NONALLOC_RELOC_KEEP:-0} == 1 ]]; then
        echo "MINILD_SHARED_NONALLOC_RELOC_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap finish EXIT

cat >"$work/native.s" <<'ASM'
.section .text.entry,"ax",@progbits
.balign 4
.globl entry
.type entry,@function
entry:
    addi a0,zero,19
    ret

# This input is intentionally not SHF_ALLOC.  The ordinary dynamic loader
# cannot and must not be asked to relocate it at runtime.
.section .debug_probe,"",@progbits
.balign 8
.globl debug_pointer
debug_pointer:
    .dword entry+8
.globl debug_pointer32
debug_pointer32:
    .word entry+4
.globl debug_pointer32_wrap
debug_pointer32_wrap:
    .word entry+4294967296
.globl debug_pointer32_negative
debug_pointer32_negative:
    .word entry-8192
.balign 4
.globl debug_pair
debug_pair:
    lla a0,entry
    .word 0x11223344
.globl debug_local_delta
debug_local_delta:
    .word debug_pair-debug_pointer
.globl debug_cross_delta
debug_cross_delta:
    .word debug_other_anchor - .

# Both owners have sh_addr=0; the symbol's section-local offset, and not
# its file offset or its containing section's placement, is its ELF value.
.section .debug_other,"",@progbits
    .zero 12
.globl debug_other_anchor
debug_other_anchor:
    .word 0x55667788
.section .note.minild,"a",@note
.balign 4
    .long 4,4,1
    .ascii "GNU\0"
    .long 0x11223344
ASM

cat >"$work/undefined.s" <<'ASM'
.section .text.entry,"ax",@progbits
.globl entry
entry:
    ret
.section .debug_probe,"",@progbits
    .dword missing_debug_definition
.section .note.minild,"a",@note
.balign 4
    .long 4,4,1
    .ascii "GNU\0"
    .long 0x11223344
ASM

# A GOT reference is a request for a live runtime address, whereas a
# non-SHF_ALLOC section has no runtime address. Reject it instead of
# manufacturing a misleading .got or dynamic relocation for debug bytes.
cat >"$work/unsupported-got.s" <<'ASM'
.section .text.entry,"ax",@progbits
.globl entry
entry:
    ret
.option pic
.section .debug_probe,"",@progbits
    la a0,entry
ASM

cat >"$work/weak.s" <<'ASM'
.section .text.entry,"ax",@progbits
.globl entry
entry:
    ret
.section .debug_probe,"",@progbits
.globl debug_pointer
debug_pointer:
    .dword missing_weak_debug_definition
.weak missing_weak_debug_definition
ASM

cat >"$work/host.s" <<'ASM'
.section .text.entry,"ax",@progbits
.globl entry
.type entry,@function
entry:
    ret
.section .debug_probe,"",@progbits
.balign 8
.globl debug_pointer
debug_pointer:
    .quad entry+8
ASM

"$MINIAS" -march=rv64i -mabi=lp64 -o "$work/native.o" "$work/native.s"
"$MINIAS" -march=rv64i -mabi=lp64 -o "$work/undefined.o" "$work/undefined.s"
"$MINIAS" -march=rv64i -mabi=lp64 -o "$work/unsupported-got.o" "$work/unsupported-got.s"
"$MINIAS" -march=rv64i -mabi=lp64 -o "$work/weak.o" "$work/weak.s"
as -o "$work/host.o" "$work/host.s"
ld -m elf_x86_64 -shared -o "$work/host.so" "$work/host.o"
readelf -Wr "$work/native.o" >"$work/native.relocs"
for name in R_RISCV_64 R_RISCV_32 R_RISCV_PCREL_HI20 R_RISCV_PCREL_LO12_I R_RISCV_ADD32 R_RISCV_SUB32; do
    if ! grep -q "$name" "$work/native.relocs"; then
        echo "MINILD_SHARED_NONALLOC_RELOC=FAIL missing_input_relocation=$name" >&2
        exit 1
    fi
done
if ! grep -q 'R_RISCV_ADD32.*debug_other_anchor' "$work/native.relocs"; then
    echo 'MINILD_SHARED_NONALLOC_RELOC=FAIL missing_cross_section_add32' >&2
    exit 1
fi
if ! grep -q 'R_RISCV_SUB32.*\.Lminias_private_' "$work/native.relocs"; then
    echo 'MINILD_SHARED_NONALLOC_RELOC=FAIL missing_cross_section_sub32' >&2
    exit 1
fi
if ! readelf -Wr "$work/unsupported-got.o" | grep -q 'R_RISCV_GOT_HI20'; then
    echo 'MINILD_SHARED_NONALLOC_RELOC=FAIL missing_input_relocation=R_RISCV_GOT_HI20' >&2
    exit 1
fi

if ! "$MINILD" -melf64lriscv -shared -o "$work/native.so" "$work/native.o" \
    >"$work/native.stdout" 2>"$work/native.stderr"; then
    echo 'MINILD_SHARED_NONALLOC_RELOC=FAIL native-link-rejected' >&2
    sed -n '1,15p' "$work/native.stderr" >&2
    exit 1
fi

cat >"$work/check.py" <<'PY'
import struct
import sys
from pathlib import Path

label, machine_text, pathname = sys.argv[1:]
machine = int(machine_text)
raw = Path(pathname).read_bytes()
if raw[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<HH', raw, 16) != (3, machine):
    raise SystemExit(f'{label}: wrong ET_DYN machine')
shoff = struct.unpack_from('<Q', raw, 40)[0]
entsize, count, strings_index = struct.unpack_from('<HHH', raw, 58)
if entsize != 64 or strings_index >= count or shoff + count * entsize > len(raw):
    raise SystemExit(f'{label}: malformed section headers')
headers = [struct.unpack_from('<IIQQQQIIQQ', raw, shoff + i * 64)
           for i in range(count)]
string_sh = headers[strings_index]
names = raw[string_sh[4]:string_sh[4] + string_sh[5]]

def name_at(strings, position):
    end = strings.find(b'\0', position)
    if end < 0:
        raise SystemExit(f'{label}: unterminated ELF string')
    return strings[position:end].decode('ascii')

by_name = {name_at(names, sh[0]): (idx, sh)
           for idx, sh in enumerate(headers)}

def required(name):
    try:
        return by_name[name]
    except KeyError:
        raise SystemExit(f'{label}: missing {name}') from None

_, debug = required('.debug_probe')
if debug[2] & 2 or debug[3] != 0:
    raise SystemExit(f'{label}: nonallocated debug section became SHF_ALLOC')
begin, size = debug[4:6]
if begin + size > len(raw):
    raise SystemExit(f'{label}: truncated debug section')
payload = raw[begin:begin + size]
if label in ('native', 'script'):
    phoff = struct.unpack_from('<Q', raw, 32)[0]
    phentsize, phnum = struct.unpack_from('<HH', raw, 54)
    if phoff and phentsize != 56:
        raise SystemExit(f'{label}: malformed program header entry size')
    for position in range(phnum):
        ptype, flags, file_off, virtual, physical, filesz, memsz, align = struct.unpack_from(
            '<IIQQQQQQ', raw, phoff + position * phentsize)
        if ptype == 1 and max(begin, file_off) < min(begin + size, file_off + filesz):
            raise SystemExit(f'{label}: nonalloc debug bytes accidentally mapped by PT_LOAD')

symtab_idx, symtab = required('.symtab')
strtab = headers[symtab[6]]
symnames = raw[strtab[4]:strtab[4] + strtab[5]]
symbols = {}
for position in range(0, symtab[5], 24):
    name, info, other, owner, value, length = struct.unpack_from(
        '<IBBHQQ', raw, symtab[4] + position)
    if name:
        symbols[name_at(symnames, name)] = (owner, value)
entry = symbols['entry'][1]
debug_index = by_name['.debug_probe'][0]
pointer_offset = symbols['debug_pointer'][1] - debug[3]
if symbols['debug_pointer'][0] != debug_index or pointer_offset < 0:
    raise SystemExit(f'{label}: debug symbol section mapping lost')
pointer = struct.unpack_from('<Q', payload, pointer_offset)[0]
expect_pointer = 0 if label == 'weak' else entry + 8
if pointer != expect_pointer:
    raise SystemExit(f'{label}: stale nonALLOC R_ABS64: got={pointer:#x} expected={expect_pointer:#x}')

if label in ('native', 'script'):
    ptr32_offset = symbols['debug_pointer32'][1] - debug[3]
    ptr32 = struct.unpack_from('<I', payload, ptr32_offset)[0]
    if ptr32 != entry + 4:
        raise SystemExit(f'{label}: stale R_RISCV_32: got={ptr32:#x} expected={entry+4:#x}')
    for symbol, expected in (
        ('debug_pointer32_wrap', (entry + (1 << 32)) & 0xffffffff),
        ('debug_pointer32_negative', (entry - 8192) & 0xffffffff),
    ):
        at = symbols[symbol][1] - debug[3]
        got = struct.unpack_from('<I', payload, at)[0]
        if got != expected:
            raise SystemExit(f'{label}: nonalloc R_RISCV_32 low-bit truncation {symbol}: '
                             f'got={got:#x} expected={expected:#x}')
    call_offset = symbols['debug_pair'][1] - debug[3]
    auipc, addi = struct.unpack_from('<II', payload, call_offset)
    if auipc & 0x7f != 0x17 or addi & 0x7f != 0x13:
        raise SystemExit(f'{label}: corrupted PC-relative pair')
    hi = auipc & 0xfffff000
    if hi & 0x80000000:
        hi -= 1 << 32
    lo = addi >> 20
    if lo & 0x800:
        lo -= 0x1000
    resolved = debug[3] + call_offset + hi + lo
    if resolved != entry:
        raise SystemExit(f'{label}: stale PCREL HI/LO: got={resolved:#x} expected={entry:#x}')
    if struct.unpack_from('<I', payload, call_offset + 8)[0] != 0x11223344:
        raise SystemExit(f'{label}: debug data corrupted after PCREL pair')
    local_offset = symbols['debug_local_delta'][1] - debug[3]
    actual_local = struct.unpack_from('<I', payload, local_offset)[0]
    expected_local = (call_offset - pointer_offset) & 0xffffffff
    if actual_local != expected_local:
        raise SystemExit(f'{label}: wrong section-local difference: {actual_local:#x} != {expected_local:#x}')
    _, other = required('.debug_other')
    other_idx = by_name['.debug_other'][0]
    if other[2] & 2 or other[3] != 0:
        raise SystemExit(f'{label}: cross-ref target has nonzero address or SHF_ALLOC')
    if label == 'script' and not other[4] < debug[4]:
        raise SystemExit(f'{label}: linker script did not order .debug_other before .debug_probe')
    if symbols['debug_other_anchor'][0] != other_idx:
        raise SystemExit(f'{label}: debug cross-reference owner lost')
    anchor_local = symbols['debug_other_anchor'][1] - other[3]
    if anchor_local != 12:
        raise SystemExit(f'{label}: wrong cross-reference section-local symbol value: {anchor_local:#x}')
    cross_offset = symbols['debug_cross_delta'][1] - debug[3]
    actual_cross = struct.unpack_from('<I', payload, cross_offset)[0]
    expected_cross = (anchor_local - cross_offset) & 0xffffffff
    if actual_cross != expected_cross:
        raise SystemExit(f'{label}: stale ADD32/SUB32 cross-ref: got={actual_cross:#x} expected={expected_cross:#x}')
    if struct.unpack_from('<I', raw, other[4] + anchor_local)[0] != 0x55667788:
        raise SystemExit(f'{label}: target debug section payload damaged')

if '.rela.dyn' in by_name and by_name['.rela.dyn'][1][5] != 0:
    raise SystemExit(f'{label}: debug-only relocation leaked into .rela.dyn')
if '.rela.debug_probe' in by_name:
    raise SystemExit(f'{label}: ET_DYN unexpectedly retained input debug relocation section')
print(f'MINILD_SHARED_NONALLOC_RELOC_CHECK=PASS case={label} '
      f'entry={entry:#x} debug_pointer={pointer:#x}' +
      (' pcrel=patched abs32=patched local-cross-difference=patched'
       if label in ('native', 'script') else ''))
PY

python3 "$work/check.py" host 62 "$work/host.so"
python3 "$work/check.py" native 243 "$work/native.so"

"$MINILD" -melf64lriscv -shared -o "$work/weak.so" "$work/weak.o" \
    >"$work/weak.stdout" 2>"$work/weak.stderr" || {
    echo 'MINILD_SHARED_NONALLOC_RELOC=FAIL undefined-weak-link-rejected' >&2
    sed -n '1,15p' "$work/weak.stderr" >&2
    exit 1
}
python3 "$work/check.py" weak 243 "$work/weak.so"

# An unresolved strong reference inside non-ALLOC bytes cannot be deferred to
# the runtime loader: those bytes are outside PT_LOAD and .rela.dyn.
if "$MINILD" -melf64lriscv -shared \
    -o "$work/undefined.so" "$work/undefined.o" \
    >"$work/undefined.stdout" 2>"$work/undefined.stderr"; then
    echo 'MINILD_SHARED_NONALLOC_RELOC=FAIL unresolved-nonalloc-linked' >&2
    exit 1
fi
if [[ -e "$work/undefined.so" ]]; then
    echo 'MINILD_SHARED_NONALLOC_RELOC=FAIL unresolved-nonalloc-output-retained' >&2
    exit 1
fi
echo 'MINILD_SHARED_NONALLOC_RELOC_CHECK=PASS case=undefined-strong rejection=explicit'

# A non-alloc section explicitly discarded by the user is dead. It must not
# create a section or cause diagnostics for its otherwise unresolved symbol.
cat >"$work/discard.lds" <<'LDS'
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
  .text : { *(.text.entry) } :text
  .dynamic : { *(.dynamic) } :text :dynamic
  /DISCARD/ : { *(.debug_probe) }
}
PHDRS {
  text PT_LOAD FLAGS(5) FILEHDR PHDRS;
  dynamic PT_DYNAMIC FLAGS(4);
  note PT_NOTE FLAGS(4);
  eh_frame_hdr PT_GNU_EH_FRAME;
}
VERSION {
  LINUX_4.15 {
    global: entry;
    local: *;
  };
}
LDS
"$MINILD" -melf64lriscv -shared -soname libnonalloc-discard.so -T "$work/discard.lds" \
    -o "$work/discard.so" "$work/undefined.o" \
    >"$work/discard.stdout" 2>"$work/discard.stderr" || {
    echo 'MINILD_SHARED_NONALLOC_RELOC=FAIL discard-dead-referer-rejected' >&2
    sed -n '1,15p' "$work/discard.stderr" >&2
    exit 1
}
if readelf -S "$work/discard.so" | grep -Eq '[[:space:]]\.debug_probe[[:space:]]'; then
    echo 'MINILD_SHARED_NONALLOC_RELOC=FAIL discard-debug-section-survived' >&2
    exit 1
fi
echo 'MINILD_SHARED_NONALLOC_RELOC_CHECK=PASS case=discard dead_reference=ignored'

# Explicit ET_DYN script section placement is a separate owner/offset path
# from the default orphan layout. Put the cross-reference target before the
# referer, checking both independently keep sh_addr=0 despite file offsets.
cat >"$work/layout.lds" <<'LDS'
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
  .text : { *(.text.entry) } :text
  .dynamic : { *(.dynamic) } :text :dynamic
  .debug_other : { *(.debug_other) }
  .debug_probe : { *(.debug_probe) }
}
PHDRS {
  text PT_LOAD FLAGS(5) FILEHDR PHDRS;
  dynamic PT_DYNAMIC FLAGS(4);
  note PT_NOTE FLAGS(4);
  eh_frame_hdr PT_GNU_EH_FRAME;
}
VERSION {
  LINUX_4.15 {
    global: entry;
    local: *;
  };
}
LDS
"$MINILD" -melf64lriscv -shared -soname libnonalloc-layout.so \
    -T "$work/layout.lds" -o "$work/script.so" "$work/native.o" \
    >"$work/script.stdout" 2>"$work/script.stderr" || {
    echo 'MINILD_SHARED_NONALLOC_RELOC=FAIL explicit-nonalloc-layout-rejected' >&2
    sed -n '1,15p' "$work/script.stderr" >&2
    exit 1
}
python3 "$work/check.py" script 243 "$work/script.so"

if "$MINILD" -melf64lriscv -shared \
    -o "$work/unsupported-got.so" "$work/unsupported-got.o" \
    >"$work/unsupported-got.stdout" 2>"$work/unsupported-got.stderr"; then
    echo 'MINILD_SHARED_NONALLOC_RELOC=FAIL unsupported-nonalloc-GOT-linked' >&2
    exit 1
fi
if [[ -e "$work/unsupported-got.so" ]]; then
    echo 'MINILD_SHARED_NONALLOC_RELOC=FAIL unsupported-nonalloc-GOT-output-retained' >&2
    exit 1
fi
echo 'MINILD_SHARED_NONALLOC_RELOC_CHECK=PASS case=unsupported-GOT rejection=explicit'
echo 'MINILD_SHARED_NONALLOC_RELOC=PASS cases=native,host,weak,undefined,discard,script,unsupported-GOT'
