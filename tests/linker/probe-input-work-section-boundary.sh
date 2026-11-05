#!/usr/bin/env bash
# Source input-section identity vs mutable work-section order.  This combines
# distinct COMDAT groups with identical section names, SHF_LINK_ORDER source
# links, RX/RO/alignment reordering and relocations through -r -> -rr -> final.
# All objects are built by MiniAS; no GNU RISC-V tools or QEMU are required.
set -Eeuo pipefail
: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"
command -v python3 >/dev/null 2>&1 || {
    echo 'MINILD_INPUT_WORK=INCONCLUSIVE missing_tool=python3' >&2
    exit 8
}

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-input-work.XXXXXX")
finish() {
    if [[ ${MINILD_INPUT_WORK_KEEP:-0} == 1 ]]; then
        echo "MINILD_INPUT_WORK_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap finish EXIT

cat > "$work/a.s" <<'ASM'
.section .rodata.a,"a",@progbits
.balign 32
.globl data_a
data_a:
    .dword 0x1111222233334444
.section .text.work,"axG",@progbits,a_fn,comdat
.balign 64
.globl a_fn
.type a_fn,@function
a_fn:
    addi a0,zero,11
    ret
.size a_fn,.-a_fn
.section .meta.work,"aoG",@progbits,a_fn,a_fn,comdat
    .word 0x11112222
.section .meta.note,"oG",@progbits,a_fn,a_fn,comdat
    .word 0xaaaabbbb
.section .text.dead,"ax",@progbits
.globl dead_fn
dead_fn:
    call never_defined
    ret
ASM

# The duplicate signature loses before its bogus reference can enter the
# resolver or working relocation graph.
cat > "$work/loser.s" <<'ASM'
.section .text.work,"axG",@progbits,a_fn,comdat
.globl a_fn
.type a_fn,@function
a_fn:
    addi a0,zero,99
    call losing_undefined
    ret
.size a_fn,.-a_fn
.section .meta.work,"aoG",@progbits,a_fn,a_fn,comdat
    .word 0xdeadbeef
ASM

cat > "$work/b.s" <<'ASM'
.section .rodata.b,"a",@progbits
.balign 8
.globl data_b
data_b:
    .dword 0x5555666677778888
.section .text.work,"axG",@progbits,b_fn,comdat
.balign 16
.globl b_fn
.type b_fn,@function
b_fn:
    addi a0,zero,22
    ret
.size b_fn,.-b_fn
.section .meta.work,"aoG",@progbits,b_fn,b_fn,comdat
    .word 0x33334444
.section .meta.note,"oG",@progbits,b_fn,b_fn,comdat
    .word 0xccccdddd
.comm work_common,16,8
ASM

# Input order is RO(a), RX(a), RO(b), RX(b), RX(entry).  The final non-script
# GC path performs input restoration, RX/RO partition, then alignment sort.
cat > "$work/entry.s" <<'ASM'
.section .text.entry,"ax",@progbits
.globl _start
.type _start,@function
_start:
    call a_fn
    call b_fn
    lui t0,%hi(work_refs)
    addi t0,t0,%lo(work_refs)
    addi a0,zero,0
    addi a7,zero,93
    ecall
.size _start,.-_start
.section .data.refs,"aw",@progbits
.balign 8
.globl work_refs
.type work_refs,@object
work_refs:
    .dword a_fn
    .dword b_fn
    .dword data_a
    .dword data_b
    .dword work_common
.size work_refs,40
ASM

cat > "$work/link.lds" <<'LDS'
OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : { *(.text.entry) *(.text.work) }
  .rodata : { *(.rodata.*) }
  . = ALIGN(0x1000);
  .data : { *(.data.refs) }
  . = ALIGN(0x1000);
  .meta : { *(.meta.work) }
  .meta_note : { *(.meta.note) }
  . = ALIGN(0x1000);
  .bss : { *(.bss) *(COMMON) }
  /DISCARD/ : { *(.text.dead) }
}
LDS

for source in a loser b entry; do
    "$MINIAS" -march=rv64i -mabi=lp64 -o "$work/$source.o" "$work/$source.s"
done

