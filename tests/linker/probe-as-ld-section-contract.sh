#!/usr/bin/env bash
# MiniAS -> MiniLD RISC-V ELF input-section contract.  This deliberately
# exercises the producer/consumer boundary rather than only MiniAS output:
#   * duplicate COMDAT signatures across input objects
#   * different COMDAT signatures sharing one section name
#   * SHF_LINK_ORDER metadata and its GC dependency after an intermediate -r
#   * a second -r hop, proving that the first output is still a valid input
#
# GNU AS/LD are optional independent oracles.  In a cross-toolchain environment
# four producer/consumer combinations are run; locally MiniAS+MiniLD suffices.
# No Linux build, cached object pool, QEMU or full CI is required.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must name minic-as}"
: "${MINILD:?MINILD must name minic-ld}"
for required in python3 readelf; do
    command -v "$required" >/dev/null 2>&1 || {
        echo "MINILD_AS_SECTION_CONTRACT=INCONCLUSIVE missing_tool=$required" >&2
        exit 8
    }
done

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-as-section-contract.XXXXXX")
finish() {
    if [[ "${MINILD_SECTION_CONTRACT_KEEP:-0}" == 1 ]]; then
        echo "MINILD_AS_SECTION_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap finish EXIT

cat >"$work/dup_a.s" <<'ASM'
.section .text.choice,"axG",@progbits,choice,comdat
.globl choice
.type choice,@function
choice:
    addi a0,zero,11
    ret
.size choice,.-choice
.section .rodata.choice,"aG",@progbits,choice,comdat
.globl choice_data
choice_data:
    .dword choice
ASM

cat >"$work/dup_b.s" <<'ASM'
.section .text.choice,"axG",@progbits,choice,comdat
.globl choice
.type choice,@function
choice:
    addi a0,zero,22
    ret
.size choice,.-choice
.section .rodata.choice,"aG",@progbits,choice,comdat
.globl choice_data
choice_data:
    .dword choice
    # The losing COMDAT member must never make the final link resolve this.
    .dword discarded_dependency
ASM

cat >"$work/dup_entry.s" <<'ASM'
.section .text.entry,"ax",@progbits
.globl _start
.type _start,@function
_start:
    call choice
    addi a7,zero,93
    ecall
.size _start,.-_start
ASM

cat >"$work/distinct_a.s" <<'ASM'
.section .text.same,"axG",@progbits,first,comdat
.globl first
.type first,@function
first:
    addi a0,zero,3
    ret
.size first,.-first
.section .rodata.same,"aG",@progbits,first,comdat
    .dword first
ASM

cat >"$work/distinct_b.s" <<'ASM'
.section .text.same,"axG",@progbits,second,comdat
.globl second
.type second,@function
second:
    addi a0,zero,4
    ret
.size second,.-second
.section .rodata.same,"aG",@progbits,second,comdat
    .dword second
ASM

cat >"$work/distinct_entry.s" <<'ASM'
.section .text.entry,"ax",@progbits
.globl _start
.type _start,@function
_start:
    call first
    call second
    addi a7,zero,93
    ecall
.size _start,.-_start
ASM

cat >"$work/link_order.s" <<'ASM'
.section .text.live,"ax",@progbits
.globl live
.type live,@function
live:
    addi a0,zero,7
    ret
.size live,.-live
.section .meta.order,"ao",@progbits,live
    .word 0xa1b2c3d4
.section .meta.note,"o",@progbits,live
    .word 0x55aa66bb
.section .text.dead,"ax",@progbits
.globl dead
.type dead,@function
dead:
    addi a0,zero,8
    ret
.size dead,.-dead
.section .meta.order,"ao",@progbits,dead
    .word 0x12345678
.section .meta.note,"o",@progbits,dead
    .word 0xddaa99ee
ASM

cat >"$work/link_entry.s" <<'ASM'
.section .text.entry,"ax",@progbits
.globl _start
.type _start,@function
_start:
    call live
    addi a7,zero,93
    ecall
.size _start,.-_start
ASM

# Two independent COMDAT groups deliberately share *both* target and
# metadata section names.  Ordinary name-based coalescing would turn two
# distinct SHF_LINK_ORDER targets into one and silently corrupt sh_link.
# A non-allocating oG section adds the same dependency without SHF_ALLOC.
cat >"$work/same_a.s" <<'ASM'
.section .text.same,"axG",@progbits,first,comdat
.globl first
.type first,@function
first:
    addi a0,zero,31
    ret
.size first,.-first
.section .meta.same,"aoG",@progbits,first,first,comdat
    .word 0x11112222
.section .meta.note,"oG",@progbits,first,first,comdat
    .word 0xaaaabbbb
ASM

cat >"$work/same_b.s" <<'ASM'
.section .text.same,"axG",@progbits,second,comdat
.globl second
.type second,@function
second:
    addi a0,zero,32
    ret
.size second,.-second
.section .meta.same,"aoG",@progbits,second,second,comdat
    .word 0x33334444
.section .meta.note,"oG",@progbits,second,second,comdat
    .word 0xccccdddd
ASM

cat >"$work/same_entry.s" <<'ASM'
.section .text.entry,"ax",@progbits
.globl _start
.type _start,@function
_start:
    call first
    call second
    addi a7,zero,93
    ecall
.size _start,.-_start
ASM

# Structural ELF checks are architecture-independent, using host readelf and
# Python to inspect ELF64LE section headers.  No external RISC-V binutils are
# needed for the Mini-only path.
cat >"$work/check.py" <<'PY'
import re
import struct
import subprocess
import sys
from pathlib import Path

case, stage, file_name = sys.argv[1:]
path = Path(file_name)
data = path.read_bytes()
if data[:6] != b"\x7fELF\x02\x01":
    raise SystemExit(f"{path}: expected ELF64 little endian")
if struct.unpack_from("<H", data, 18)[0] != 243:
    raise SystemExit(f"{path}: expected RISC-V machine 243")
expected_type = 2 if stage == "final" else 1
if struct.unpack_from("<H", data, 16)[0] != expected_type:
    raise SystemExit(f"{path}: expected ELF type {expected_type}")

headers = subprocess.check_output(["readelf", "-W", "-S", str(path)], text=True)
symbols = subprocess.check_output(["readelf", "-W", "-s", str(path)], text=True)
sections = {}
for line in headers.splitlines():
    match = re.match(
        r"^\s*\[\s*(\d+)\]\s+(\S+)\s+(\S+)\s+"
        r"\S+\s+\S+\s+\S+\s+\S+\s+([A-Z]+)\s+"
        r"(\d+)\s+\d+\s+\d+", line
    )
    if match:
        index, name, kind, flags, link = match.groups()
        sections.setdefault(name, []).append((int(index), kind, flags, int(link)))

def section_bytes(section_index):
    """Read bytes from this exact ELF64 section, not a whole-file substring."""
    shoff = struct.unpack_from("<Q", data, 40)[0]
    shentsize, shcount = struct.unpack_from("<HH", data, 58)
    if shentsize != 64 or not 0 <= section_index < shcount:
        raise SystemExit(f"{path}: invalid section index {section_index}")
    header = shoff + shentsize * section_index
    if header + 64 > len(data):
        raise SystemExit(f"{path}: invalid section table range")
    offset, size = struct.unpack_from("<QQ", data, header + 24)
    if offset + size > len(data):
        raise SystemExit(f"{path}: invalid section byte range {section_index}")
    return data[offset:offset + size]

def section_groups():
    """Map COMDAT signatures to original section-header indices by name."""
    output = subprocess.check_output(
        ["readelf", "--section-groups", str(path)], text=True
    )
    result = {}
    signature = None
    for line in output.splitlines():
        header = re.search(r"COMDAT group section.*\[([^]]+)\] contains", line)
        if header:
            signature = header.group(1)
            result[signature] = {}
            continue
        member = re.match(r"^\s*\[\s*(\d+)\]\s+(\S+)", line)
        if signature is not None and member:
            result[signature].setdefault(member.group(2), []).append(int(member.group(1)))
    return result

def require_same_name_link_order(expected_signatures):
    # The grouped text and metadata names are all identical.  The group
    # membership must correlate each metadata sh_link with *its own* text
    # section, not merely with some same-named section in the file.
    groups = section_groups()
    if set(groups) != set(expected_signatures):
        raise SystemExit(f"{path}: unexpected COMDAT signatures {groups}")
    all_links = set()
    expected_payload = {
        "first": {".meta.same": b"\x22\x22\x11\x11",
                  ".meta.note": b"\xbb\xbb\xaa\xaa"},
        "second": {".meta.same": b"\x44\x44\x33\x33",
                   ".meta.note": b"\xdd\xdd\xcc\xcc"},
    }
    for signature in expected_signatures:
        group = groups[signature]
        if any(len(group.get(name, [])) != 1 for name in
               (".text.same", ".meta.same", ".meta.note")):
            raise SystemExit(f"{path}: incomplete {signature} group: {group}")
        text_index = group[".text.same"][0]
        all_links.add(text_index)
        for name, expected_alloc in ((".meta.same", True),
                                     (".meta.note", False)):
            meta_index = group[name][0]
            found = [entry for entry in sections.get(name, [])
                     if entry[0] == meta_index]
            if len(found) != 1:
                raise SystemExit(f"{path}: {signature} lost {name}: {found}")
            _, kind, flags, link = found[0]
            if kind != "PROGBITS" or "L" not in flags or "G" not in flags or \
               ("A" in flags) != expected_alloc or link != text_index:
                raise SystemExit(f"{path}: {signature} {name} sh_link={link},"
                                 f" expected {text_index} flags={flags}")
            if expected_payload[signature][name] not in section_bytes(meta_index):
                raise SystemExit(f"{path}: {signature} {name} payload lost"
                                 f" from section {meta_index}")
    if len(all_links) != len(expected_signatures):
        raise SystemExit(f"{path}: separate COMDATs collapsed their targets")

def defined(name):
    # readelf's last column is the symbol name; reject UND and duplicates.
    found = []
    for line in symbols.splitlines():
        columns = line.split()
        if len(columns) >= 8 and columns[-1] == name and columns[6] != "UND":
            found.append(columns)
    return found

def require_once(name):
    matches = defined(name)
    if len(matches) != 1:
        raise SystemExit(f"{path}: expected exactly one defined {name}, got {matches}")

if stage == "source":
    # Verify that MiniAS/GNU AS did not already lose the producer metadata.
    # The linked-output checks below are meaningful only if their inputs have
    # SHT_GROUP and SHF_LINK_ORDER in the first place.
    groups = subprocess.check_output(
        ["readelf", "--section-groups", str(path)], text=True
    )
    if case in {"duplicate", "distinct"} and not path.name.endswith("_entry.o"):
        if "COMDAT group section" not in groups:
            raise SystemExit(f"{path}: producer omitted SHT_GROUP")
    if case == "link_order" and path.name.endswith("-link_order.o"):
        meta = sections.get(".meta.order", [])
        notes = sections.get(".meta.note", [])
        live = sections.get(".text.live", [])
        dead = sections.get(".text.dead", [])
        linked = {link for _, _, flags, link in meta if "L" in flags}
        if len(meta) != 2 or linked != {live[0][0], dead[0][0]}:
            raise SystemExit(f"{path}: assembler lost SHF_LINK_ORDER {meta}")
        linked_notes = {link for _, _, flags, link in notes
                        if "L" in flags and "A" not in flags}
        if len(notes) != 2 or linked_notes != {live[0][0], dead[0][0]}:
            raise SystemExit(f"{path}: assembler lost nonalloc LINK_ORDER {notes}")
    if case == "same_link" and not path.name.endswith("_entry.o"):
        signature = "first" if path.name.endswith("_a.o") else "second"
        require_same_name_link_order([signature])
    print(f"MINILD_AS_SECTION_INPUT=PASS case={case} file={path.name}")
    sys.exit(0)

require_once("_start")
if case in {"duplicate", "distinct"}:
    if stage == "reloc":
        groups = subprocess.check_output(
            ["readelf", "--section-groups", str(path)], text=True
        )
        if case == "duplicate":
            expected_signatures = ["choice"]
            expected_members = {".text.choice": 1, ".rodata.choice": 1}
        else:
            expected_signatures = ["first", "second"]
            expected_members = {".text.same": 2, ".rodata.same": 2}
        for signature in expected_signatures:
            if len(re.findall(r"COMDAT group section[^\n]*\[" + signature + r"\]", groups)) != 1:
                raise SystemExit(f"{path}: missing/duplicate COMDAT [{signature}]\n{groups}")
        if len(re.findall(r"COMDAT group section", groups)) != len(expected_signatures):
            raise SystemExit(f"{path}: wrong COMDAT group count\n{groups}")
        for name, expected in expected_members.items():
            if len(sections.get(name, [])) != expected:
                raise SystemExit(f"{path}: {name} count {len(sections.get(name, []))} != {expected}")
    if case == "duplicate":
        require_once("choice")
        require_once("choice_data")
        if stage == "final":
            if b"\x13\x05\xb0\x00" not in data or b"\x13\x05\x60\x01" in data:
                raise SystemExit(f"{path}: wrong COMDAT winner; expected 11, not 22")
    else:
        require_once("first")
        require_once("second")
        if stage == "reloc":
            relocations = subprocess.check_output(
                ["readelf", "-W", "-r", str(path)], text=True
            )
            if "R_RISCV_64" not in relocations or not all(
                re.search(r"R_RISCV_64\s+\S+\s+" + name + r"(?:\s|$)", relocations)
                for name in ("first", "second")
            ):
                raise SystemExit(f"{path}: distinct COMDAT relocation targets merged or lost")
        if stage == "final":
            for instruction in (b"\x13\x05\x30\x00", b"\x13\x05\x40\x00"):
                if instruction not in data:
                    raise SystemExit(f"{path}: same-name COMDAT payload missing {instruction.hex()}")
elif case == "same_link":
    require_once("first")
    require_once("second")
    if stage == "reloc":
        require_same_name_link_order(["first", "second"])
        if len(sections.get(".text.same", [])) != 2:
            raise SystemExit(f"{path}: merged same-named link-order targets")
    else:
        targets = {name: int(defined(name)[0][6])
                   for name in ("first", "second")}
        by_index = {item[0]: item for members in sections.values()
                    for item in members}
        for name, target in targets.items():
            text_section = by_index.get(target)
            if text_section is None or "A" not in text_section[2] or \
               "X" not in text_section[2]:
                raise SystemExit(f"{path}: {name} is not in live executable text")
        for name, instruction in (("first", b"\x13\x05\xf0\x01"),
                                  ("second", b"\x13\x05\x00\x02")):
            if instruction not in section_bytes(targets[name]):
                raise SystemExit(f"{path}: lost executable {name} bytes")
        # GNU's final linker folds these two distinct .text.same inputs into
        # one .text, and likewise coalesces .meta.same/.meta.note, sh_link
        # referring to the common output.  In a linker retaining *separate*
        # output text sections, metadata must preserve a strict 1:1 link.
        for section_name, allocated, payloads in (
            (".meta.same", True,
             {"first": b"\x22\x22\x11\x11", "second": b"\x44\x44\x33\x33"}),
            (".meta.note", False,
             {"first": b"\xbb\xbb\xaa\xaa", "second": b"\xdd\xdd\xcc\xcc"}),
        ):
            metas = sections.get(section_name, [])
            if not metas:
                raise SystemExit(f"{path}: missing {section_name} output")
            matched_sections = {}
            for symbol, payload in payloads.items():
                matched = [index for index, kind, flags, link in metas
                           if kind == "PROGBITS" and "L" in flags and
                           ("A" in flags) == allocated and
                           link == targets[symbol] and
                           payload in section_bytes(index)]
                if len(matched) != 1:
                    raise SystemExit(f"{path}: {section_name} {symbol} lost"
                                     f" exact payload/sh_link: {metas}")
                matched_sections[symbol] = matched[0]
            if targets["first"] != targets["second"] and \
               matched_sections["first"] == matched_sections["second"]:
                raise SystemExit(f"{path}: {section_name} two distinct targets"
                                 " incorrectly share one metadata section")
else:
    require_once("live")
    meta = sections.get(".meta.order", [])
    notes = sections.get(".meta.note", [])
    if stage == "reloc":
        expected = {sections[".text.live"][0][0], sections[".text.dead"][0][0]}
        actual = {link for _, _, flags, link in meta if "L" in flags}
        if len(meta) != 2 or actual != expected:
            raise SystemExit(f"{path}: SHF_LINK_ORDER -r lost two target identities {meta}, {expected}")
        actual_notes = {link for _, _, flags, link in notes
                        if "L" in flags and "A" not in flags}
        if len(notes) != 2 or actual_notes != expected:
            raise SystemExit(f"{path}: nonalloc LINK_ORDER -r lost targets {notes}, {expected}")
        if not any(link == sections[".text.live"][0][0] and
                   b"\xbb\x66\xaa\x55" in section_bytes(index)
                   for index, _, _, link in notes) or not any(
                   link == sections[".text.dead"][0][0] and
                   b"\xee\x99\xaa\xdd" in section_bytes(index)
                   for index, _, _, link in notes):
            raise SystemExit(f"{path}: nonalloc LINK_ORDER -r mispaired payload/target")
    else:
        if len(meta) != 1:
            raise SystemExit(f"{path}: GC must retain only live metadata; got {meta}")
        index, _, flags, link = meta[0]
        # GNU final ld normally folds .text.live into output .text and
        # remaps sh_link to that output section; MiniLD's current GC may
        # retain a uniquely named .text.live fragment.  Both are valid.
        executable_targets = [
            section for name, members in sections.items()
            for section in members
            if name.startswith(".text") and section[1] == "PROGBITS"
            and "A" in section[2] and "X" in section[2]
        ]
        if "L" not in flags or not any(
            section[0] == link for section in executable_targets
        ):
            raise SystemExit(f"{path}: live link-order sh_link/flags invalid: {meta}, target {executable_targets}")
        if b"\xd4\xc3\xb2\xa1" not in data or b"\x78\x56\x34\x12" in data:
            raise SystemExit(f"{path}: link-order GC kept wrong payload")
        if len(notes) != 1 or "L" not in notes[0][2] or \
           "A" in notes[0][2] or notes[0][3] not in {
               section[0] for section in executable_targets
           } or b"\xbb\x66\xaa\x55" not in section_bytes(notes[0][0]) or \
           b"\xee\x99\xaa\xdd" in section_bytes(notes[0][0]):
            raise SystemExit(f"{path}: nonalloc LINK_ORDER GC retained dead metadata: {notes}")
        if defined("dead"):
            raise SystemExit(f"{path}: unreachable dead section/symbol survived GC")

print(f"MINILD_AS_SECTION_CHECK=PASS case={case} stage={stage} file={path.name}")
PY

# Single source of truth for each case's object order.  Duplicate COMDAT's
# first object must win; the distinct-signature case must keep both members.
case_files() {
    case "$1" in
        duplicate)  printf '%s\n' dup_a dup_b dup_entry ;;
        distinct)   printf '%s\n' distinct_a distinct_b distinct_entry ;;
        link_order) printf '%s\n' link_order link_entry ;;
        same_link)  printf '%s\n' same_a same_b same_entry ;;
        *) return 2 ;;
    esac
}

