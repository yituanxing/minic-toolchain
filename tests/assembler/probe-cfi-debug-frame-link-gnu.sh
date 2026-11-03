#!/usr/bin/env bash
set -Eeuo pipefail

: "${MINIAS:?Set MINIAS to the exact-head assembler binary}"
for tool in as ld readelf python3; do command -v "$tool" >/dev/null; done

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# The actual CIE/FDE graph matters, not the producer's CIE byte offsets:
# GNU ld -r concatenates .debug_frame from independent input objects.
compare_frames() {
  python3 - "$@" <<'PY'
import pathlib
import re
import sys

label, reference, actual = sys.argv[1:]

def parse(path):
    cies, fdes, current = [], [], None
    for line in pathlib.Path(path).read_text().splitlines():
        cie = re.search(r'^\s*([0-9a-f]{8})\s+[0-9a-f]+\s+[0-9a-f]+\s+CIE\b', line)
        fde = re.search(r'^\s*([0-9a-f]{8})\s+[0-9a-f]+\s+[0-9a-f]+\s+FDE\s+cie=([0-9a-f]{8})\b', line)
        if cie:
            current = dict(addr=cie.group(1), rules=[])
            cies.append(current)
        elif fde:
            current = dict(cie=fde.group(2), rules=[])
            fdes.append(current)
        elif current is not None and 'DW_CFA_' in line:
            rule = line.split('DW_CFA_', 1)[1].strip()
            if rule != 'nop' and not rule.startswith('advance_loc'):
                current['rules'].append(rule)
    addresses = {cie['addr']: i for i, cie in enumerate(cies)}
    # CIE deduplication is linker/target-dependent: some GNU linkers keep
    # one CIE per input FDE.  The CIE a given FDE *resolves to* determines
    # unwind semantics, not the number of physically duplicated CIE records.
    assert cies and len(fdes) == 4, (label, path, cies, fdes)
    assert all(fde['cie'] in addresses for fde in fdes), (label, path, fdes)
    return {'cie_rules': [cie['rules'] for cie in cies],
            'fde_cies': [addresses[fde['cie']] for fde in fdes],
            'fde_rules': [fde['rules'] for fde in fdes],
            'resolved_fdes': [
                (cies[addresses[fde['cie']]]['rules'], fde['rules'])
                for fde in fdes]}

gnu, mini = parse(reference), parse(actual)
if label.startswith('ELF'):
    # Preserve the historically strict x86 ELF32/64 CIE graph contract.
    assert len(gnu['cie_rules']) == 2 and gnu['fde_cies'] == [0, 0, 1, 1], (label, gnu)
    equivalent = mini == gnu
else:
    # Cross targets may legitimately retain four CIE records where x86
    # coalesces two.  Check each FDE's resolved rules, including its CIE's
    # CFA rule, so a wrongly linked CIE still fails even after GNU ld -r.
    equivalent = mini['resolved_fdes'] == gnu['resolved_fdes']
if not equivalent:
    raise SystemExit(f'{label} ld -r CIE-link mismatch:\nGNU={gnu}\nMini={mini}')
print(f'MINIAS_CFI_LINK=PASS {label} GNU_ld_r '
      f'GNU_CIE={len(gnu["cie_rules"])} Mini_CIE={len(mini["cie_rules"])} '
      'FDE=4 resolved_CIE_rules=exact')
PY
}

# Every FDE in .debug_frame needs a relocation for its CIE pointer, even
# when the CIE begins at offset zero in the original ET_REL.  GNU ld -r may
# concatenate multiple such sections, shifting later objects' CIE offsets.
# Reading individual objects alone cannot catch the missing relocation.
cat >"$work/source.s" <<'ASM'
.cfi_sections .debug_frame
.text
one:
.cfi_startproc
nop
.cfi_endproc
two:
.cfi_startproc
nop
.cfi_endproc
ASM

for bits in 32 64; do
  as "--$bits" -o "$work/gnu.o" "$work/source.s"
  if [[ "$bits" == 32 ]]; then
    "$MINIAS" --target=x86_64 -march=i386 -mabi=i386 \
      -o "$work/mini.o" "$work/source.s"
    emulation=elf_i386
  else
    "$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/source.s"
    emulation=elf_x86_64
  fi

  # GNU reference and mixed GNU+MiniAS must give the same CIE/FDE graph
  # after the linker adjusts all .debug_frame section-base relocations.
  ld -r -m "$emulation" -o "$work/gg.o" "$work/gnu.o" "$work/gnu.o"
  ld -r -m "$emulation" -o "$work/gm.o" "$work/gnu.o" "$work/mini.o"
  readelf --debug-dump=frames "$work/gg.o" >"$work/gg.frames"
  readelf --debug-dump=frames "$work/gm.o" >"$work/gm.frames"

  compare_frames "ELF$bits" "$work/gg.frames" "$work/gm.frames"
