#!/usr/bin/env bash
# A resolver/archive boundary regression: a mixed regular/thin archive group
# needs four selection rounds, while weak-only references must not pull in
# otherwise unused members.  Whole-archive and final script resolution cover
# the same candidate graph without requiring a cross compiler or QEMU.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"
for tool in ar python3; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "MINILD_ARCHIVE_RESOLUTION=INCONCLUSIVE missing_tool=$tool" >&2
        exit 8
    }
done

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-archive-resolution.XXXXXX")
finish() {
    if [[ ${MINILD_ARCHIVE_RESOLUTION_KEEP:-0} == 1 ]]; then
        echo "MINILD_ARCHIVE_RESOLUTION_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap finish EXIT
mkdir -p "$work/lib"

cat > "$work/root.s" <<'ASM'
.section .text.entry,"ax",@progbits
.globl _start
.type _start,@function
_start:
    call phase_d
    call chosen
    addi a0,zero,0
    addi a7,zero,93
    ecall
.size _start,.-_start

.section .data.refs,"aw",@progbits
.balign 8
.globl boundary_refs
.type boundary_refs,@object
boundary_refs:
    .dword phase_a
    .dword phase_b
    .dword phase_c
    .dword phase_d
    .dword chosen
    .dword only_weak_regular
    .dword only_weak_thin
    .dword common_state
    .dword script_mark
.size boundary_refs,72
.weak only_weak_regular
.weak only_weak_thin
ASM

cat > "$work/weak.s" <<'ASM'
.section .text.weak,"ax",@progbits
.weak chosen
.type chosen,@function
chosen:
    addi a0,zero,7
    ret
.size chosen,.-chosen
ASM

cat > "$work/strong.s" <<'ASM'
.section .text.strong,"ax",@progbits
.globl chosen
.type chosen,@function
chosen:
    addi a0,zero,42
    ret
.size chosen,.-chosen
ASM

# When a plain object follows an archive inside a group, its first visit may
# introduce an unresolved symbol that requires revisiting the earlier archive.
# The object itself must not be registered a second time on subsequent rounds.
cat > "$work/after-object.s" <<'ASM'
.section .text.after_object,"ax",@progbits
.globl after_root
.type after_root,@function
after_root:
    call phase_a
    ret
.size after_root,.-after_root
ASM

# Deliberately place providers in reverse dependency order: root -> D -> C
# -> B -> A.  A, B, C and D are visited in that order by each group round.
# Each round therefore uncovers one more archive's needed definition.
cat > "$work/lib/a.s" <<'ASM'
.section .text.phase_a,"ax",@progbits
.globl phase_a
.type phase_a,@function
phase_a:
    addi a0,zero,11
    ret
.size phase_a,.-phase_a
ASM

cat > "$work/lib/b.s" <<'ASM'
.section .text.phase_b,"ax",@progbits
.globl phase_b
.type phase_b,@function
phase_b:
    call phase_a
    ret
.size phase_b,.-phase_b
ASM

cat > "$work/lib/c.s" <<'ASM'
.section .text.phase_c,"ax",@progbits
.globl phase_c
.type phase_c,@function
phase_c:
    call phase_b
    ret
.size phase_c,.-phase_c
.comm common_state,24,8
ASM

cat > "$work/lib/d.s" <<'ASM'
.section .text.phase_d,"ax",@progbits
.globl phase_d
.type phase_d,@function
phase_d:
    call phase_c
    ret
.size phase_d,.-phase_d
ASM

cat > "$work/lib/unused-regular.s" <<'ASM'
.section .text.unused_regular,"ax",@progbits
.globl only_weak_regular
.type only_weak_regular,@function
only_weak_regular:
    ret
.size only_weak_regular,.-only_weak_regular
ASM

cat > "$work/lib/unused-thin.s" <<'ASM'
.section .text.unused_thin,"ax",@progbits
.globl only_weak_thin
.type only_weak_thin,@function
only_weak_thin:
    ret
.size only_weak_thin,.-only_weak_thin
ASM

cat > "$work/layout.lds" <<'LDS'
OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : { *(.text*) }
  .data : { *(.data*) }
  .bss : { *(.bss) *(COMMON) }
  PROVIDE(script_mark = 0x12345678);
}
LDS