gnu_available=0
gnu_as=${RISCV_AS:-riscv64-linux-gnu-as}
gnu_ld=${RISCV_LD:-riscv64-linux-gnu-ld}
if command -v "$gnu_as" >/dev/null 2>&1 &&
   command -v "$gnu_ld" >/dev/null 2>&1; then
    gnu_available=1
fi

failures=0
for testcase in duplicate distinct link_order same_link; do
    readarray -t names < <(case_files "$testcase")
    for producer in mini gnu; do
        if [[ $producer == gnu && $gnu_available == 0 ]]; then
            continue
        fi
        for name in "${names[@]}"; do
            target="$work/${testcase}-${producer}-${name}.o"
            if [[ $producer == mini ]]; then
                "$MINIAS" -march=rv64i -mabi=lp64 \
                    -o "$target" "$work/$name.s" || exit 1
            else
                "$gnu_as" -march=rv64i -mabi=lp64 \
                    -o "$target" "$work/$name.s" || exit 1
            fi
            python3 "$work/check.py" "$testcase" source "$target"
        done
    done

    pairs=(mini:mini)
    if (( gnu_available )); then
        pairs+=(mini:gnu gnu:mini gnu:gnu)
    fi
    for pair in "${pairs[@]}"; do
        producer=${pair%%:*}
        consumer=${pair##*:}
        linker=$MINILD
        if [[ $consumer == gnu ]]; then linker=$gnu_ld; fi
        objects=()
        for name in "${names[@]}"; do
            objects+=("$work/${testcase}-${producer}-${name}.o")
        done
        output="$work/${testcase}-${producer}-${consumer}"
        # Verify direct final-link COMDAT selection and link-order GC as well
        # as the longer -r -> -r -> final route.  A consumer must not rely on
        # an intermediate relocatable link to select/discard input groups.
        if ! "$linker" -melf64lriscv -static --gc-sections -e _start \
            -o "$output.direct.elf" "${objects[@]}" \
            >"$output.direct.stdout" 2>"$output.direct.stderr"; then
            echo "MINILD_AS_SECTION_CONTRACT=BLOCKED case=$testcase pair=$pair stage=direct-final" >&2
            sed -n '1,12p' "$output.direct.stderr" >&2
            failures=$((failures + 1))
        elif ! python3 "$work/check.py" "$testcase" final "$output.direct.elf"; then
            echo "MINILD_AS_SECTION_CONTRACT=FAIL case=$testcase pair=$pair stage=direct-final-verify" >&2
            failures=$((failures + 1))
        fi
        if ! "$linker" -melf64lriscv -r -o "$output.r.o" \
            "${objects[@]}" >"$output.r.stdout" 2>"$output.r.stderr"; then
            echo "MINILD_AS_SECTION_CONTRACT=BLOCKED case=$testcase pair=$pair stage=reloc" >&2
            sed -n '1,12p' "$output.r.stderr" >&2
            failures=$((failures + 1))
            continue
        fi
        if ! python3 "$work/check.py" "$testcase" reloc "$output.r.o"; then
            echo "MINILD_AS_SECTION_CONTRACT=FAIL case=$testcase pair=$pair stage=reloc-verify" >&2
            failures=$((failures + 1))
            continue
        fi
        # A relocatable output is not just a final serialization format: it
        # must remain a first-class object containing enough ownership data
        # for another linker invocation to make the same decisions.
        if ! "$linker" -melf64lriscv -r -o "$output.rr.o" \
            "$output.r.o" >"$output.rr.stdout" 2>"$output.rr.stderr"; then
            echo "MINILD_AS_SECTION_CONTRACT=BLOCKED case=$testcase pair=$pair stage=reloc-again" >&2
            sed -n '1,12p' "$output.rr.stderr" >&2
            failures=$((failures + 1))
            continue
        fi
        if ! python3 "$work/check.py" "$testcase" reloc "$output.rr.o"; then
            echo "MINILD_AS_SECTION_CONTRACT=FAIL case=$testcase pair=$pair stage=reloc-again-verify" >&2
            failures=$((failures + 1))
            continue
        fi
        if ! "$linker" -melf64lriscv -static --gc-sections -e _start \
            -o "$output.elf" "$output.rr.o" \
            >"$output.final.stdout" 2>"$output.final.stderr"; then
            echo "MINILD_AS_SECTION_CONTRACT=BLOCKED case=$testcase pair=$pair stage=final" >&2
            sed -n '1,12p' "$output.final.stderr" >&2
            failures=$((failures + 1))
            continue
        fi
        if ! python3 "$work/check.py" "$testcase" final "$output.elf"; then
            echo "MINILD_AS_SECTION_CONTRACT=FAIL case=$testcase pair=$pair stage=final-verify" >&2
            failures=$((failures + 1))
            continue
        fi
        echo "MINILD_AS_SECTION_CASE=PASS case=$testcase producer=$producer linker=$consumer stages=4"
    done
done

# GNU AS may carry an extra anonymous STT_SECTION symbol against an SHT_GROUP
# header.  It describes metadata, not a projected input section or an extra
# COMDAT signature.  Synthesize that exact ELF edge on the MiniAS fixture as
# a host-only regression, so an absent cross-GNU-AS cannot hide the failure.
group_local="$work/same_link-mini-group-local.o"
group_invalid="$work/same_link-mini-group-invalid.o"
cp "$work/same_link-mini-same_a.o" "$group_local"
cp "$group_local" "$group_invalid"
python3 - "$group_local" "$group_invalid" <<'PY'
import struct
import sys
from pathlib import Path

for output, invalid in ((Path(sys.argv[1]), False),
                        (Path(sys.argv[2]), True)):
    image = bytearray(output.read_bytes())
    if image[:6] != b"\x7fELF\x02\x01":
        raise SystemExit("group-local fixture needs ELF64LE")
    shoff = struct.unpack_from("<Q", image, 40)[0]
    shentsize, count = struct.unpack_from("<HH", image, 58)
    if shentsize != 64:
        raise SystemExit("group-local fixture needs ELF64 section headers")
    groups, symtabs = [], []
    for index in range(count):
        kind = struct.unpack_from("<I", image, shoff + index * 64 + 4)[0]
        if kind == 17:  # SHT_GROUP
            groups.append(index)
        elif kind == 2:  # SHT_SYMTAB
            symtabs.append(index)
    if len(groups) != 1 or len(symtabs) != 1:
        raise SystemExit("unexpected group-local fixture input")
    header = shoff + symtabs[0] * 64
    offset, length = struct.unpack_from("<QQ", image, header + 24)
    entry_size = struct.unpack_from("<Q", image, header + 56)[0]
    signature = struct.unpack_from("<I", image,
                                   shoff + groups[0] * 64 + 44)[0]
    if entry_size != 24 or length % 24 != 0:
        raise SystemExit("unexpected group-local symbol table")
    for index in range(1, length // 24):
        entry = offset + index * 24
        name, info = struct.unpack_from("<IB", image, entry)
        value, size = struct.unpack_from("<QQ", image, entry + 8)
        if index != signature and name == 0 and info == 3 and \
           value == 0 and size == 0:  # local STT_SECTION
            struct.pack_into("<H", image, entry + 6, groups[0])
            if invalid:
                image[entry + 4] = 0  # NOTYPE is not a section symbol
            output.write_bytes(image)
            break
    else:
        raise SystemExit("no disposable section symbol in fixture")
PY

group_objects=("$group_local" "$work/same_link-mini-same_b.o"
               "$work/same_link-mini-same_entry.o")
group_base="$work/same_link-mini-group-local"
"$MINILD" -melf64lriscv -static --gc-sections -e _start \
    -o "$group_base.direct.elf" "${group_objects[@]}"
python3 "$work/check.py" same_link final "$group_base.direct.elf"
"$MINILD" -melf64lriscv -r -o "$group_base.r.o" "${group_objects[@]}"
python3 "$work/check.py" same_link reloc "$group_base.r.o"
"$MINILD" -melf64lriscv -r -o "$group_base.rr.o" "$group_base.r.o"
python3 "$work/check.py" same_link reloc "$group_base.rr.o"
"$MINILD" -melf64lriscv -static --gc-sections -e _start \
    -o "$group_base.elf" "$group_base.rr.o"
python3 "$work/check.py" same_link final "$group_base.elf"
if "$MINILD" -melf64lriscv -r -o "$work/group-invalid.o" \
    "$group_invalid" "$work/same_link-mini-same_b.o" \
    >"$work/group-invalid.stdout" 2>"$work/group-invalid.stderr" || \
   ! grep -q 'unsupported-symbol-section' "$work/group-invalid.stderr"; then
    echo "MINILD_AS_GROUP_SYMBOL=FAIL accepted malformed group-local symbol" >&2
    failures=$((failures + 1))
else
    echo "MINILD_AS_GROUP_SYMBOL=PASS metadata_local=ignored stages=4 malformed=fail_closed"
fi

if (( failures )); then
    echo "MINILD_AS_SECTION_CONTRACT=BLOCKED failures=$failures gnu_oracle=$gnu_available" >&2
    exit 1
fi
echo "MINILD_AS_SECTION_CONTRACT=PASS cases=4 gnu_oracle=$gnu_available"