"$MINILD" -melf64lriscv -r -o "$work/first.o" \
    "$work/a.o" "$work/loser.o" "$work/b.o" "$work/entry.o"
"$MINILD" -melf64lriscv -r -o "$work/second.o" "$work/first.o"

for origin in direct rr; do
    inputs=("$work/second.o")
    if [[ $origin == direct ]]; then
        inputs=("$work/a.o" "$work/loser.o" "$work/b.o" "$work/entry.o")
    fi
    "$MINILD" -melf64lriscv -static --gc-sections -e _start \
        -o "$work/$origin-default.elf" "${inputs[@]}"
    "$MINILD" -melf64lriscv -static --gc-sections -T "$work/link.lds" \
        -o "$work/$origin-script.elf" "${inputs[@]}"
done

python3 - "$work/first.o" "$work/second.o" \
    "$work/direct-default.elf" "$work/rr-default.elf" \
    "$work/direct-script.elf" "$work/rr-script.elf" <<'PY'
import struct
import sys
from pathlib import Path

def parse(path):
    blob = Path(path).read_bytes()
    if blob[:6] != b'\x7fELF\x02\x01':
        raise SystemExit(f'{path}: expected ELF64 little endian')
    kind, machine = struct.unpack_from('<HH', blob, 16)
    if machine != 243 or kind not in (1, 2):
        raise SystemExit(f'{path}: invalid ELF type or RISC-V target')
    shoff = struct.unpack_from('<Q', blob, 40)[0]
    stride, count, shstr = struct.unpack_from('<HHH', blob, 58)
    if stride != 64 or count < 3 or shstr >= count or shoff + count * 64 > len(blob):
        raise SystemExit(f'{path}: invalid section table')
    headers = [struct.unpack_from('<IIQQQQIIQQ', blob, shoff + 64 * i)
               for i in range(count)]

    def bytes_of(header):
        start, size = header[4:6]
        if start + size > len(blob):
            raise SystemExit(f'{path}: section payload out of bounds')
        return blob[start:start + size]

    def cstr(pool, start):
        end = pool.find(b'\0', start)
        if start >= len(pool) or end < 0:
            raise SystemExit(f'{path}: invalid ELF string')
        return pool[start:end].decode()

    names = bytes_of(headers[shstr])
    sections = {}
    for index, header in enumerate(headers):
        sections.setdefault(cstr(names, header[0]), []).append((index, header))
    symtab = sections['.symtab'][0][1]
    symstr = bytes_of(headers[symtab[6]])
    entries = bytes_of(symtab)
    if len(entries) % 24:
        raise SystemExit(f'{path}: invalid symtab')
    symbols = {}
    for offset in range(0, len(entries), 24):
        nameoff, info, other, index, value, size = struct.unpack_from(
            '<IBBHQQ', entries, offset)
        name = cstr(symstr, nameoff)
        if name and not name.startswith('$') and info >> 4:
            if name in symbols:
                raise SystemExit(f'{path}: duplicate canonical global {name}')
            symbols[name] = (info >> 4, index, value, size)
    return kind, blob, headers, sections, symbols, bytes_of

