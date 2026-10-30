#!/usr/bin/env bash
# ELF Section identity is (name, group, unique ID, link-order association).
# Test actual GNU `unique,<number>` and `?` inheritance, not merely many
# different section names. GNU is an independent object-level oracle.
set -Eeuo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mini=${MINIAS:-"$root/build/release/bin/minic-as"}
test -x "$mini"
command -v as >/dev/null
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

cat >"$work/unique.s" <<'ASM_UNIQUE'
.section .text.same_unique,"ax",@progbits
.byte 0x65
.section .text.same_unique,"ax",@progbits,unique,0
.byte 0x10
.section .text.same_unique,"ax",@progbits,unique,1
.byte 0x21
.section .text.same_unique,"ax",@progbits,unique,2
.byte 0x32
.section .text.same_unique,"ax",@progbits,unique,4294967295
.byte 0x76
.section .text.same_unique,"ax",@progbits,unique,1
.byte 0x43
.section .text.same_unique,"ax",@progbits,unique,0
.byte 0x54
ASM_UNIQUE

cat >"$work/inherited_group.s" <<'ASM_GROUP'
.section .text.same_group,"axG",@progbits,group_alpha,comdat,unique,0
anchor_alpha:
  ret
.pushsection .meta.same,"a?",@progbits
.byte 0xa1
.popsection
.pushsection .meta.link,"ao?",@progbits,anchor_alpha
.byte 0xa2
.popsection
.section .text.same_group,"axG",@progbits,group_beta,comdat,unique,1
anchor_beta:
  nop
.pushsection .meta.same,"a?",@progbits
.byte 0xb1
.popsection
.pushsection .meta.link,"ao?",@progbits,anchor_beta
.byte 0xb2
.popsection
ASM_GROUP

# GNU accepts a section group without COMDAT linkage. This separately
# tests whether the ObjectModel preserves group flags instead of unconditionally
# serializing GRP_COMDAT for every group.
cat >"$work/plain_group.s" <<'ASM_PLAIN'
.section .meta.plain,"aG",@progbits,group_plain
.byte 0x5b
.pushsection .meta.inherited_plain,"a?",@progbits
.byte 0x60
.popsection
.section .meta.plain,"aG",@progbits,group_comdat,comdat
.byte 0x5c
.section .meta.additional,"aG",@progbits,group_plain
.byte 0x5d
# GNU upgrades a mixed-linkage group to COMDAT; don't invent two SHT_GROUPs
# for the same signature or drop a member from either input declaration.
.section .meta.promoted,"aG",@progbits,group_upgraded
.byte 0x5e
.section .meta.promoted_second,"aG",@progbits,group_upgraded,comdat
.byte 0x5f
ASM_PLAIN

for case_name in unique inherited_group plain_group; do
    if ! as --64 -o "$work/$case_name.gnu.o" "$work/$case_name.s" \
        2>"$work/$case_name.gnu.log"; then
        sed -n '1,40p' "$work/$case_name.gnu.log" >&2
        exit 1
    fi
    if [[ $case_name == plain_group ]]; then
        grep -q 'assuming all members of group.*are COMDAT' \
            "$work/$case_name.gnu.log"
    fi
    ld -r -o "$work/$case_name.gnu-linked.o" "$work/$case_name.gnu.o"
    if "$mini" --target=x86_64 -o "$work/$case_name.mini.o" \
        "$work/$case_name.s" >"$work/$case_name.mini.log" 2>&1; then
        if ! ld -r -o "$work/$case_name.mini-linked.o" \
            "$work/$case_name.mini.o" >"$work/$case_name.mini-link.log" 2>&1; then
            printf 'MINIAS_SECTIONKEY_LINKED=NO case=%s diagnostic=%s\n' \
                "$case_name" "$(head -n 1 "$work/$case_name.mini-link.log")"
        fi
        printf 'MINIAS_SECTIONKEY_ASSEMBLED=YES case=%s\n' "$case_name"
    else
        printf 'MINIAS_SECTIONKEY_ASSEMBLED=NO case=%s diagnostic=%s\n' \
            "$case_name" "$(head -n 1 "$work/$case_name.mini.log")"
    fi