for source in root weak strong after-object; do
    "$MINIAS" -march=rv64i -mabi=lp64 -o "$work/$source.o" "$work/$source.s"
done
for source in a b c d unused-regular unused-thin; do
    "$MINIAS" -march=rv64i -mabi=lp64 \
        -o "$work/lib/$source.o" "$work/lib/$source.s"
done

# Two archive storage formats alternate in one --start-group.  Creating the
# archives from relative paths exercises thin-member path ownership as well.
(
    cd "$work"
    ar rcsD liba.a lib/a.o lib/unused-regular.o
    ar rcsTD libb.a lib/b.o lib/unused-thin.o
    ar rcsD libc.a lib/c.o
    ar rcsTD libd.a lib/d.o
)
archives=("$work/liba.a" "$work/libb.a" "$work/libc.a" "$work/libd.a")

"$MINILD" -melf64lriscv -r -o "$work/group.o" \
    "$work/root.o" "$work/weak.o" "$work/strong.o" \
    --start-group "${archives[@]}" --end-group

"$MINILD" -melf64lriscv -static -T "$work/layout.lds" \
    -o "$work/script.elf" \
    "$work/root.o" "$work/weak.o" "$work/strong.o" \
    --start-group "${archives[@]}" --end-group

"$MINILD" -melf64lriscv -r -o "$work/whole.o" \
    "$work/root.o" "$work/weak.o" "$work/strong.o" \
    --whole-archive "${archives[@]}" --no-whole-archive

"$MINILD" -melf64lriscv -r -o "$work/archive-before-object.o" \
    --start-group "$work/liba.a" "$work/after-object.o" --end-group

# A second pass over the same archive group must not commit members already
# selected in the first group (including the weak-only candidates left out).
"$MINILD" -melf64lriscv -r -o "$work/repeated-group.o" \
    "$work/root.o" "$work/weak.o" "$work/strong.o" \
    --start-group "${archives[@]}" --end-group \
    --start-group "${archives[@]}" --end-group

python3 - "$work/group.o" "$work/script.elf" "$work/whole.o" \
    "$work/archive-before-object.o" "$work/repeated-group.o" <<'PY'
import struct
import sys
from pathlib import Path

def inspect(path):
    blob = Path(path).read_bytes()
    if blob[:6] != b'\x7fELF\x02\x01':
        raise SystemExit(f'{path}: expected little-endian ELF64')
    kind, machine = struct.unpack_from('<HH', blob, 16)
    if machine != 243 or kind not in (1, 2):
        raise SystemExit(f'{path}: expected RISC-V ET_REL or ET_EXEC')
    offset = struct.unpack_from('<Q', blob, 40)[0]
    stride, count, shstrings = struct.unpack_from('<HHH', blob, 58)
    if stride != 64 or count < 3 or shstrings >= count or offset + 64 * count > len(blob):
        raise SystemExit(f'{path}: malformed section headers')
    headers = [struct.unpack_from('<IIQQQQIIQQ', blob, offset + 64 * i)
               for i in range(count)]

    def section_bytes(header):
        start, size = header[4:6]
        if start + size > len(blob):
            raise SystemExit(f'{path}: section exceeds file')
        return blob[start:start + size]

    def cstring(pool, start):
        end = pool.find(b'\0', start)
        if start >= len(pool) or end < 0:
            raise SystemExit(f'{path}: invalid ELF string')
        return pool[start:end].decode()

    strings = section_bytes(headers[shstrings])
    sections = {cstring(strings, header[0]): (i, header)
                for i, header in enumerate(headers)}
    if '.symtab' not in sections:
        raise SystemExit(f'{path}: missing .symtab')
    symtab = sections['.symtab'][1]
    symbol_strings = section_bytes(headers[symtab[6]])
    raw = section_bytes(symtab)
    if len(raw) % 24:
        raise SystemExit(f'{path}: malformed .symtab')
    symbols = {}
    for position in range(0, len(raw), 24):
        nameoff, info, other, index, value, size = struct.unpack_from(
            '<IBBHQQ', raw, position)
        name = cstring(symbol_strings, nameoff)
        if name and not name.startswith('$') and info >> 4:
            if name in symbols:
                raise SystemExit(f'{path}: duplicate global {name}')
            symbols[name] = (info >> 4, index, value, size)
    return kind, blob, sections, symbols, section_bytes

