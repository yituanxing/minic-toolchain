#!/usr/bin/env python3
"""Audit the sole reassembled x86 boot header against frozen GNU inputs.

The original GNU header.o and a replay using frozen GNU zoffset.h must have
identical allocated bytes. A second replay with MiniLD-derived zoffset.h may
change only header.S-owned boot sections; fixed relocation tables, section
geometry, and every other allocated section must stay identical. This admits
the exact upstream Kbuild dependency without broadly allowing Linux C changes.
"""

from pathlib import Path
import re
import struct
import sys


class InvalidHeader(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise InvalidHeader(message)


def view(data, offset, size, reason):
    require(0 <= offset <= len(data) and 0 <= size <= len(data) - offset,
            "out-of-bounds:" + reason)
    return data[offset:offset + size]


def read_object(path):
    data = Path(path).read_bytes()
    require(len(data) >= 52 and data[:6] == b"\x7fELF\x01\x01",
            "not-little-endian-ELF32:" + path)
    (etype, machine, _, _, _, shoff, _, _, _, _, shentsize, shnum,
     strindex) = struct.unpack_from("<HHIIIIIHHHHHH", data, 16)
    require(etype == 1 and machine == 3 and shentsize == 40 and shnum > 0 and
            0 < strindex < shnum, "not-x86-ET_REL:" + path)
    view(data, shoff, 40 * shnum, "section-headers")
    sections = [struct.unpack_from("<IIIIIIIIII", data, shoff + 40 * i)
                for i in range(shnum)]
    _, kind, _, _, str_off, str_size, _, _, _, _ = sections[strindex]
    require(kind == 3, "missing-section-name-table")
    names = view(data, str_off, str_size, "section-names")
    result = {}
    by_index = {}
    for index, (nameoff, kind, flags, _, offset, size,
                link, _, align, _) in enumerate(sections):
        require(nameoff < len(names), "bad-section-name-offset")
        end = names.find(b"\0", nameoff)
        require(end >= 0, "unterminated-section-name")
        name = names[nameoff:end].decode("ascii")
        by_index[index] = name
        payload = b"" if kind == 8 else view(data, offset, size, name)
        if flags & 2:  # SHF_ALLOC
            require(name not in result, "duplicate-allocated-section:" + name)
            result[name] = (kind, flags, align, size, payload)
    relocs = {}
    symbols = {}
    for index, (nameoff, kind, _, _, offset, size,
                link, _, align, entsize) in enumerate(sections):
        name = by_index[index]
        if kind in (4, 9):  # RELA / REL, including alloc and debug fixups
            relocs[name] = view(data, offset, size, name)
        if kind != 2:  # SHT_SYMTAB
            continue
        require(entsize == 16 and size % 16 == 0 and link < shnum,
                "bad-symbol-table")
        _, strkind, _, _, stroff, strlen, _, _, _, _ = sections[link]
        require(strkind == 3, "bad-symbol-string-table")
        strings = view(data, stroff, strlen, "symbol-strings")
        for at in range(offset, offset + size, 16):
            st_name, st_value, _, _, _, st_shndx = struct.unpack_from(
                "<IIIBBH", data, at)
            require(st_name < len(strings), "bad-symbol-name")
            end = strings.find(b"\0", st_name)
            require(end >= 0, "unterminated-symbol-name")
            sym = strings[st_name:end].decode("ascii", "replace")
            if sym:
                symbols[sym] = (by_index.get(st_shndx), st_value)
    return result, relocs, symbols


def zoffsets(path):
    result = {}
    for line in Path(path).read_text().splitlines():
        matched = re.fullmatch(r"#define ZO_(\w+) 0x([0-9a-fA-F]+)", line)
        require(matched is not None, "invalid-zoffset-header:" + path)
        name, number = matched.groups()
        require(name not in result, "duplicate-zoffset:" + name)
        result[name] = int(number, 16)
    require(len(result) >= 10 and all(name in result for name in (
        'z_input_len', 'efi_pe_entry', 'input_data', '_text')),
        "missing-critical-zoffset")
    return result


def word(sections, symbols, name, addend=0):
    require(name in symbols, "missing-header-symbol:" + name)
    section, offset = symbols[name]
    require(section in sections, "bad-header-symbol-section:" + name)
    payload = sections[section][4]
    require(0 <= offset + addend <= len(payload) - 4,
            "bad-header-word:" + name)
    return struct.unpack_from("<I", payload, offset + addend)[0]


def zoffset_derived_deltas(old_values, new_values):
    mask = 0xffffffff
    differences = {(new_values[name] - old_values[name]) & mask
                   for name in old_values.keys() & new_values.keys()}
    # Linux header.S may emit INIT_SIZE: a derived page-rounded extraction
    # distance, not just a literal ZO_* value. Include that exact expression
    # from the upstream source, rather than admitting arbitrary byte edits.
    def init_size(values):
        extra = (values['z_output_len'] >> 8) + 131072
        distance = ((values['z_output_len'] + extra - values['z_input_len'])
                    if values['z_output_len'] > values['z_input_len']
                    else extra)
        head_size = values['_ehead'] - values['startup_32']
        if head_size > distance:
            distance = head_size
        return (values['_end'] - values['startup_32'] +
                ((distance + 4095) & ~4095))
    differences.add((init_size(new_values) - init_size(old_values)) & mask)
    differences.add((old_values['z_input_len'] -
                     new_values['z_input_len']) & mask)
    differences.discard(0)
    return differences


def check_derived_word_changes(name, old_bytes, new_bytes, deltas):
    changed = {i for i, (a, b) in enumerate(zip(old_bytes, new_bytes))
               if a != b}
    if not changed:
        return
    candidates = set()
    for offset in range(max(0, len(old_bytes) - 3)):
        old_word = struct.unpack_from('<I', old_bytes, offset)[0]
        new_word = struct.unpack_from('<I', new_bytes, offset)[0]
        if (new_word - old_word) & 0xffffffff in deltas:
            candidates.update(range(offset, offset + 4))
    unknown = changed - candidates
    require(not unknown,
            f'unexpected-header-byte-change:{name}:offset={min(unknown) if unknown else 0:#x}')


def main():
    if len(sys.argv) != 6:
        raise SystemExit("usage: header-audit.py GNU.o FROZEN_REPLAY.o "
                         "MINI_REPLAY.o GNU_ZOFFSET.h MINI_ZOFFSET.h")
    try:
        gnu_alloc, gnu_relocs, gnu_symbols = read_object(sys.argv[1])
        old_alloc, old_relocs, old_symbols = read_object(sys.argv[2])
        new_alloc, new_relocs, new_symbols = read_object(sys.argv[3])
        old_values, new_values = zoffsets(sys.argv[4]), zoffsets(sys.argv[5])
        require(gnu_alloc == old_alloc,
                "replayed-GNU-header-allocated-bytes-differ")
        require(gnu_relocs == old_relocs == new_relocs,
                "header-relocation-records-changed")
        require({key: value[:4] for key, value in old_alloc.items()} ==
                {key: value[:4] for key, value in new_alloc.items()},
                "header-allocated-section-geometry-changed")
        allowed = {'.bstext', '.pecompat', '.header', '.entrytext', '.initdata'}
        allowed_deltas = zoffset_derived_deltas(old_values, new_values)
        changed = []
        for name in old_alloc:
            if old_alloc[name][4] != new_alloc[name][4]:
                require(name in allowed, "unexpected-header-section-change:" + name)
                check_derived_word_changes(name, old_alloc[name][4],
                                           new_alloc[name][4], allowed_deltas)
                changed.append(name)
        old_placed = {key: value for key, value in old_symbols.items()
                      if value[0] in old_alloc}
        new_placed = {key: value for key, value in new_symbols.items()
                      if value[0] in new_alloc}
        require(old_placed == new_placed,
                "header-placed-symbol-identities-or-offsets-changed")
        require(word(old_alloc, old_symbols, 'payload_length') ==
                old_values['z_input_len'] & 0xffffffff,
                "GNU-header-payload-length-inconsistent")
        require(word(new_alloc, new_symbols, 'payload_length') ==
                new_values['z_input_len'] & 0xffffffff,
                "MiniLD-header-payload-length-inconsistent")
        old_pe = word(old_alloc, old_symbols, 'optional_header', 16)
        new_pe = word(new_alloc, new_symbols, 'optional_header', 16)
        require((new_pe - old_pe) & 0xffffffff ==
                (new_values['efi_pe_entry'] - old_values['efi_pe_entry']) & 0xffffffff,
                "EFI-entry-not-derived-from-current-zoffset")
        if old_values['z_input_len'] != new_values['z_input_len']:
            require(changed, "changed-piggy-with-unchanged-boot-header")
        print('MINILD_X86_HEADER_AUDIT=PASS '
              'frozen_GNU_replay=exact allocated_geometry=exact '
              'relocations=exact changed_sections=' + ','.join(sorted(changed)) +
              ' payload_length=' + hex(new_values['z_input_len']) +
              ' EFI_entry_delta=' +
              hex((new_pe - old_pe) & 0xffffffff))
    except (InvalidHeader, OSError, UnicodeError, struct.error) as exc:
        print('MINILD_X86_HEADER_AUDIT=FAIL ' + str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