done

# This is a documented, non-wrapping 32-bit GNU ID. Both assemblers must
# reject the next integer, not silently alias it to ID zero.
printf '.section .text.bad_unique,"ax",@progbits,unique,4294967296\n.byte 1\n' \
    >"$work/unique-overflow.s"
if as --64 -o "$work/unique-overflow.gnu.o" \
    "$work/unique-overflow.s" >"$work/overflow.gnu.log" 2>&1; then
    echo 'GNU unexpectedly accepted out-of-range Section unique ID' >&2
    exit 1
fi
if "$mini" --target=x86_64 -o "$work/unique-overflow.mini.o" \
    "$work/unique-overflow.s" >"$work/overflow.mini.log" 2>&1; then
    echo 'MiniAS unexpectedly accepted out-of-range Section unique ID' >&2
    exit 1
fi
test ! -e "$work/unique-overflow.mini.o"
echo 'MINIAS_SECTIONKEY_UNIQUE_NEGATIVE=PASS overflow_u32=1'

# A different declaration of the *same* SectionKey cannot change linkage;
# this differs from distinct members of one group, whose COMDAT policy GNU
# promotes collectively (tested in plain_group above).
cat >"$work/conflicting-group.s" <<'ASM_BAD_GROUP'
.section .meta.conflict,"aG",@progbits,group_conflict
.byte 1
.section .meta.conflict,"aG",@progbits,group_conflict,comdat
.byte 2
ASM_BAD_GROUP
if as --64 -o "$work/conflicting-group.gnu.o" \
    "$work/conflicting-group.s" >"$work/conflicting-group.gnu.log" 2>&1; then
    echo 'GNU unexpectedly accepted conflicting same-Section linkage' >&2
    exit 1
fi
if "$mini" --target=x86_64 -o "$work/conflicting-group.mini.o" \
    "$work/conflicting-group.s" >"$work/conflicting-group.mini.log" 2>&1; then
    echo 'MiniAS unexpectedly accepted conflicting same-Section linkage' >&2
    exit 1
fi
test ! -e "$work/conflicting-group.mini.o"
echo 'MINIAS_SECTIONKEY_GROUP_NEGATIVE=PASS conflicting_same_key=1'

python3 - "$work" <<'PY'
import pathlib
import struct
import sys

root = pathlib.Path(sys.argv[1])


