#!/usr/bin/env bash
# Common variable DATA must compile expression operands once into stable
# MCExpr SymbolIDs. GNU is the byte oracle on all four real target profiles;
# the trace rejects silently falling back to source interpretation.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"
: "${ARCH:?Target required}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
trap 'echo "VARIABLE_EXPR_FAIL arch=$ARCH command=$BASH_COMMAND" >&2; test ! -f "$work/trace" || cat "$work/trace" >&2' ERR
case "$ARCH" in
  x86_64) mini=(--target=x86_64); gas=(as --64); copy=(objcopy);;
  riscv) mini=(--target=riscv)
    gas=(riscv64-linux-gnu-as -march=rv64i -mabi=lp64)
    copy=(riscv64-linux-gnu-objcopy);;
  aarch64) mini=(--target=aarch64)
    gas=(aarch64-linux-gnu-as); copy=(aarch64-linux-gnu-objcopy);;
  loongarch64) mini=(--target=loongarch64)
    gas=(loongarch64-linux-gnu-as); copy=(loongarch64-linux-gnu-objcopy);;
  *) echo "unsupported ARCH=$ARCH" >&2; exit 2;;
esac

cat >"$work/variable.s" <<'SRC'
.data
.globl variable_start
variable_start:
  .set MUTABLE,3
  .fill MUTABLE,1,0x31
  .set MUTABLE,5
  .fill MUTABLE,1,0x32
  .space span_end-span_begin,0x4f
  .space (span_end-span_begin)*2,0x33
  .fill (span_end-span_begin)/2,2,0x1234
dot_anchor:
  .space .-dot_anchor+3,0x66
fill_anchor:
  .fill .-fill_anchor+3,1,0xa4
  .skip span_end-span_begin,0x77
  .zero (span_end-span_begin)/2
  .fill -3,1,0xff
  .fill 0,8,0x01234567
  .byte 0xdd
.section .rodata,"a"
span_begin:
  .byte 1,2,3,4,5,6,7,8
span_end:
SRC
MINIAS_TRACE_RELAYOUT=1 "$MINIAS" "${mini[@]}" \
  -o "$work/mini.o" "$work/variable.s" 2>"$work/trace"
"${gas[@]}" -o "$work/gnu.o" "$work/variable.s"
grep -Eq '^minias-mcexpr-variable:typed=[1-9][0-9]* fallback=0$' "$work/trace"
grep -q '^minias-fragment-graph:anchors_built=1.*reparse=0$' "$work/trace"
for sec in .data .rodata; do
  "${copy[@]}" -j "$sec" -O binary "$work/mini.o" "$work/mini.${sec##.}"
  "${copy[@]}" -j "$sec" -O binary "$work/gnu.o" "$work/gnu.${sec##.}"
  cmp "$work/mini.${sec##.}" "$work/gnu.${sec##.}"
done
mini_value=$(readelf -sW "$work/mini.o" | awk '$NF=="variable_start" {print $2}')
gnu_value=$(readelf -sW "$work/gnu.o" | awk '$NF=="variable_start" {print $2}')
test "$mini_value" = "$gnu_value"

# Unsupported symbolic arithmetic remains a deliberately observable fallback;
# it must still assemble to the same bytes and not disappear as a no-op.
cat >"$work/fallback.s" <<'SRC'
.data
  .space ((span_end-span_begin)*2)+1,0x5a
  .byte 0xff
.section .rodata,"a"
span_begin: .byte 1,2,3
span_end:
SRC
MINIAS_TRACE_RELAYOUT=1 "$MINIAS" "${mini[@]}" \
  -o "$work/fallback-mini.o" "$work/fallback.s" 2>"$work/trace"
"${gas[@]}" -o "$work/fallback-gnu.o" "$work/fallback.s"
grep -q '^minias-mcexpr-variable:typed=0 fallback=1$' "$work/trace"
"${copy[@]}" -j .data -O binary "$work/fallback-mini.o" "$work/fallback-mini.bin"
"${copy[@]}" -j .data -O binary "$work/fallback-gnu.o" "$work/fallback-gnu.bin"
cmp "$work/fallback-mini.bin" "$work/fallback-gnu.bin"

# Invalid unresolved or cross-section spans must fail closed even when their
# arithmetic parses into a valid MCExpr node; no huge zero-filled object and
# no fabricated address arithmetic are acceptable.
for invalid in undefined cross_section; do
  case "$invalid" in
    undefined)
      printf '.data\n.space unknown_end-unknown_start,0x44\n' >"$work/bad.s";;
    cross_section)
      printf '.data\ndata_anchor: .byte 1\n.space ro_anchor-data_anchor\n.section .rodata,"a"\nro_anchor: .byte 1\n' >"$work/bad.s";;
  esac
  if "$MINIAS" "${mini[@]}" -o "$work/bad.o" \
      "$work/bad.s" 2>"$work/bad.err"; then
    echo "MiniAS accepted invalid typed variable difference: $invalid" >&2
    exit 1
  fi
  grep -q 'unresolved-typed-variable-data' "$work/bad.err"
  test ! -e "$work/bad.o"
done

echo "MINIAS_VARIABLE_MCEXPR_FIXEDPOINT=PASS arch=$ARCH typed_symbol_ids=1 lexical_mutable_set=1 forward_data=1 dot_anchor=1 scaled_difference=1 GNU_exact=1 fallback_explicit=1 invalid_rejected=2"