for mode, path in zip(('group', 'script', 'whole', 'after-object', 'repeated-group'), sys.argv[1:]):
    kind, blob, sections, symbols, section_bytes = inspect(path)
    if kind != (2 if mode == 'script' else 1):
        raise SystemExit(f'{mode}: unexpected ELF output type {kind}')
    if mode == 'after-object':
        for name in ('after_root', 'phase_a'):
            if name not in symbols or symbols[name][1] == 0:
                raise SystemExit(f'{mode}: {name} not resolved on archive rescan')
        if 'only_weak_regular' in symbols:
            raise SystemExit(f'{mode}: unrelated member extracted on rescan')
        print('MINILD_ARCHIVE_RESOLUTION_CHECK=PASS mode=after-object '
              'archive_before_plain_object=1 rescan=1 object_once=1')
        continue
    for name in ('phase_a', 'phase_b', 'phase_c', 'phase_d', 'chosen'):
        if name not in symbols or symbols[name][1] in (0, 0xfff2):
            raise SystemExit(f'{mode}: {name} not resolved after group convergence')
    if symbols['chosen'][0] != 1:
        raise SystemExit(f'{mode}: strong candidate did not win over weak')
    weak_names = ('only_weak_regular', 'only_weak_thin')
    for name in weak_names:
        if name not in symbols:
            raise SystemExit(f'{mode}: missing weak reference {name}')
        if mode == 'whole':
            if symbols[name][1] == 0:
                raise SystemExit(f'{mode}: whole archive did not extract {name}')
        elif symbols[name][:2] != (2, 0):
            raise SystemExit(f'{mode}: weak-only reference extracted {name}')
    if mode != 'script':
        continue
    if symbols.get('script_mark', (None, None, None))[:3] != (1, 0xfff1, 0x12345678):
        raise SystemExit('script: PROVIDE symbol lost or moved')
    if '.bss' not in sections or 'common_state' not in symbols:
        raise SystemExit('script: COMMON allocation missing')
    bss_idx = sections['.bss'][0]
    common = symbols['common_state']
    if common[1] != bss_idx or common[3] != 24 or common[2] % 8:
        raise SystemExit('script: COMMON size, section or alignment wrong')
    if '.data' not in sections or 'boundary_refs' not in symbols:
        raise SystemExit('script: missing reference data')
    data_header = sections['.data'][1]
    refs = symbols['boundary_refs']
    data_offset = refs[2] - data_header[3]
    data = section_bytes(data_header)
    if refs[1] != sections['.data'][0] or data_offset < 0 or data_offset + 72 > len(data):
        raise SystemExit('script: reference data outside output section')
    values = struct.unpack_from('<9Q', data, data_offset)
    expected = tuple(symbols[name][2] for name in
                     ('phase_a', 'phase_b', 'phase_c', 'phase_d', 'chosen'))
    expected += (0, 0, common[2], 0x12345678)
    if values != expected:
        raise SystemExit(f'script: relocation references stale winner: {values} != {expected}')
    text_header = sections['.text'][1]
    text = section_bytes(text_header)
    chosen_offset = symbols['chosen'][2] - text_header[3]
    if chosen_offset < 0 or text[chosen_offset:chosen_offset + 4] != bytes.fromhex('1305a002'):
        raise SystemExit('script: weak body replaced strong winner')
    print('MINILD_ARCHIVE_RESOLUTION_CHECK=PASS mode=script '
          'mixed_fixed_point=4 weak_unresolved=2 strong_winner=1 '
          'common=24 provide=1 reloc_winner=1')
    continue

if Path(sys.argv[1]).read_bytes() != Path(sys.argv[5]).read_bytes():
    raise SystemExit('repeated-group: repeated archive groups changed output')
print('MINILD_ARCHIVE_RESOLUTION_CHECK=PASS mode=repeated-group '
      'selected_idempotent=1 byte_identical=1')
print('MINILD_ARCHIVE_RESOLUTION=PASS modes=group,script,whole,after-object,repeated-group '
      'archive_storage=regular+thin')
PY