for stage, path in zip(('r', 'rr', 'direct-default', 'rr-default',
                        'direct-script', 'rr-script'), sys.argv[1:]):
    kind, blob, headers, sections, symbols, bytes_of = parse(path)
    if kind != (1 if stage in ('r', 'rr') else 2):
        raise SystemExit(f'{stage}: incorrect ET_REL/ET_EXEC type')
    for name in ('a_fn', 'b_fn', 'data_a', 'data_b', 'work_refs'):
        if name not in symbols or symbols[name][1] in (0, 0xfff2):
            raise SystemExit(f'{stage}: {name} missing after section remap')
    # An unused undefined name may remain in the symbol table after its
    # source relocation is discarded. Only the losing COMDAT's bytes and
    # relocation must disappear; a bare undefined name is not evidence that
    # the member was selected.
    if bytes.fromhex('efbeadde') in blob:
        raise SystemExit(f'{stage}: losing COMDAT metadata entered output')
    for name, expected in (('a_fn', bytes.fromhex('1305b000')),
                           ('b_fn', bytes.fromhex('13056001'))):
        symbol = symbols[name]
        section = headers[symbol[1]]
        base = section[3] if kind == 2 else 0
        start = symbol[2] - base
        if start < 0 or bytes_of(section)[start:start + 4] != expected:
            raise SystemExit(f'{stage}: source winner moved to wrong work bytes: {name}')
    if stage in ('r', 'rr'):
        # Identically named COMDAT members must remain distinct through both
        # partial-link hops; metadata links to the correct target section ID.
        if len(sections.get('.text.work', [])) != 2:
            raise SystemExit(f'{stage}: -r merged different COMDAT input identities')
        for section_name, payloads, alloc in (
            ('.meta.work', (('a_fn', '22221111'), ('b_fn', '44443333')), True),
            ('.meta.note', (('a_fn', 'bbbbaaaa'), ('b_fn', 'ddddcccc')), False),
        ):
            for name, marker in payloads:
                target = symbols[name][1]
                matching = [(idx, sh) for idx, sh in sections.get(section_name, [])
                            if sh[6] == target and bool(sh[2] & 2) == alloc
                            and bytes.fromhex(marker) in bytes_of(sh)]
                if len(matching) != 1:
                    raise SystemExit(f'{stage}: LINK_ORDER metadata lost owner '
                                     f'{section_name} -> {name}')
        print(f'MINILD_INPUT_WORK_CHECK=PASS stage={stage} distinct_groups=2 link_order=4')
        continue

    for name, align in (('a_fn', 64), ('b_fn', 16), ('data_a', 32)):
        if symbols[name][2] % align:
            raise SystemExit(f'{stage}: {name} work alignment lost')
    if 'work_common' not in symbols or symbols['work_common'][3] != 16:
        raise SystemExit(f'{stage}: COMMON allocation lost')
    if 'dead_fn' in symbols and symbols['dead_fn'][1] != 0:
        raise SystemExit(f'{stage}: dead source remained defined after GC')
    ref = symbols['work_refs']
    ref_section = headers[ref[1]]
    offset = ref[2] - ref_section[3]
    data = bytes_of(ref_section)
    if offset < 0 or offset + 40 > len(data):
        raise SystemExit(f'{stage}: source reference section not mapped')
    pointer_values = struct.unpack_from('<5Q', data, offset)
    expected = tuple(symbols[name][2] for name in
                     ('a_fn', 'b_fn', 'data_a', 'data_b', 'work_common'))
    if pointer_values != expected:
        raise SystemExit(f'{stage}: work permutation left stale relocation: '
                         f'{pointer_values} != {expected}')
    if stage.endswith('-default'):
        # No script: restore input order, stable RX-vs-RO partition, then
        # alignment sorting.  All live code must precede the referenced RO.
        text_end = max(symbols[name][2] + symbols[name][3]
                       for name in ('_start', 'a_fn', 'b_fn'))
        ro_start = min(symbols[name][2] for name in ('data_a', 'data_b'))
        if text_end > ro_start:
            raise SystemExit(f'{stage}: RX/RO work reorder regressed')
    # Full-link metadata is written after OutputSection ownership projection;
    # its sh_link must target live text even when two work targets fold into one.
    for payloads, alloc in (
        ((('a_fn', '22221111'), ('b_fn', '44443333')), True),
        ((('a_fn', 'bbbbaaaa'), ('b_fn', 'ddddcccc')), False),
    ):
        for name, marker in payloads:
            target = symbols[name][1]
            matches = [(idx, sh) for items in sections.values() for idx, sh in items
                       if sh[6] == target and bool(sh[2] & 2) == alloc
                       and (sh[2] & 0x80) and bytes.fromhex(marker) in bytes_of(sh)]
            if len(matches) != 1:
                raise SystemExit(f'{stage}: final LINK_ORDER output owner lost '
                                 f'{name} allocated={alloc}')
    print(f'MINILD_INPUT_WORK_CHECK=PASS stage={stage} '
          'work_remap=1 reloc_winner=5 link_order=4 common=16')

print('MINILD_INPUT_WORK=PASS stages=r,rr,direct-default,rr-default,direct-script,rr-script')
PY