done

# A GNU RISC-V cross linker is not required for this structural contract:
# the common writer must emit an R_RISCV_32 CIE-pointer relocation in each
# RISC-V ELF64 ET_REL too, independent of x86/i386 target specifics.
"$MINIAS" --target=riscv -march=rv64i -mabi=lp64 \
    -o "$work/riscv.o" "$work/source.s"
readelf -Wr "$work/riscv.o" >"$work/riscv.reloc"
python3 - "$work/riscv.reloc" <<'PY'
import pathlib
import re
import sys

content = pathlib.Path(sys.argv[1]).read_text()
section = re.search(r"Relocation section '\.rela.debug_frame'.*?(?=Relocation section|\Z)",
                    content, re.S)
assert section, content
# Each FDE contributes a CIE section-relative relocation, plus its PC.
types = re.findall(r'\b(R_RISCV_\w+)\b', section.group())
assert types.count('R_RISCV_32') == 2, types
print('MINIAS_CFI_LINK=PASS RV64 .debug_frame CIE_relocations=R_RISCV_32x2')
PY

# Four target families plus RV32: a second source object moves the later CIE
# away from section offset zero. Only real GNU cross linkers can certify the
# corresponding CIE relocations. Do not count unavailable cross tools as a
# passing test in the required exact-head CI lane.
check_cross_frames() {
  local profile=$1 gas=$2 linker=$3 gasflags=$4 ldflags=$5 miniargs=$6 instruction=$7 cfa=$8
  local -a gas_args=() ld_args=() mini_args=()
  read -r -a gas_args <<<"$gasflags"
  read -r -a ld_args <<<"$ldflags"
  read -r -a mini_args <<<"$miniargs"
  if ! command -v "$gas" >/dev/null || ! command -v "$linker" >/dev/null; then
    if [[ ${REQUIRE_ALL_GNU:-0} == 1 ]]; then
      echo "MINIAS_CFI_LINK=BLOCKED profile=$profile missing=$gas/$linker" >&2
      return 2
    fi
    echo "MINIAS_CFI_LINK=SKIP profile=$profile cross_gnu_unavailable"
    return 0
  fi
  cat >"$work/$profile.s" <<ASM
.cfi_sections .debug_frame
.text
one:
.cfi_startproc simple
.cfi_def_cfa $cfa,16
$instruction
.cfi_endproc
two:
.cfi_startproc simple
.cfi_def_cfa $cfa,32
$instruction
.cfi_endproc
ASM
  "$gas" "${gas_args[@]}" -o "$work/$profile.gnu.o" "$work/$profile.s" || return 1
  "$MINIAS" "${mini_args[@]}" -o "$work/$profile.mini.o" "$work/$profile.s" || return 1
  "$linker" "${ld_args[@]}" -r -o "$work/$profile.gg.o" \
    "$work/$profile.gnu.o" "$work/$profile.gnu.o" || return 1
  "$linker" "${ld_args[@]}" -r -o "$work/$profile.gm.o" \
    "$work/$profile.gnu.o" "$work/$profile.mini.o" || return 1
  readelf --debug-dump=frames "$work/$profile.gg.o" >"$work/$profile.gg.frames" || return 1
  readelf --debug-dump=frames "$work/$profile.gm.o" >"$work/$profile.gm.frames" || return 1
  compare_frames "$profile" "$work/$profile.gg.frames" "$work/$profile.gm.frames" || return 1
}

cross_failures=0
run_cross_frames() {
  # Collect every target's result in one CI attempt, without silently
  # accepting a failed assembler/linker as an absent optional probe.
  if ! check_cross_frames "$@"; then
    ((cross_failures+=1))
  fi
}

run_cross_frames rv32 riscv64-linux-gnu-as riscv64-linux-gnu-ld \
  '-march=rv32i -mabi=ilp32' '-m elf32lriscv' \
  '--target=riscv -march=rv32i -mabi=ilp32' 'addi x0,x0,0' 2
run_cross_frames rv64 riscv64-linux-gnu-as riscv64-linux-gnu-ld \
  '-march=rv64i -mabi=lp64' '-m elf64lriscv' \
  '--target=riscv -march=rv64i -mabi=lp64' 'addi x0,x0,0' 2
run_cross_frames aarch64 aarch64-linux-gnu-as aarch64-linux-gnu-ld \
  '-march=armv8-a' '' '--target=aarch64 -march=armv8-a -mabi=lp64' 'nop' 31
run_cross_frames loong loongarch64-linux-gnu-as loongarch64-linux-gnu-ld \
  '-mabi=lp64d' '' '--target=loongarch64 -march=loongarch64 -mabi=lp64d' \
  'addi.d $r0,$r0,0' 3
if ((cross_failures)); then
  echo "MINIAS_CFI_LINK=FAIL cross_profiles=$cross_failures" >&2
  exit 1
fi
