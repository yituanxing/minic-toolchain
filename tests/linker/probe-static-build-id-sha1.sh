#!/usr/bin/env bash
# GNU/BFD checksum semantics, not merely the presence of an ELF NOTE header.
set -Eeuo pipefail
: "${MINILD:?set MINILD to the minic-ld executable}"
for command in as ld readelf python3 cmp; do
    command -v "$command" >/dev/null || {
        echo "MINILD_BUILD_ID=INCONCLUSIVE missing=$command" >&2
        exit 8
    }
done
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-buildid.XXXXXX")
if [[ ${MINILD_BUILDID_KEEP:-0} == 1 ]]; then
    echo "MINILD_BUILDID_ARTIFACTS=$work"
else
    trap 'rm -r -- "$work"' EXIT
fi

cat >"$work/start.s" <<'ASM'
.section .text,"ax",@progbits
.globl _start
_start:
  mov $60,%eax
  xor %edi,%edi
  syscall
.section .note.demo,"a",@note
  .long 4,4,7
  .ascii "DEMO"
  .long 0x12345678
ASM
as -o "$work/start.o" "$work/start.s"
cat >"$work/build-id.lds" <<'LDS'
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)
PHDRS {
  text PT_LOAD FLAGS(5);
  note PT_NOTE FLAGS(4);
}
SECTIONS {
  . = 0x400000;
  .text : { *(.text) } :text
  . = ALIGN(4);
  .notes : { KEEP(*(.note.gnu.build-id)) *(.note.*) } :note
}
LDS

ld -m elf_x86_64 --build-id=sha1 \
    -o "$work/gnu-default.elf" "$work/start.o"
"$MINILD" -melf_x86_64 --build-id=sha1 \
    -o "$work/mini-default.elf" "$work/start.o"
"$MINILD" -melf_x86_64 --build-id=sha1 \
    -o "$work/mini-repeat.elf" "$work/start.o"
cmp "$work/mini-default.elf" "$work/mini-repeat.elf"

ld -m elf_x86_64 --build-id=sha1 -n -T "$work/build-id.lds" \
    -o "$work/gnu-script.elf" "$work/start.o"
"$MINILD" -melf_x86_64 --build-id=sha1 -n \
    -T "$work/build-id.lds" -o "$work/mini-script.elf" "$work/start.o"

# The note is an ELF-class/common-writer contract, not an x86-only feature.
# These cross-target objects deliberately contain no executable runtime path.
if [[ -n ${MINIAS:-} ]]; then
    for target in rv32 rv64 aarch64 loongarch64; do
        printf '%s\n' '.text' '.globl _start' '_start:' '.long 0' \
            >"$work/$target.s"
        case "$target" in
            rv32) machine=riscv; march=rv32i; mabi=ilp32; emulation=elf32lriscv ;;
            rv64) machine=riscv; march=rv64i; mabi=lp64; emulation=elf64lriscv ;;
            aarch64) machine=aarch64; march=armv8-a; mabi=lp64; emulation=aarch64linux ;;
            loongarch64) machine=loongarch64; march=loongarch64; mabi=lp64d; emulation=elf64loongarch ;;
        esac
        "$MINIAS" "--target=$machine" "-march=$march" "-mabi=$mabi" \
            -o "$work/$target.o" "$work/$target.s"
        "$MINILD" "-m$emulation" -static --build-id=sha1 -e _start \
            -o "$work/mini-$target.elf" "$work/$target.o"
    done
fi

python3 - "$work" <<'PY'
import hashlib
import pathlib
import struct
import sys


