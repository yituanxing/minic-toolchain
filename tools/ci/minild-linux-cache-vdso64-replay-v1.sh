#!/usr/bin/env bash
# Probe GNU LD's *real* RISC-V 64-bit vDSO shared-link boundary using a frozen
# Linux 6.6.143 Kbuild object cache. No Linux C build, no Kbuild and no QEMU.
# Preserve exact GNU arguments: do NOT silently ignore ELF metadata flags.
# Reference replay fix #38487964158: .dbg.tmp is intentionally deleted by Kbuild.
# #38521133625 then hit --hash-style=both. Both -S and GNU_HASH now have focused T1 coverage; re-evaluate exact CLI
# on the current MiniLD SHA; reject any missing shared-ELF semantics.
# Reduced shared-link probe is diagnostically useful but never qualifies P1.
# Live gap gate: observed remaining GNU LD vDSO64 from #38485397284;
# only a new 64-bit shared-link implementation can remove this fallback.
set -Eeuo pipefail
if (( $# != 3 )); then
  echo "usage: $0 <gnu-kbuild-out> <minild-build-dir> <evidence-dir>" >&2
  exit 64
fi
out=$(realpath "$1")
tools=$(realpath -m "$2")
ev=$(realpath -m "$3")
repo=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$ev"
declare -a inputs=(
  arch/riscv/kernel/vdso/rt_sigreturn.o
  arch/riscv/kernel/vdso/vgettimeofday.o
  arch/riscv/kernel/vdso/getcpu.o
  arch/riscv/kernel/vdso/flush_icache.o
  arch/riscv/kernel/vdso/hwprobe.o
  arch/riscv/kernel/vdso/sys_hwprobe.o
  arch/riscv/kernel/vdso/note.o
)
for f in "${inputs[@]}" arch/riscv/kernel/vdso/vdso.lds; do
  [[ -s "$out/$f" ]] || {
    echo "MINILD_VDSO=INCONCLUSIVE missing_pinned_GNU_object=$f" >&2
    exit 8
  }
  sha256sum "$out/$f" >>"$ev/inputs.sha256"
done
make -C "$repo" -j4 MODE=release CFLAGS=-Werror BUILD_DIR="$tools" \
  "$tools/bin/minic-ld"
minild="$tools/bin/minic-ld"
sha256sum "$minild" >"$ev/tool.sha256"
(
  cd "$out"
  # GNU Kbuild deletes vdso.so.dbg.tmp after producing vdso.so.dbg.
  # Regenerate the EXACT reference link from the same frozen .o/.lds inputs,
  # rather than claiming the transient intermediate must remain in cache.
  riscv64-linux-gnu-ld -melf64lriscv -z noexecstack \
    --no-warn-rwx-segments -shared -S \
    -soname=linux-vdso.so.1 --build-id=sha1 \
    --hash-style=both --eh-frame-hdr \
    -T arch/riscv/kernel/vdso/vdso.lds \
    "${inputs[@]}" -o "$ev/gnu-vdso.so.dbg.tmp" \
    >"$ev/gnu-ld.stdout" 2>"$ev/gnu-ld.stderr"
  sha256sum "$ev/gnu-vdso.so.dbg.tmp" >"$ev/gnu-output.sha256"
  # Read-only GNU oracle for the *actual* RISC-V vDSO: count .eh_frame
  # FDEs and report GNU EH-frame section / program-header semantics before
  # attempting the native link. Do not infer the correct header from flags.
  riscv64-linux-gnu-readelf -W -S -l "$ev/gnu-vdso.so.dbg.tmp" >"$ev/gnu-phdr-sections.txt"
  riscv64-linux-gnu-readelf -W --debug-dump=frames "$ev/gnu-vdso.so.dbg.tmp" >"$ev/gnu-frames.txt"
  grep -E '([.]eh_frame|GNU_EH_FRAME)' "$ev/gnu-phdr-sections.txt" |
    sed 's/^/MINILD_VDSO_GNU_EH_LAYOUT /' || true
  echo "MINILD_VDSO_GNU_FDES=$(grep -c ' FDE ' "$ev/gnu-frames.txt" || true)"
  echo "MINILD_VDSO_CIE_FDE_REFERENCE_BEGIN"
  head -n 100 "$ev/gnu-frames.txt"
  echo "MINILD_VDSO_CIE_FDE_REFERENCE_END"
  echo "MINILD_VDSO_LINKER_SCRIPT_BEGIN"
  # The .lds file starts with generated C preprocessor comments. Show
  # executable linker directives, not the first 110 irrelevant header lines.
  awk 'BEGIN{show=0} /^OUTPUT_ARCH|^SECTIONS|^VERSION|^PHDRS/{show=1} show{print}'     "$out/arch/riscv/kernel/vdso/vdso.lds" | head -n 240
  echo "MINILD_VDSO_LINKER_SCRIPT_END"
  # Validate the whole real generated script with the existing MiniLD AST,
  # not a hand-selected approximation. Uses HOSTCC only for the probe.
  cc -std=c11 -O2 -Wall -Wextra -Werror \
    -I"$repo/linker/src" -I"$repo/linker/include" \
    "$repo/tests/linker/vdso_real_script_probe.c" \
    "$repo/linker/src/linker_script_lex.c" \
    "$repo/linker/src/linker_script_parse.c" \
    "$repo/linker/src/linker_script_expr.c" \
    "$repo/linker/src/linker_script_match.c" \
    -o "$ev/vdso-real-script-probe"
  "$ev/vdso-real-script-probe" arch/riscv/kernel/vdso/vdso.lds
  echo "MINILD_VDSO_REFERENCE=PASS source=same_frozen_Kbuild_inputs compiler_C=skipped"
  start=$(date +%s%3N)
  code=0
  timeout 90s "$minild" -melf64lriscv -z noexecstack \
    --no-warn-rwx-segments -shared -S \
    -soname=linux-vdso.so.1 --build-id=sha1 \
    --hash-style=both --eh-frame-hdr \
    -T arch/riscv/kernel/vdso/vdso.lds \
    "${inputs[@]}" -o "$ev/vdso.so.dbg.tmp" \
    >"$ev/ld.stdout" 2>"$ev/ld.stderr" || code=$?
  elapsed=$(($(date +%s%3N) - start))
  printf '%s\t%s\n' "$elapsed" "$code" >"$ev/timing.tsv"
  echo "MINILD_VDSO_LINK elapsed_ms=$elapsed rc=$code no_Linux_C_recompile=true"
  if (( code != 0 )); then
    echo "MINILD_VDSO=GAP exact_GNU_shared_link_not_yet_supported" >&2
    # Preserve the first *actual* current MiniLD failure rather than
    # treating an earlier error (-S) as an unchanging diagnosis.
    first_blocker="$(grep -m1 '^minic-ld:' "$ev/ld.stderr" || true)"
    printf '%s\n' "$first_blocker" > "$ev/first-blocker.txt"
    echo "MINILD_VDSO_FIRST_BLOCKER=${first_blocker:-unknown} exact_GNU_flags=true"
    # A staging verdict, NOT a Linux vDSO acceptance claim. Distinguish
    # successful evaluation of the real four-PHDR script addresses from the
    # still-gated ELF writer; a failed preflight prints its exact reason.
    if [[ "$first_blocker" == 'minic-ld: unsupported-shared-script-PHDR-emission' ]]; then
      echo 'MINILD_VDSO_SCRIPT_LAYOUT_PREFLIGHT=PASS real_GNU_script=true ELF_emission=false P1=false'
    else
      echo 'MINILD_VDSO_SCRIPT_LAYOUT_PREFLIGHT=INCOMPLETE real_GNU_script=true ELF_emission=false P1=false'
    fi
    tail -n 55 "$ev/ld.stderr" >&2
    # Exploratory ONLY: test the existing generic ELF64 shared-link engine
    # without -S, --hash-style=both, --eh-frame-hdr, or the missing
    # shared linker-script layout. Do NOT claim this is a valid Linux vDSO.
    reduced_status=0
    timeout 90s "$minild" -melf64lriscv -shared \
      -soname=linux-vdso.so.1 \
      "${inputs[@]}" -o "$ev/vdso-reduced-diagnostic.so" \
      >"$ev/reduced.stdout" 2>"$ev/reduced.stderr" || reduced_status=$?
    echo "MINILD_VDSO_REDUCED_DIAGNOSTIC rc=$reduced_status omitted_exact_flags=-S,hash_style_both,eh_frame_hdr,script runtime_claim=false"
    if (( reduced_status != 0 )); then
      tail -n 35 "$ev/reduced.stderr" >&2
    else
      riscv64-linux-gnu-readelf -Wh "$ev/vdso-reduced-diagnostic.so" \
        >"$ev/reduced-header.txt" || true
      riscv64-linux-gnu-readelf -W -S -l -d \
        "$ev/vdso-reduced-diagnostic.so" >"$ev/reduced-elf.txt" || true
    fi
    # Independent diagnostic only: exercise the new EH-frame-hdr parser
    # against all seven genuine Linux .o (4 GNU FDEs), without pretending
    # the missing full -T linker script or symbol versioning is implemented.
    # The script-defined _vdso_data cannot resolve without -T, so define
    # a diagnostic-only ABS symbol. Never feed this output into Linux.
    cat >"$ev/vdso-diagnostic-symbol.s" <<'MINILD_VDSO_DIAG_ASM'
    .globl _vdso_data
    .set _vdso_data, 0
    .globl _timens_data
    .set _timens_data, 4096
MINILD_VDSO_DIAG_ASM
    riscv64-linux-gnu-as -march=rv64gc -mabi=lp64d \
      -o "$ev/vdso-diagnostic-symbol.o" "$ev/vdso-diagnostic-symbol.s"
    # Extract the actual VERSION block, not a separately invented
    # seven-export mapping. Feed the same source to --version-script.
    python3 - arch/riscv/kernel/vdso/vdso.lds "$ev/vdso-version-only.map" <<'PY_VDSO_VERSION'
from pathlib import Path
import re,sys
source=Path(sys.argv[1]).read_text()
match=re.search(r'\bVERSION\s*\{\s*(LINUX_4\.15\s*\{.*?\};)\s*\}',source,re.S)
if match is None:
    raise SystemExit('MINILD_VDSO=FAIL cannot_extract_exact_LINUX_4.15_version')
Path(sys.argv[2]).write_text(match.group(1)+'\n')
PY_VDSO_VERSION
    diag_rc=0
    timeout 90s "$minild" -melf64lriscv -shared -S \
      --hash-style=both --eh-frame-hdr -soname=linux-vdso.so.1 \
      --version-script="$ev/vdso-version-only.map" \
      "${inputs[@]}" "$ev/vdso-diagnostic-symbol.o" \
      -o "$ev/vdso-eh-only-diagnostic.so" \
      >"$ev/eh-diagnostic.stdout" 2>"$ev/eh-diagnostic.stderr" || diag_rc=$?
    echo "MINILD_VDSO_EH_DIAGNOSTIC_LINK rc=$diag_rc genuine_GNU_objects=7 synthetic_script_symbol=true exact_script=false runtime_claim=false"
    if (( diag_rc == 0 )); then
      riscv64-linux-gnu-readelf -W -S -l -d \
        "$ev/vdso-eh-only-diagnostic.so" >"$ev/eh-diagnostic-elf.txt"
      riscv64-linux-gnu-readelf -W --debug-dump=frames \
        "$ev/vdso-eh-only-diagnostic.so" >"$ev/eh-diagnostic-frames.txt"
      diag_fdes="$(grep -c ' FDE ' "$ev/eh-diagnostic-frames.txt" || true)"
      # Verify that all 4 real CFI records are actually represented in the
      # native sorted binary search index, not just present in .eh_frame.
      # This is still NOT acceptance for the exact -T / VERSION script.
      python3 - "$ev/vdso-eh-only-diagnostic.so" <<'PY_EH_INDEX'
import pathlib, struct, sys
b = pathlib.Path(sys.argv[1]).read_bytes()
u16 = lambda off: struct.unpack_from('<H', b, off)[0]
u32 = lambda off: struct.unpack_from('<I', b, off)[0]
u64 = lambda off: struct.unpack_from('<Q', b, off)[0]
phoff, shoff = u64(32), u64(40)
phsize, phcount = u16(54), u16(56)
shsize, shcount, strindex = u16(58), u16(60), u16(62)
strhdr = shoff + strindex * shsize
names = b[u64(strhdr+24):u64(strhdr+24)+u64(strhdr+32)]
sects = {}
for i in range(shcount):
    o = shoff + i * shsize
    start = u32(o)
    end = names.find(b'\x00', start)
    name = names[start:end].decode()
    sects[name] = (u64(o+16),u64(o+24),u64(o+32))
eh_vaddr, _, eh_size = sects['.eh_frame']
hdr_vaddr, hdr_off, hdr_size = sects['.eh_frame_hdr']
hdr = b[hdr_off:hdr_off+hdr_size]
assert hdr_size == 44, f"unexpected EH header bytes: {hdr_size}"
assert hdr[:4] == bytes([1,0x1b,3,0x3b])
assert struct.unpack_from('<I',hdr,8)[0] == 4
ptr = struct.unpack_from('<i',hdr,4)[0]
assert hdr_vaddr + 4 + ptr == eh_vaddr
pcs, fdes = [], []
for i in range(4):
    pc = hdr_vaddr + struct.unpack_from('<i', hdr,12+8*i)[0]
    fde = hdr_vaddr + struct.unpack_from('<i', hdr,16+8*i)[0]
    pcs.append(pc)
    fdes.append(fde)
assert pcs == sorted(pcs), pcs
assert len(set(fdes)) == 4 and all(eh_vaddr <= v < eh_vaddr+eh_size for v in fdes)
assert any(u32(phoff+i*phsize) == 0x6474e550 for i in range(phcount))
print('MINILD_VDSO_EH_INDEX=PASS real_fdes=4 header_bytes=44 sorted_pcs=true PT_GNU_EH_FRAME=true runtime_claim=false')
PY_EH_INDEX
      # Assert the seven actual versioned Linux exports in .dynsym.
      # This lane deliberately has NO -T layout and cannot satisfy P1.
      riscv64-linux-gnu-readelf -W --version-info \
        "$ev/vdso-eh-only-diagnostic.so" > "$ev/native-version-info.txt"
      riscv64-linux-gnu-readelf -W --dyn-syms \
        "$ev/vdso-eh-only-diagnostic.so" > "$ev/native-dynsyms.txt"
      test "$(grep -c '(VERDEFNUM).*2' "$ev/eh-diagnostic-elf.txt")" -eq 1
      for sym in __vdso_rt_sigreturn __vdso_gettimeofday __vdso_clock_gettime \
                 __vdso_clock_getres __vdso_getcpu __vdso_flush_icache \
                 __vdso_riscv_hwprobe; do
        grep -Fq "${sym}@@LINUX_4.15" "$ev/native-dynsyms.txt"
      done
      echo 'MINILD_VDSO_VERSION_DIAGNOSTIC=PASS actual_export_count=7 node=LINUX_4.15 verdefnum=2 runtime_claim=false'
      echo "MINILD_VDSO_EH_DIAGNOSTIC fdes=$diag_fdes expected=4 GNU_EH_FRAME=$(grep -c 'GNU_EH_FRAME' "$ev/eh-diagnostic-elf.txt" || true) runtime_claim=false"
    else
      sed 's/^/MINILD_VDSO_EH_DIAGNOSTIC_ERROR /' "$ev/eh-diagnostic.stderr" | head -n 45
    fi
    exit "$code"
  fi
)
ref="$ev/gnu-vdso.so.dbg.tmp"
test -s "$ev/vdso.so.dbg.tmp"
for a in "$ref" "$ev/vdso.so.dbg.tmp"; do
  riscv64-linux-gnu-readelf -Wh "$a" >"$ev/$(basename "$a").header.$(sha256sum "$a" | cut -c1-8).txt"
done
riscv64-linux-gnu-readelf -W -lS -d "$ref" >"$ev/gnu-vdso.txt"
riscv64-linux-gnu-readelf -W -lS -d "$ev/vdso.so.dbg.tmp" >"$ev/native-vdso.txt"
python3 - "$ref" "$ev/vdso.so.dbg.tmp" <<'PY'
import struct,sys
from pathlib import Path
for p in map(Path,sys.argv[1:]):
  b=p.read_bytes()
  if b[:6]!=b'\x7fELF\x02\x01':
    raise SystemExit(f'MINILD_VDSO=FAIL not_ELF64LE: {p}')
  if struct.unpack_from('<H',b,16)[0]!=3:
    raise SystemExit(f'MINILD_VDSO=FAIL not_ET_DYN: {p}')
  if struct.unpack_from('<H',b,18)[0]!=243:
    raise SystemExit(f'MINILD_VDSO=FAIL not_RISCV: {p}')
  print(f'MINILD_VDSO_ELF=PASS {p}')
a,b=[Path(p).read_bytes() for p in sys.argv[1:]]
if a[48:52]!=b[48:52]:
  raise SystemExit('MINILD_VDSO=FAIL ABI_flags_differ')
print('MINILD_VDSO_ABI=PASS')
PY
for section in .hash .gnu.hash .eh_frame_hdr .dynamic .dynsym; do
  grep -Fq "$section " "$ev/gnu-vdso.txt" || {
    echo "MINILD_VDSO=INCONCLUSIVE GNU_missing_required_section:$section" >&2
    exit 8
  }
  grep -Fq "$section " "$ev/native-vdso.txt" || {
    echo "MINILD_VDSO=FAIL native_missing_section:$section" >&2
    exit 10
  }
done
grep -Fq 'linux-vdso.so.1' "$ev/native-vdso.txt" || {
  echo "MINILD_VDSO=FAIL missing_SONAME" >&2
  exit 11
}
# Acceptance requires GNU-matching PHDR types/flags and seven real
# versioned Linux exports, not merely an ELF header plus section names.
python3 - "$ref" "$ev/vdso.so.dbg.tmp" <<'PY_VDSO_ABI'
from pathlib import Path
import struct, sys

def read(path):
    b = Path(path).read_bytes()
    e_phoff, e_shoff = struct.unpack_from('<QQ', b, 32)
    e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx = struct.unpack_from('<HHHHH', b, 54)
    names_at = e_shoff + e_shentsize * e_shstrndx
    string_offset, string_size = struct.unpack_from('<QQ', b, names_at + 24)
    names = b[string_offset:string_offset + string_size]
    sections = {}
    for i in range(e_shnum):
        at = e_shoff + e_shentsize * i
        begin = struct.unpack_from('<I', b, at)[0]
        end = names.find(b'\x00', begin)
        name = names[begin:end].decode()
        typ, flags, vma, offset, size = struct.unpack_from('<IQQQQ', b, at + 4)
        sections[name] = (typ, flags, vma, offset, size)
    phdrs = []
    for i in range(e_phnum):
        at = e_phoff + e_phentsize * i
        typ, flags, offset, vma, paddr, filesz, memsz, align = struct.unpack_from('<IIQQQQQQ', b, at)
        phdrs.append((typ, flags, offset, vma, filesz, memsz))
    return sections, phdrs

gnu_sections, gnu_phdrs = read(sys.argv[1])
native_sections, native_phdrs = read(sys.argv[2])
required = [1, 2, 4, 0x6474e550]
for source, phdrs, sections in [('gnu',gnu_phdrs,gnu_sections),('native',native_phdrs,native_sections)]:
    assert [p[0] for p in phdrs] == required, (source,phdrs)
    assert [p[1] for p in phdrs] == [5,4,4,4], (source,phdrs)
    assert phdrs[0][2:4] == (0,0), (source,phdrs)
    assert sections['.eh_frame_hdr'][2] == phdrs[3][3]
    assert sections['.eh_frame_hdr'][4] == phdrs[3][4] == 44
    assert sections['.dynamic'][2] == phdrs[1][3]
    note_vma, note_bytes = phdrs[2][3], phdrs[2][4]
    assert any(info[0] == 7 and info[2] >= note_vma and info[2]+info[4] <= note_vma+note_bytes
               for info in sections.values()), (source,'bad-PT_NOTE')
    assert sections['.text'][2] >= 0x800, (source,'text-before-script-boundary')
for name in ['.hash','.gnu.hash','.dynsym','.dynstr','.gnu.version',
             '.gnu.version_d','.note.gnu.build-id','.dynamic','.eh_frame_hdr','.eh_frame','.text']:
    if name in gnu_sections:
        assert name in native_sections, ('missing',name)
print('MINILD_VDSO_SCRIPT_PHDR=PASS four_actual_PHDRS=true flags=5,4,4,4 load_bias=0 note_mapped=true eh_index=44')
PY_VDSO_ABI
riscv64-linux-gnu-readelf -W --dyn-syms "$ev/vdso.so.dbg.tmp" > "$ev/vdso-exports.txt"
riscv64-linux-gnu-readelf -W -d "$ev/vdso.so.dbg.tmp" > "$ev/vdso-dynamic.txt"
for sym in __vdso_rt_sigreturn __vdso_gettimeofday __vdso_clock_gettime \
           __vdso_clock_getres __vdso_getcpu __vdso_flush_icache \
           __vdso_riscv_hwprobe; do
  grep -Fq "${sym}@@LINUX_4.15" "$ev/vdso-exports.txt"
done
grep -q '(VERDEFNUM).*2' "$ev/vdso-dynamic.txt"
echo 'MINILD_VDSO_REAL_VERSIONS=PASS GNU_map=LINUX_4.15 exports=7'
sha256sum "$ev/vdso.so.dbg.tmp" >"$ev/output.sha256"
echo "MINILD_VDSO=PASS exact_GNU_flags=true ELF64_RISCV_ET_DYN=true hash_both=true eh_frame_hdr=true SONAME=true no_runtime_claim=true"
