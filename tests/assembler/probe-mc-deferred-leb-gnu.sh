#!/usr/bin/env bash
# GNU LEB expressions are retained MCFragments: the encoded width depends on
# final SymbolID layout, while emitted bytes must be a typed snapshot.
set -Eeuo pipefail
test -n "$MINIAS"
ARCH="${ARCH:-x86_64}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
case "$ARCH" in
  x86_64) target=x86_64; gas=as; copy=objcopy; gas_flags=();;
  riscv) target=riscv; gas=riscv64-linux-gnu-as; copy=riscv64-linux-gnu-objcopy
    gas_flags=(-mrelax);;
  aarch64) target=aarch64; gas=aarch64-linux-gnu-as; copy=aarch64-linux-gnu-objcopy
    gas_flags=();;
  loongarch64) target=loongarch64; gas=loongarch64-linux-gnu-as; copy=loongarch64-linux-gnu-objcopy
    gas_flags=(-mrelax);;
  *) echo "unsupported ARCH=$ARCH" >&2; exit 2;;
esac

cat >"$work/leb.s" <<'SRC'
.data
.globl leb_start
leb_start:
  .set MUTABLE,4
  .uleb128 MUTABLE
  .set MUTABLE,9
  .uleb128 MUTABLE
  .set minias_num_2_1,77
1:
  .uleb128 2f-1b
  .space 127
2:
SRC
# GNU RISC-V GAS (including binutils 2.42) rejects nonconstant SLEB, and
# some releases crash in riscv_insert_uleb128_fixes after that diagnostic.
# Keep this an explicit target contract, rather than treating GNU failure
# as successful differential evidence. Other targets retain forward SLEB.
if [[ "$ARCH" == riscv ]]; then
    printf '  .sleb128 -65\n' >>"$work/leb.s"
else
    printf '  .sleb128 2b-3f\n' >>"$work/leb.s"
fi
cat >>"$work/leb.s" <<'SRC'
  .space 64
3:
  .sleb128 -65
  .uleb128 (3b-2b)+127
  .byte 0xa5
SRC
MINIAS_TRACE_RELAYOUT=1 "$MINIAS" --target="$target" -o "$work/mini.o" \
  "$work/leb.s" 2>"$work/trace"
# Use the GNU target's default link-relaxation behavior for symbolic ULEBs;
# their linker-visible pair may zero the raw data while preserving the
# encoded width. A separate constant-only source below isolates Common
# byte encoding under explicit -mno-relax without a configuration mismatch.
"$gas" "${gas_flags[@]}" -o "$work/gnu.o" "$work/leb.s"
grep -Eq '^minias-mcexpr-variable:typed=[1-9][0-9]* fallback=0$' "$work/trace"
"$copy" -j .data -O binary "$work/mini.o" "$work/mini.bin"
"$copy" -j .data -O binary "$work/gnu.o" "$work/gnu.bin"
if ! cmp "$work/mini.bin" "$work/gnu.bin"; then
  echo "MiniAS .data bytes:" >&2
  od -An -tx1 "$work/mini.bin" >&2
  echo "GNU .data bytes:" >&2
  od -An -tx1 "$work/gnu.bin" >&2
  readelf -rW "$work/mini.o" >&2
  readelf -rW "$work/gnu.o" >&2
  exit 1
fi

# RISC-V preserves ULEB SET/SUB pairs for link-time relaxation; LoongArch
# does so only if source anchors straddle a linker-relaxable fragment. Check
# complete recorded offsets/kinds/section-symbol addends, not just raw bytes.
if [[ "$ARCH" == riscv || "$ARCH" == loongarch64 ]]; then
  readelf -rW "$work/mini.o" >"$work/mini.rela"
  readelf -rW "$work/gnu.o" >"$work/gnu.rela"
  awk '/R_(RISCV_(SET|SUB)_ULEB128|LARCH_(ADD|SUB)_ULEB128)/ { print $1, $3, $5, $6, $7 }' \
     "$work/mini.rela" >"$work/mini.uleb-rela"
  awk '/R_(RISCV_(SET|SUB)_ULEB128|LARCH_(ADD|SUB)_ULEB128)/ { print $1, $3, $5, $6, $7 }' \
     "$work/gnu.rela" >"$work/gnu.uleb-rela"
  cmp "$work/mini.uleb-rela" "$work/gnu.uleb-rela"
fi

cat >"$work/constants.s" <<'SRC'
.data
  .uleb128 0
  .uleb128 127
  .uleb128 128
  .uleb128 16384
  .sleb128 -65
  .sleb128 -1
  .sleb128 63
SRC
"$MINIAS" --target="$target" -o "$work/mini-const.o" "$work/constants.s"
if [[ "$ARCH" == riscv || "$ARCH" == loongarch64 ]]; then
  "$gas" -mno-relax -o "$work/gnu-const.o" "$work/constants.s"
else
  "$gas" -o "$work/gnu-const.o" "$work/constants.s"
fi
"$copy" -j .data -O binary "$work/mini-const.o" "$work/mini-const.bin"
"$copy" -j .data -O binary "$work/gnu-const.o" "$work/gnu-const.bin"
cmp "$work/mini-const.bin" "$work/gnu-const.bin"

# Invalid cross-section and undefined differences cannot silently collapse
# to numeric addresses (and must not produce an output object).
for invalid in undefined cross_section negative_unsigned; do
  case "$invalid" in
    undefined) printf '.data\n.uleb128 missing-end\n' >"$work/invalid.s";;
    cross_section)
      printf '.data\na: .byte 1\n.uleb128 b-a\n.section .rodata,"a"\nb: .byte 1\n' >"$work/invalid.s";;
    negative_unsigned)
      printf '.data\na: .byte 1\n.uleb128 a-b\nb: .byte 1\n' >"$work/invalid.s";;
  esac
  if "$MINIAS" --target="$target" -o "$work/invalid.o" "$work/invalid.s" \
       2>"$work/invalid.err"; then
    echo "MiniAS accepted invalid deferred LEB: $invalid" >&2
    exit 1
  fi
  grep -q 'unresolved-typed-leb-data' "$work/invalid.err"
  test ! -e "$work/invalid.o"
done
echo "MINIAS_DEFERRED_LEB_GNU=PASS arch=$ARCH typed_symbols=1 fixedpoint=1 mutable_set=1 GNU_default_relax_ELF_exact=1 constant_norelax_exact=1 invalid_rejected=3"
