#!/usr/bin/env bash
# GNU LEB expressions are retained MCFragments: the encoded width depends on
# final SymbolID layout, while emitted bytes must be a typed snapshot.
set -Eeuo pipefail
test -n "$MINIAS"
ARCH="${ARCH:-x86_64}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
case "$ARCH" in
  x86_64) target=x86_64; gas=as; copy=objcopy;;
  riscv) target=riscv; gas=riscv64-linux-gnu-as; copy=riscv64-linux-gnu-objcopy;;
  aarch64) target=aarch64; gas=aarch64-linux-gnu-as; copy=aarch64-linux-gnu-objcopy;;
  loongarch64) target=loongarch64; gas=loongarch64-linux-gnu-as; copy=loongarch64-linux-gnu-objcopy;;
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
  .sleb128 2b-3f
  .space 64
3:
  .sleb128 -65
  .uleb128 (3b-2b)+127
  .byte 0xa5
SRC
MINIAS_TRACE_RELAYOUT=1 "$MINIAS" --target="$target" -o "$work/mini.o" \
  "$work/leb.s" 2>"$work/trace"
"$gas" -o "$work/gnu.o" "$work/leb.s"
grep -Eq '^minias-mcexpr-variable:typed=[1-9][0-9]* fallback=0$' "$work/trace"
"$copy" -j .data -O binary "$work/mini.o" "$work/mini.bin"
"$copy" -j .data -O binary "$work/gnu.o" "$work/gnu.bin"
cmp "$work/mini.bin" "$work/gnu.bin"

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
echo "MINIAS_DEFERRED_LEB_GNU=PASS arch=$ARCH typed_symbols=1 fixedpoint=1 mutable_set=1 GNU_exact=1 invalid_rejected=3"
