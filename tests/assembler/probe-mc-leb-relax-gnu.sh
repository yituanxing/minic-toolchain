#!/usr/bin/env bash
# This is a distinct TARGET ELF relaxation contract, not the Common LEB
# encoding oracle. GNU GAS records linker-visible ULEB SET/SUB fixups when
# target relaxation is enabled. Missing relocations must not be counted as
# a byte-parity success: they can break DWARF after link-time code shrinking.
set -Eeuo pipefail
test -n "$MINIAS"
ARCH="${ARCH:?Set ARCH=riscv or loongarch64}"
case "$ARCH" in
  riscv)
    target=riscv
    gas=riscv64-linux-gnu-as
    copy=riscv64-linux-gnu-objcopy
    set_reloc=R_RISCV_SET_ULEB128
    sub_reloc=R_RISCV_SUB_ULEB128
    ;;
  loongarch64)
    target=loongarch64
    gas=loongarch64-linux-gnu-as
    copy=loongarch64-linux-gnu-objcopy
    set_reloc=R_LARCH_ADD_ULEB128
    sub_reloc=R_LARCH_SUB_ULEB128
    ;;
  *) echo "unsupported GNU LEB relaxation target: $ARCH" >&2; exit 2;;
esac
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/leb-relax.s" <<'ASM'
.text
.Lstart:
ASM
# The anchors must straddle a real target linker-relaxable instruction.
# LoongArch GAS emits no ADD/SUB_ULEB128 for a constant-size `.byte` span;
# even under -mrelax it only forces the pair when such a fragment intervenes.
# These pseudos are also understood by MiniAS's ordinary target parsers.
case "$ARCH" in
  riscv) printf '  la a0,external_data\n' >>"$work/leb-relax.s";;
  loongarch64) printf '  la.global $r12,external_data\n' >>"$work/leb-relax.s";;
esac
cat >>"$work/leb-relax.s" <<'ASM'
.Lend:
.section .debug_rnglists,"",@progbits
  .uleb128 .Lend-.Lstart
ASM
"$gas" -mrelax -o "$work/gnu.o" "$work/leb-relax.s"
"$MINIAS" --target="$target" -o "$work/mini.o" "$work/leb-relax.s"
readelf -rW "$work/gnu.o" >"$work/gnu.reloc"
readelf -rW "$work/mini.o" >"$work/mini.reloc"
for kind in "$set_reloc" "$sub_reloc"; do
  if ! grep -q "$kind" "$work/gnu.reloc"; then
    echo "GNU target oracle lacks required relocation $kind" >&2
    exit 2
  fi
  if ! grep -q "$kind" "$work/mini.reloc"; then
    echo "MINIAS_RELAX_ULEB128=FAIL arch=$ARCH missing=$kind GNU_target_relax=enabled" >&2
    exit 1
  fi
done
# Exact ELF contract: section-symbol identity, relocation field offset,
# relocation kind and addends must agree, not merely the number of pairs.
# Dynamic symbol indices legitimately differ if unrelated ELF symbols are
# retained, so compare semantic columns after both tools' symbol resolution.
awk '/R_(RISCV_(SET|SUB)_ULEB128|LARCH_(ADD|SUB)_ULEB128)/ {print $1, $3, $5, $6, $7}' \
    "$work/gnu.reloc" >"$work/gnu.uleb-rela"
awk '/R_(RISCV_(SET|SUB)_ULEB128|LARCH_(ADD|SUB)_ULEB128)/ {print $1, $3, $5, $6, $7}' \
    "$work/mini.reloc" >"$work/mini.uleb-rela"
cmp "$work/mini.uleb-rela" "$work/gnu.uleb-rela"
"$copy" -j .debug_rnglists -O binary "$work/gnu.o" "$work/gnu.bin"
"$copy" -j .debug_rnglists -O binary "$work/mini.o" "$work/mini.bin"
cmp "$work/mini.bin" "$work/gnu.bin"
echo "MINIAS_RELAX_ULEB128=PASS arch=$ARCH GNU_relocation_offsets_symbols_addends_exact=1 ULEB_payload_exact=1 target_relax=enabled"