def check(path, explicit, validate_checksum):
    raw = bytearray(path.read_bytes())
    assert raw[:4] == b'\x7fELF' and raw[5] == 1, (path, 'little-endian ELF')
    elf64 = raw[4] == 2
    assert elf64 or raw[4] == 1, path
    ehdr = bytearray(raw[:64 if elf64 else 52])
    if elf64:
        phoff, shoff = struct.unpack_from('<QQ', raw, 32)
        phentsize, phnum, shentsize, shnum, shstrndx = struct.unpack_from('<HHHHH', raw, 54)
        sections = [struct.unpack_from('<IIQQQQIIQQ', raw, shoff + i * shentsize)
                    for i in range(shnum)]
    else:
        phoff, shoff = struct.unpack_from('<II', raw, 28)
        phentsize, phnum, shentsize, shnum, shstrndx = struct.unpack_from('<HHHHH', raw, 42)
        sections = [struct.unpack_from('<IIIIIIIIII', raw, shoff + i * shentsize)
                    for i in range(shnum)]
    names_section = sections[shstrndx]
    names = raw[names_section[4]:names_section[4] + names_section[5]]
    found = []
    for section in sections:
        name_off = section[0]
        name = names[name_off:names.find(b'\0', name_off)].decode()
        if name in ('.note.gnu.build-id', '.notes'):
            contents = raw[section[4]:section[4] + section[5]]
            position = contents.find(b'\x04\0\0\0\x14\0\0\0\x03\0\0\0GNU\0')
            if position >= 0:
                found.append(section[4] + position)
    assert len(found) == 1, (path, 'build-id NOTE count', found)
    note = found[0]
    digest = bytes(raw[note + 16:note + 36])
    assert len(digest) == 20 and digest != b'\0' * 20, (path, 'zero-id')
    raw[note + 16:note + 36] = b'\0' * 20
    struct.pack_into('<QQ' if elf64 else '<II', ehdr,
                     32 if elf64 else 28, 0, 0)
    sha1 = hashlib.sha1()
    sha1.update(ehdr)
    sha1.update(raw[phoff:phoff + phnum * phentsize])
    for i, section in enumerate(sections):
        wire = bytearray(raw[shoff + i * shentsize:shoff + (i + 1) * shentsize])
        struct.pack_into('<Q' if elf64 else '<I', wire,
                         24 if elf64 else 16, 0)
        sha1.update(wire)
        if section[1] != 8:
            sha1.update(raw[section[4]:section[4] + section[5]])
    if validate_checksum:
        assert digest == sha1.digest(), (path, 'non-BFD-checksum',
                                         digest.hex(), sha1.hexdigest())
    headers = [struct.unpack_from('<IIQQQQQQ' if elf64 else '<IIIIIIII',
                                  raw, phoff + i * phentsize)
               for i in range(phnum)]
    notes = [p for p in headers if p[0] == 4]
    assert notes, (path, 'PT_NOTE missing')
    assert any((p[2] if elf64 else p[1]) <= note and
               note + 36 <= (p[2] if elf64 else p[1]) +
               (p[5] if elf64 else p[4]) for p in notes), (path, 'PT_NOTE range')
    if explicit:
        assert len(notes) == 1 and notes[0][1 if elf64 else 6] == 4, (path, notes)
    else:
        assert len(notes) == (2 if 'default' in path.name else 1), (path, notes)
    return digest.hex()


root = pathlib.Path(sys.argv[1])
for file, explicit in [('gnu-default.elf', False),
                       ('mini-default.elf', False),
                       ('gnu-script.elf', True),
                       ('mini-script.elf', True)]:
    print(file, check(root / file, explicit, file.startswith('mini')))
for target in ('rv32', 'rv64', 'aarch64', 'loongarch64'):
    path = root / ('mini-' + target + '.elf')
    if path.exists():
        print(path.name, check(path, False, True))
print('MINILD_BUILD_ID=PASS mini_BFD_ordered_checksum=true sha1_deterministic=true '
      'PT_NOTE=true script_and_default=true')
PY

for unsupported in --build-id=md5 --build-id=none --build-id=0x1234; do
    if "$MINILD" -melf_x86_64 "$unsupported" \
        -o "$work/unsupported.elf" "$work/start.o" \
        2>"$work/unsupported.stderr"; then
        echo "MINILD_BUILD_ID=FAIL silently accepted $unsupported" >&2
        exit 1
    fi
done
echo 'MINILD_BUILD_ID_NEGATIVE=PASS unsupported_styles_fail_closed=true'
