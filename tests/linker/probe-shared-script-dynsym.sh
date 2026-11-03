#!/usr/bin/env bash
# ET_DYN linker-script symbols must be materialized before dynamic symbol,
# version, relocation and hash metadata are frozen.  Uses native RV64
# MiniAS/MiniLD and an independently assembled GNU ld.bfd host oracle.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"
for tool in python3 readelf; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "MINILD_SCRIPT_DYNSYM=INCONCLUSIVE missing_tool=$tool" >&2
        exit 8
    }
done

if [[ $(uname -m) != x86_64 ]] || ! command -v as >/dev/null 2>&1 || \
        ! command -v ld >/dev/null 2>&1; then
    echo 'MINILD_SCRIPT_DYNSYM=INCONCLUSIVE missing_host_GNU_x86_64_oracle' >&2
    exit 8
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-script-dynsym.XXXXXX")
cleanup() {
    if [[ ${MINILD_SCRIPT_DYNSYM_KEEP:-0} == 1 ]]; then
        echo "MINILD_SCRIPT_DYNSYM_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap cleanup EXIT

cat >"$work/native.s" <<'ASM'
.option pic
.section .text.probe,"ax",@progbits
.balign 4
.globl probe_entry
.type probe_entry,@function
probe_entry:
    call script_defined
    call script_override
    call script_hidden
    la a0, script_defined
    la a1, script_override
    la a2, script_hidden
    ret
.size probe_entry,.-probe_entry

.section .data.refs,"aw",@progbits
.balign 8
.globl probe_refs
.type probe_refs,@object
probe_refs:
    .dword script_defined
    .dword script_abs
    .dword script_local
    .dword script_provided
    .dword script_override
    .dword script_hidden
.size probe_refs,.-probe_refs

.globl script_hidden
.hidden script_hidden

.section .note.minild,"a",@note
    .balign 4
    .long 4,4,1
    .ascii "GNU\0"
    .long 0x12345678
ASM

cat >"$work/native.lds" <<'LDS'
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
  .rela.plt : { *(.rela.plt) } :text
  .plt : { *(.plt) } :text
  .got : { *(.got) *(.got.plt) } :text
  .text : {
    script_defined = .;
    script_abs = ABSOLUTE(.);
    script_local = .;
    *(.text.probe)
  } :text
  .data : { *(.data.refs) } :text
  PROVIDE(script_unused = .);
  PROVIDE(script_provided = .);
  script_override = .;
  script_hidden = 0x1234;
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
    global:
      probe_entry; probe_refs; script_defined; script_abs;
      script_provided; script_override;
    local: *;
  };
}
LDS

"$MINIAS" -march=rv64i -mabi=lp64 -o "$work/native.o" "$work/native.s"

# Equivalent host relocations: CALL/PLT, GOTPCREL and absolute data pointers.
# GNU ld.bfd is the independent dynamic-script oracle (not byte-identical
# target code; the ELF symbol/relocation semantics must agree).
cat >"$work/host.s" <<'ASM'
.section .text.probe,"ax",@progbits
.balign 4
.globl probe_entry
.type probe_entry,@function
probe_entry:
    call script_defined
    call script_override
    call script_hidden
    mov script_defined@GOTPCREL(%rip),%rax
    mov script_override@GOTPCREL(%rip),%rbx
    mov script_hidden@GOTPCREL(%rip),%rcx
    ret
.size probe_entry,.-probe_entry

.section .data.refs,"aw",@progbits
.balign 8
.globl probe_refs
.type probe_refs,@object
probe_refs:
    .quad script_defined
    .quad script_abs
    .quad script_local
    .quad script_provided
    .quad script_override
    .quad script_hidden
.size probe_refs,.-probe_refs

.globl script_hidden
.hidden script_hidden

.section .note.minild,"a",@note
    .balign 4
    .long 4,4,1
    .ascii "GNU\0"
    .long 0x12345678
ASM
sed 's/^OUTPUT_ARCH(riscv)$/OUTPUT_ARCH(i386:x86-64)/' \
    "$work/native.lds" >"$work/host.lds"
as --64 -o "$work/host.o" "$work/host.s"
ld -r -o "$work/host-after-r.o" "$work/host.o"

cat >"$work/check.py" <<'PY'
import struct
import sys
from pathlib import Path

