#!/usr/bin/env bash
set -Eeuo pipefail
: "${MINIAS:?Set MINIAS to the exact-head assembler binary}"
for tool in readelf objcopy cmp python3; do command -v "$tool" >/dev/null; done

work=$(mktemp -d)
if [[ ${MINIAS_CFI_KEEP_WORK:-0} == 1 ]]; then
  echo "MINIAS_CFI_SOURCE_WORK=$work"
else
  trap 'rm -rf "$work"' EXIT
fi

# CFI is appended after the user has already opened the two real sections.
# GNU GAS pads a CIE to an absolute section boundary; odd user-authored
# prefixes show why padding the CIE relative to its own first byte is wrong.
for arch in x86_64 i386 riscv32 riscv64 aarch64 loongarch64; do
  if [[ -n ${MINIAS_CFI_ARCH_ONLY:-} && $arch != "$MINIAS_CFI_ARCH_ONLY" ]]; then
    continue
  fi
  case "$arch" in
    x86_64) mini=(--target=x86_64); gnu=(as --64); obj=objcopy
            cfa=7; insn=nop;;
    i386) mini=(--target=x86_64 -march=i386 -mabi=i386)
          gnu=(as --32); obj=objcopy; cfa=4; insn=nop;;
    riscv32) mini=(-march=rv32i -mabi=ilp32)
             gnu=(riscv64-linux-gnu-as -march=rv32i -mabi=ilp32)
             obj=riscv64-linux-gnu-objcopy
             cfa=2; insn='addi x0,x0,0';;
    riscv64) mini=(-march=rv64i -mabi=lp64)
             gnu=(riscv64-linux-gnu-as -march=rv64i -mabi=lp64)
             obj=riscv64-linux-gnu-objcopy
             cfa=2; insn='addi x0,x0,0';;
    aarch64) mini=(--target=aarch64); gnu=(aarch64-linux-gnu-as)
             obj=aarch64-linux-gnu-objcopy
             cfa=31; insn=nop;;
    loongarch64) mini=(--target=loongarch64); gnu=(loongarch64-linux-gnu-as)
                 obj=loongarch64-linux-gnu-objcopy
                 cfa=3; insn='addi.d $r0,$r0,0';;
  esac
  command -v "${gnu[0]}" >/dev/null
  command -v "$obj" >/dev/null
  for prefix in clean word byte; do
    if [[ $arch == x86_64 ]]; then
      # Keep the original failing GNU .s forms as stable, readable fixtures.
      case "$prefix" in
        clean) fixture=cfi-source-section-clean-gnu.S;;
        word) fixture=cfi-source-section-collision-gnu.S;;
        byte) fixture=cfi-source-section-unaligned-gnu.S;;
      esac
      cp "tests/assembler/fixtures/$fixture" "$work/source.s"
    else
      case "$prefix" in
        clean) seed='' ;;
        word) seed='.long 0' ;;
        byte) seed='.byte 0x5a' ;;
      esac
      {
        printf '.cfi_sections .eh_frame, .debug_frame\n'
        if [[ -n "$seed" ]]; then
          printf '.section .eh_frame,"a",@progbits\n%s\n' "$seed"
          printf '.section .debug_frame,"",@progbits\n%s\n' "$seed"
        fi
        printf '.text\n.globl cfi_source_collision\ncfi_source_collision:\n'
        printf '  .cfi_startproc\n  .cfi_def_cfa %s,8\n  %s\n  .cfi_endproc\n' "$cfa" "$insn"
      } >"$work/source.s"
    fi

    "$MINIAS" "${mini[@]}" -o "$work/mini.o" "$work/source.s"
    "${gnu[@]}" -o "$work/gnu.o" "$work/source.s"
    for section in .eh_frame .debug_frame; do
      "$obj" --dump-section "$section=$work/gnu.$section.bin" "$work/gnu.o"
      "$obj" --dump-section "$section=$work/mini.$section.bin" "$work/mini.o"
      if ! cmp -s "$work/gnu.$section.bin" "$work/mini.$section.bin"; then
        echo "MINIAS_CFI_SOURCE_COLLISION=FAIL arch=$arch prefix=$prefix section=$section bytes" >&2
        cmp "$work/gnu.$section.bin" "$work/mini.$section.bin" >&2 || true
        exit 1
      fi
    done
    readelf -rW "$work/gnu.o" >"$work/gnu.reloc"
    readelf -rW "$work/mini.o" >"$work/mini.reloc"
    python3 - "$arch" "$prefix" "$work" <<'PY'
from pathlib import Path
import re
import sys

arch, prefix, directory = sys.argv[1:]
directory = Path(directory)

def relocation_sites(producer):
    section = None
    rows = {}
    for line in (directory / f'{producer}.reloc').read_text().splitlines():
        header = re.search(r"Relocation section '([^']+)'", line)
        if header:
            section = header.group(1)
            continue
        row = re.match(r'^\s*([0-9a-fA-F]+)\s+\S+\s+(R_\S+)\s+', line)
        if row and section in ('.rela.eh_frame', '.rel.eh_frame',
                               '.rela.debug_frame', '.rel.debug_frame'):
            rows.setdefault(section, []).append((int(row.group(1), 16), row.group(2)))
    # Relocation-table order is not semantic; ADD then SUB at a shared
    # location remains a paired, linker-visible operation.
    for records in rows.values():
        records.sort()
    return rows

gnu, mini = relocation_sites('gnu'), relocation_sites('mini')
has_eh = gnu.get('.rela.eh_frame') or gnu.get('.rel.eh_frame')
has_debug = gnu.get('.rela.debug_frame') or gnu.get('.rel.debug_frame')
if gnu != mini or not has_eh or not has_debug:
    raise SystemExit(f'CFI GNU relocation mismatch arch={arch} prefix={prefix}: {gnu=} {mini=}')
PY
    if [[ $arch == loongarch64 && $prefix == word ]]; then
      # The new LoongArch CIE/FDE ADD/SUB length fixups must resolve to
      # the same bytes after a real GNU link, not just carry equal types.
      command -v loongarch64-linux-gnu-ld >/dev/null
      for who in gnu mini; do
        loongarch64-linux-gnu-ld -e cfi_source_collision \
          -o "$work/$who.elf" "$work/$who.o" \
          >"$work/$who.link.log" 2>&1
        "$obj" --dump-section ".debug_frame=$work/$who.linked-debug.bin" \
          "$work/$who.elf"
      done
      cmp "$work/gnu.linked-debug.bin" "$work/mini.linked-debug.bin"
    fi
    echo "MINIAS_CFI_SOURCE_COLLISION=PASS arch=$arch prefix=$prefix bytes=exact reloc_sites=exact"
  done
done