def parse_elf(path):
    image = path.read_bytes()
    if image[:6] != b'\x7fELF\x02\x01':
        raise AssertionError(f'{path}: expected ELF64 little-endian')
    elf = struct.unpack_from('<16sHHIQQQIHHHHHH', image)
    offset, stride, count, shstrndx = elf[6], elf[11], elf[12], elf[13]
    raw = [struct.unpack_from('<IIQQQQIIQQ', image, offset + i * stride)
           for i in range(count)]

    def section_data(index):
        section = raw[index]
        return b'' if section[1] == 8 else image[section[4]:section[4] + section[5]]

    names = section_data(shstrndx)

    def string(table, index):
        end = table.index(b'\0', index)
        return table[index:end].decode('utf-8')

    sections = []
    for entry in raw:
        sections.append(dict(name=string(names, entry[0]), type=entry[1],
                             flags=entry[2], link=entry[6], info=entry[7],
                             data=section_data(len(sections))))

    groups = {}
    for section in sections:
        if section['type'] != 17:  # SHT_GROUP
            continue
        symtab = sections[section['link']]
        strtab = sections[raw[section['link']][6]]['data']
        sym_offset = section['info'] * raw[section['link']][9]
        symbol_name = struct.unpack_from('<I', symtab['data'], sym_offset)[0]
        signature = string(strtab, symbol_name)
        words = struct.unpack('<' + 'I' * (len(section['data']) // 4),
                              section['data'])
        if signature in groups:
            raise AssertionError(f'{path}: duplicate group {signature}')
        groups[signature] = dict(flags=words[0],
                                 members=set(words[1:]))
    return sections, groups


def check_unique(sections, groups):
    del groups
    values = [s['data'].hex() for s in sections
              if s['name'] == '.text.same_unique']
    assert values == ['65', '1054', '2143', '32', '76'], values
    return 'same_name=5 ordinary_distinct_from_unique0=1 unique_ids=0,1,2,UINT32_MAX reopen=2'


def check_inherited_group(sections, groups):
    wanted = {
        'group_alpha': {('.text.same_group', 'c3'),
                        ('.meta.same', 'a1'), ('.meta.link', 'a2')},
        'group_beta': {('.text.same_group', '90'),
                       ('.meta.same', 'b1'), ('.meta.link', 'b2')},
    }
    assert set(groups) == set(wanted), groups
    for signature, expected in wanted.items():
        group = groups[signature]
        assert group['flags'] == 1, (signature, group['flags'])
        member_ids = group['members']
        members = {(sections[i]['name'], sections[i]['data'].hex())
                   for i in member_ids}
        assert members == expected, (signature, members, expected)
        owners = [i for i in member_ids
                  if sections[i]['name'] == '.text.same_group']
        links = [i for i in member_ids
                 if sections[i]['name'] == '.meta.link']
        assert len(owners) == len(links) == 1, (signature, owners, links)
        assert sections[links[0]]['link'] == owners[0], signature
        assert sections[links[0]]['flags'] & 0x280 == 0x280, signature
        assert any(sections[i]['name'] == '.meta.same' and
                   sections[i]['flags'] & 0x200 for i in member_ids), signature
    return 'comdat_groups=2 inherited_members=4 push_pop=4 link_order=2'


def check_plain_group(sections, groups):
    expected = {'group_plain': (0, {('.meta.plain', '5b'),
                                    ('.meta.inherited_plain', '60'),
                                    ('.meta.additional', '5d')}),
                'group_comdat': (1, {('.meta.plain', '5c')}),
                'group_upgraded': (1, {('.meta.promoted', '5e'),
                                       ('.meta.promoted_second', '5f')})}
    assert set(groups) == set(expected), groups
    for signature, (flags, wanted) in expected.items():
        group = groups[signature]
        assert group['flags'] == flags, (signature, group)
        members = {(sections[i]['name'], sections[i]['data'].hex())
                   for i in group['members']}
        assert members == wanted, (signature, members, wanted)
    return 'non_comdat=1 comdat=1 mixed_linkage_promoted=1 inherited_plain=1 same_name_distinct=1'


tests = [('unique', check_unique),
         ('inherited_group', check_inherited_group),
         ('plain_group', check_plain_group)]
failures = []
for name, checker in tests:
    gnu_sections, gnu_groups = parse_elf(root / f'{name}.gnu.o')
    summary = checker(gnu_sections, gnu_groups)
    print(f'GNU_SECTIONKEY_ORACLE=PASS case={name} {summary}')
    candidate = root / f'{name}.mini.o'
    if not candidate.exists():
        failures.append(f'{name}:assemble')
        print(f'MINIAS_SECTIONKEY_ORACLE=FAIL case={name} reason=assembly_rejected')
        continue
    try:
        mini_sections, mini_groups = parse_elf(candidate)
        checker(mini_sections, mini_groups)
    except (AssertionError, IndexError, KeyError, ValueError, struct.error) as exc:
        failures.append(f'{name}:object_semantics')
        print(f'MINIAS_SECTIONKEY_ORACLE=FAIL case={name} reason={exc}')
    else:
        if not (root / f'{name}.mini-linked.o').exists():
            failures.append(f'{name}:gnu_ld_r')
            print(f'MINIAS_SECTIONKEY_ORACLE=FAIL case={name} reason=gnu_ld_r')
            continue
        print(f'MINIAS_SECTIONKEY_ORACLE=PASS case={name}')
if failures:
    print('MINIAS_SECTIONKEY_GNU=FAIL failures=' + ','.join(failures))
    sys.exit(1)
print('MINIAS_SECTIONKEY_GNU=PASS cases=3 gnu_ld_r=3 minias_ld_r=3')
PY