producer, stage, filename = sys.argv[1:]
data = Path(filename).read_bytes()
machine = 243 if producer == "native" else 62
if data[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<HH", data, 16) != (3, machine):
    raise SystemExit(f"{producer}/{stage}: not a {machine} ELF64 ET_DYN")
shoff = struct.unpack_from("<Q", data, 40)[0]
shentsize, shcount, shstridx = struct.unpack_from("<HHH", data, 58)
if shentsize != 64 or shstridx >= shcount or shoff + shcount * shentsize > len(data):
    raise SystemExit("invalid section table")
sections = [struct.unpack_from("<IIQQQQIIQQ", data, shoff + n * 64)
            for n in range(shcount)]

def contents(sh):
    if sh[1] == 8:
        return bytes(sh[5])
    if sh[4] + sh[5] > len(data):
        raise SystemExit("section extends beyond ELF")
    return data[sh[4]:sh[4] + sh[5]]

def getstr(pool, start):
    if start >= len(pool):
        raise SystemExit("invalid string offset")
    end = pool.find(b"\x00", start)
    if end < 0:
        raise SystemExit("unterminated string")
    return pool[start:end].decode()

shnames = contents(sections[shstridx])
byname = {getstr(shnames, sh[0]): (idx, sh)
          for idx, sh in enumerate(sections)}
for required in (".dynsym", ".symtab", ".text", ".data", ".rela.dyn",
                 ".dynamic", ".gnu.version", ".gnu.version_d",
                 ".hash", ".gnu.hash"):
    if required not in byname:
        raise SystemExit(f"{producer}/{stage}: missing {required}")

def symbols(table):
    _, sh = byname[table]
    names = contents(sections[sh[6]])
    blob = contents(sh)
    if sh[9] != 24 or len(blob) % 24:
        raise SystemExit(f"invalid {table}")
    result = []
    for offset in range(0, len(blob), 24):
        name, info, other, section, value, size = struct.unpack_from("<IBBHQQ", blob, offset)
        result.append(dict(name=getstr(names, name), info=info, other=other,
                           shndx=section, value=value, size=size))
    return result

dynlist = symbols(".dynsym")
symlist = symbols(".symtab")
dynsym = {s["name"]: s for s in dynlist if s["name"]}
symtab = {s["name"]: s for s in symlist if s["name"]}
exported = {"probe_entry", "probe_refs", "script_defined", "script_abs",
            "script_provided", "script_override"}
hidden = {"script_local", "script_unused", "script_hidden"}
for name in exported:
    if name not in dynsym or name not in symtab:
        raise SystemExit(f"{producer}/{stage}: missing exported {name}")
    if dynsym[name]["shndx"] == 0 or symtab[name]["shndx"] == 0:
        raise SystemExit(f"{producer}/{stage}: unresolved script symbol {name}")
    if dynsym[name]["info"] >> 4 != 1:
        raise SystemExit(f"{producer}/{stage}: exported {name} not GLOBAL")
versym_idx, versym_header = byname[".gnu.version"]
_ = versym_idx
if versym_header[1] != 0x6FFFFFFF or versym_header[6] != byname[".dynsym"][0]:
    raise SystemExit(".gnu.version has wrong type/dynsym sh_link")
versym_bytes = contents(versym_header)
if len(versym_bytes) != 2 * len(dynlist):
    raise SystemExit(".gnu.version does not match .dynsym entries")
for i, symbol in enumerate(dynlist):
    if symbol["name"] in exported and struct.unpack_from("<H", versym_bytes, i * 2)[0] != 2:
        raise SystemExit(f"VERSION MINILD_1 missing for {symbol['name']}")
if byname[".gnu.version_d"][1][1] != 0x6FFFFFFD:
    raise SystemExit("version definition missing")
for name in hidden:
    if name in dynsym:
        raise SystemExit(f"{producer}/{stage}: VERSION local:* leaked {name} to .dynsym")
if "script_unused" in symtab:
    raise SystemExit(f"{producer}/{stage}: unreferenced PROVIDE materialized")
if "script_local" not in symtab:
    raise SystemExit(f"{producer}/{stage}: lost local-script static symbol")
if symtab["script_local"]["info"] >> 4 != 0:
    raise SystemExit(f"{producer}/{stage}: VERSION local:* did not localize .symtab")
if ("script_hidden" not in symtab or
        symtab["script_hidden"]["shndx"] != 0xFFF1 or
        symtab["script_hidden"]["value"] != 0x1234):
    raise SystemExit(f"{producer}/{stage}: hidden UND not resolved to ABS 0x1234")

textidx, text = byname[".text"]
dataidx, datash = byname[".data"]
if dynsym["script_defined"]["shndx"] != textidx:
    raise SystemExit("script_defined=. lost section-relative definition")
if dynsym["script_abs"]["shndx"] != 0xFFF1:
    raise SystemExit("script_abs=ABSOLUTE(.) did not remain SHN_ABS")
for name in ("script_defined", "script_abs", "script_local"):
    if symtab[name]["value"] != text[3]:
        raise SystemExit(f"{name} must point at .text start")
if symtab["probe_entry"]["value"] != text[3]:
    raise SystemExit("probe_entry did not begin .text")
if symtab["probe_refs"]["shndx"] != dataidx:
    raise SystemExit("probe_refs not in .data")
if symtab["probe_refs"]["size"] != 48:
    raise SystemExit("probe_refs payload size changed")
for name in ("script_provided", "script_override"):
    if dynsym[name]["value"] != datash[3] + datash[5]:
        raise SystemExit(f"{name} did not resolve at .data end")
    if dynsym[name]["shndx"] != dataidx or symtab[name]["shndx"] != dataidx:
        raise SystemExit(f"{name} after .data must bind .data section, not ABS")

def relocations(section):
    if section not in byname:
        return []
    _, sh = byname[section]
    blob = contents(sh)
    if sh[9] != 24 or len(blob) % 24:
        raise SystemExit(f"invalid {section} entries")
    result = []
    for offset in range(0, len(blob), 24):
        place, info, addend = struct.unpack_from("<QQq", blob, offset)
        symidx, kind = info >> 32, info & 0xFFFFFFFF
        if symidx >= len(dynlist):
            raise SystemExit(f"{section} bad dynsym index")
        result.append((place, kind, dynlist[symidx]["name"], addend))
    return result

dynamic_relocs = relocations(".rela.dyn")
plt_relocs = relocations(".rela.plt")
if any(name in ("script_local", "script_hidden")
       for _, _, name, _ in dynamic_relocs + plt_relocs):
    raise SystemExit(f"{producer}/{stage}: non-exported script symbol in dynamic relocation")
refs_va = symtab["probe_refs"]["value"]
if any(place == refs_va + 40 for place, _, _, _ in dynamic_relocs + plt_relocs):
    raise SystemExit("hidden ABS data pointer unexpectedly has dynamic relocation")
refbytes = contents(datash)
hidden_pointer_offset = refs_va - datash[3] + 40
if hidden_pointer_offset < 0 or hidden_pointer_offset + 8 > len(refbytes):
    raise SystemExit("hidden ABS pointer outside refs")
if struct.unpack_from("<Q", refbytes, hidden_pointer_offset)[0] != 0x1234:
    raise SystemExit("hidden ABS .data pointer was not statically patched to 0x1234")
if stage == "pie":
    # Executable-defined symbols are non-preemptible in PIE, even if
    # --export-dynamic publishes them in .dynsym.  GNU ld emits RELATIVE
    # relocations for the relocatable .data references and no JUMP_SLOT.
    if producer != "host":
        raise SystemExit("native -pie -T must be tested after gaining support")
    for i in (0, 2, 3, 4):
        if not any(place == refs_va + i * 8 and kind == 8 and sym == ""
                   for place, kind, sym, _ in dynamic_relocs):
            raise SystemExit(f"GNU PIE: missing R_X86_64_RELATIVE ref[{i}]")
    if plt_relocs:
        raise SystemExit("GNU PIE manufactured preemptible PLT")
    print("MINILD_SCRIPT_DYNSYM_CHECK=PASS producer=host stage=pie "
          "exports=6 relative=4 PLT=0")
    sys.exit(0)
if producer == "native":
    # The four externally preemptible addresses need R_RISCV_64; the local
    # script address must use R_RISCV_RELATIVE, never a global dynamic index.
    for i, name in ((0, "script_defined"), (1, "script_abs"),
                    (3, "script_provided"), (4, "script_override")):
        if not any(place == refs_va + i * 8 and kind == 2 and sym == name
                   for place, kind, sym, _ in dynamic_relocs):
            raise SystemExit(f"{stage}: missing R_RISCV_64 {name} ref[{i}]")
    if not any(place == refs_va + 16 and kind == 3 and sym == ""
               for place, kind, sym, _ in dynamic_relocs):
        raise SystemExit(f"{stage}: local script_local must be R_RISCV_RELATIVE")
    for name in ("script_defined", "script_override"):
        if not any(kind == 5 and sym == name for _, kind, sym, _ in plt_relocs):
            raise SystemExit(f"{stage}: missing PLT JUMP_SLOT {name}")
        if not any(kind == 2 and sym == name for _, kind, sym, _ in dynamic_relocs):
            raise SystemExit(f"{stage}: missing GOT R_RISCV_64 {name}")
    plt = byname.get(".plt")
    if plt is None:
        raise SystemExit("missing .plt")
    plt_begin, plt_end = plt[1][3], plt[1][3] + plt[1][5]
    textdata = contents(text)
    call_targets = []
    for index in (0, 1, 2):
        pc = symtab["probe_entry"]["value"] + index * 8
        pos = pc - text[3]
        if pos < 0 or pos + 8 > len(textdata):
            raise SystemExit("CALL sequence outside .text")
        hi, lo = struct.unpack_from("<II", textdata, pos)
        if hi & 0x7f != 0x17 or lo & 0x707f != 0x67:
            raise SystemExit("CALL does not encode AUIPC/JALR")
        imm_hi = (hi & 0xFFFFF000)
        if imm_hi & 0x80000000:
            imm_hi -= 1 << 32
        imm_lo = (lo >> 20) & 0xFFF
        if imm_lo & 0x800:
            imm_lo -= 1 << 12
        dest = pc + imm_hi + imm_lo
        if index == 2:
            if dest != 0x1234:
                raise SystemExit(f"hidden CALL target was not ABS 0x1234: {dest:#x}")
            continue
        if not plt_begin <= dest < plt_end:
            raise SystemExit(f"CALL[{index}] did not target PLT: {dest:#x}")
        call_targets.append(dest)
    if call_targets[0] == call_targets[1]:
        raise SystemExit("two distinct CALL symbols incorrectly share a PLT stub")
    got = byname.get(".got")
    if got is None:
        raise SystemExit("missing GOT for hidden ABS reference")
    got_section = got[1]
    got_addr, got_bytes = got_section[3], contents(got_section)
    # Three CALLs (24 bytes), two preemptible 8-byte LA sequences, then
    # an LA that must read the static ABS literal from a GOT slot.
    got_pc = symtab["probe_entry"]["value"] + 40
    got_off = got_pc - text[3]
    if got_off < 0 or got_off + 8 > len(textdata):
        raise SystemExit("hidden GOT sequence outside .text")
    got_hi, got_lo = struct.unpack_from("<II", textdata, got_off)
    if got_hi & 0x7F != 0x17 or got_lo & 0x707F != 0x3003:
        raise SystemExit("hidden PIC LA is not AUIPC/LD")
    got_upper = got_hi & 0xFFFFF000
    if got_upper & 0x80000000:
        got_upper -= 1 << 32
    got_lower = (got_lo >> 20) & 0xFFF
    if got_lower & 0x800:
        got_lower -= 1 << 12
    got_slot = got_pc + got_upper + got_lower
    got_index = got_slot - got_addr
    if (got_index < 0 or got_index + 8 > len(got_bytes) or
            struct.unpack_from("<Q", got_bytes, got_index)[0] != 0x1234):
        raise SystemExit("hidden ABS PIC GOT slot must be static 0x1234")
    if any(place == got_slot for place, _, _, _ in dynamic_relocs + plt_relocs):
        raise SystemExit("hidden ABS PIC GOT slot must have no dynamic relocation")
else:
    # GNU x86_64 oracle may lower preemptible GOT+PLT to GLOB_DAT/PLT.GOT,
    # but the two external GOT symbols and four absolute references must be
    # reachable through runtime relocations, with the hidden one RELATIVE.
    for i, name in ((0, "script_defined"), (1, "script_abs"),
                    (3, "script_provided"), (4, "script_override")):
        if not any(place == refs_va + i * 8 and kind == 1 and sym == name
                   for place, kind, sym, _ in dynamic_relocs):
            raise SystemExit(f"GNU {stage}: missing R_X86_64_64 {name}")
    if not any(place == refs_va + 16 and kind == 8 and sym == ""
               for place, kind, sym, _ in dynamic_relocs):
        raise SystemExit(f"GNU {stage}: local pointer not RELATIVE")
    for name in ("script_defined", "script_override"):
        if not any(kind == 6 and sym == name for _, kind, sym, _ in dynamic_relocs):
            raise SystemExit(f"GNU {stage}: GOT lacks GLOB_DAT {name}")
    textdata = contents(text)
    offset = symtab["probe_entry"]["value"] - text[3] + 10
    if offset < 0 or offset + 5 > len(textdata) or textdata[offset] != 0xE8:
        raise SystemExit("GNU hidden CALL is not direct x86 rel32")
    displacement = struct.unpack_from("<i", textdata, offset + 1)[0]
    if text[3] + offset + 5 + displacement != 0x1234:
        raise SystemExit("GNU hidden CALL target is not 0x1234")

print(f"MINILD_SCRIPT_DYNSYM_CHECK=PASS producer={producer} stage={stage} "
      f"exports={len(exported)} local=1 provide_unused=absent "
      f"relocs={len(dynamic_relocs)} PLT={len(plt_relocs)}")
PY

# Re-link the original ET_REL and a MiniLD -r output through the same script.
"$MINILD" -melf64lriscv -r -o "$work/native-after-r.o" "$work/native.o"
for stage in direct after-r; do
    input="$work/native.o"
    if [[ $stage == after-r ]]; then
        input="$work/native-after-r.o"
    fi
    if ! "$MINILD" -melf64lriscv -shared --hash-style=both \
            -soname libscript-dynsym.so -T "$work/native.lds" \
            -o "$work/native-$stage.so" "$input" \
            >"$work/native-$stage.stdout" \
            2>"$work/native-$stage.stderr"; then
        echo "MINILD_SCRIPT_DYNSYM=FAIL native_link=$stage" >&2
        sed -n '1,20p' "$work/native-$stage.stderr" >&2
        exit 1
    fi
done

for stage in direct after-r; do
    input="$work/host.o"
    if [[ $stage == after-r ]]; then
        input="$work/host-after-r.o"
    fi
    if ! ld -m elf_x86_64 -shared --hash-style=both \
            -soname libscript-dynsym.so -T "$work/host.lds" \
            -o "$work/host-$stage.so" "$input" \
            >"$work/host-$stage.stdout" \
            2>"$work/host-$stage.stderr"; then
        echo "MINILD_SCRIPT_DYNSYM=FAIL GNU_oracle=$stage" >&2
        sed -n '1,20p' "$work/host-$stage.stderr" >&2
        exit 1
    fi
done

for stage in direct after-r; do
    python3 "$work/check.py" host "$stage" "$work/host-$stage.so"
done
for stage in direct after-r; do
    python3 "$work/check.py" native "$stage" "$work/native-$stage.so"
done

# GNU ld supports -pie with a full -T script.  MiniLD deliberately rejects
# -pie -T today; make that unsupported mode visible but do not call it PASS or
# let it block the shared-library symbol-lifecycle contract.
ld -m elf_x86_64 -pie --export-dynamic --hash-style=both -e probe_entry \
    -soname libscript-dynsym.so -T "$work/host.lds" \
    -o "$work/host-pie.elf" "$work/host.o"
python3 "$work/check.py" host pie "$work/host-pie.elf"
if "$MINILD" -melf64lriscv -pie -e probe_entry --hash-style=both \
        -soname libscript-dynsym.so -T "$work/native.lds" \
        -o "$work/native-pie.elf" "$work/native.o" \
        >"$work/native-pie.stdout" 2>"$work/native-pie.stderr"; then
    python3 "$work/check.py" native pie "$work/native-pie.elf"
else
    if ! grep -Fq 'unsupported-pie-linker-script:' "$work/native-pie.stderr"; then
        echo 'MINILD_SCRIPT_DYNSYM=FAIL PIE unexpected rejection' >&2
        sed -n '1,12p' "$work/native-pie.stderr" >&2
        exit 1
    fi
    echo 'MINILD_SCRIPT_DYNSYM_PIE=INCONCLUSIVE native_T_script_unsupported GNU_oracle=PASS'
fi

echo 'MINILD_SCRIPT_DYNSYM=PASS native_shared=2 GNU_shared=2'
