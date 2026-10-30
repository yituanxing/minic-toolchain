#!/usr/bin/env bash
set -Eeuo pipefail

: "${MINIAS:?Set MINIAS to the exact-head assembler binary}"
for tool in as readelf cmp python3; do command -v "$tool" >/dev/null; done

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# Both programs have identical CFI values.  The expression spelling mirrors
# real Linux LoongArch VDSO's `.cfi_offset 1, 8 + 1 * 8` and checks the full
# Common absolute-expression path, including an already-defined .set symbol.
cat >"$work/expr.s" <<'ASM'
.set CFI_SLOT,8
.text
.globl cfi_math
cfi_math:
.cfi_startproc
.cfi_return_column (1 - 1)
.cfi_def_cfa (2 + 5), (4 * 4)
.cfi_offset 1, 8 + 1 * 8
nop
.cfi_rel_offset 2, CFI_SLOT + 1 * 8
.cfi_val_offset 4, -((8 + 8))
.cfi_register (1 + 2), (2 * 2)
.cfi_undefined (2 + 3)
.cfi_same_value (2 * 3)
.cfi_restore 1 * 1
.cfi_def_cfa_register (3 + 4)
.cfi_def_cfa_offset (4 * 8)
.cfi_adjust_cfa_offset (8 - 4) * 2
.cfi_escape (1 - 1)
nop
.cfi_endproc
ASM

cat >"$work/literal.s" <<'ASM'
.set CFI_SLOT,8
.text
.globl cfi_math
cfi_math:
.cfi_startproc
.cfi_return_column 0
.cfi_def_cfa 7,16
.cfi_offset 1,16
nop
.cfi_rel_offset 2,16
.cfi_val_offset 4,-16
.cfi_register 3,4
.cfi_undefined 5
.cfi_same_value 6
.cfi_restore 1
.cfi_def_cfa_register 7
.cfi_def_cfa_offset 32
.cfi_adjust_cfa_offset 8
.cfi_escape 0
nop
.cfi_endproc
ASM

for mode in .eh_frame .debug_frame; do
  for arch in x86_64 i386 riscv loongarch64; do
    case "$arch" in
      x86_64) opts=(--target=x86_64); gnu_args=(--64) ;;
      i386) opts=(--target=x86_64 -march=i386 -mabi=i386); gnu_args=(--32) ;;
      riscv) opts=(--target=riscv -march=rv64i -mabi=lp64); gnu_args=() ;;
      loongarch64) opts=(--target=loongarch64); gnu_args=() ;;
    esac
    for producer in expr literal; do
      # No code copy/paste or target-specific parsing is involved: the same
      # CFI source is assembled for all four target profiles.
      sed "1i\\.cfi_sections $mode" "$work/$producer.s" >"$work/in.s"
      "$MINIAS" "${opts[@]}" -o "$work/$producer.mini.o" "$work/in.s"
      # Host objcopy may be built with x86-only BFD targets.  readelf can
      # inspect raw CFI bytes across all four ELF e_machine families.
      readelf -x "$mode" "$work/$producer.mini.o" \
          >"$work/$producer.mini.bytes"
      if [[ "${#gnu_args[@]}" != 0 ]]; then
        as "${gnu_args[@]}" -o "$work/$producer.gnu.o" "$work/in.s"
        readelf -x "$mode" "$work/$producer.gnu.o" \
            >"$work/$producer.gnu.bytes"
      fi
    done
    cmp "$work/expr.mini.bytes" "$work/literal.mini.bytes"
    if [[ "${#gnu_args[@]}" != 0 ]]; then
      cmp "$work/expr.gnu.bytes" "$work/literal.gnu.bytes"
      cmp "$work/expr.mini.bytes" "$work/expr.gnu.bytes"
    fi
    echo "MINIAS_CFI_EXPRESSION=PASS arch=$arch section=$mode constant_arithmetic=exact GNU=${#gnu_args[@]}"
  done
done

# Signed two's-complement wrap is accepted by GAS as a 64-bit absolute
# expression, although a literal beyond UINT64_MAX is rejected.  The
# intermediate operation must never overflow host signed arithmetic.
for src in '9223372036854775807 + 1' '18446744073709551616'; do
  cat >"$work/check.s" <<ASM
.text
f:
.cfi_startproc
.cfi_offset 1, $src
nop
.cfi_endproc
ASM
  if [[ "$src" == 9223* ]]; then
    as --64 -o "$work/gnu.o" "$work/check.s"
    "$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/check.s"
    readelf -x .eh_frame "$work/gnu.o" >"$work/gnu.bytes"
    readelf -x .eh_frame "$work/mini.o" >"$work/mini.bytes"
    cmp "$work/gnu.bytes" "$work/mini.bytes"
    echo 'MINIAS_CFI_EXPRESSION=PASS signed_64bit_wrap=GNU_exact'
  else
    if as --64 -o "$work/gnu.o" "$work/check.s" >/dev/null 2>&1 ||
       "$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/check.s" >/dev/null 2>&1; then
      echo 'CFI accepted a literal overflowing uint64' >&2
      exit 1
    fi
  fi
done

# GNU GAS and MiniAS must both reject non-absolute, invalid and misaligned
# CFI numeric operands; a .text label must not be coerced to its parse-time
# current-location value just because the assembler knows its section.
for operand in '1 / 0' 'UNDEFINED_CFI_SYMBOL' '8 + cfi_bad' '8 + 1' '18446744073709551616'; do
  cat >"$work/reject.s" <<ASM
.text
cfi_bad:
.cfi_startproc
.cfi_offset 1, $operand
nop
.cfi_endproc
ASM
  if as --64 -o "$work/gnu.o" "$work/reject.s" >/dev/null 2>&1; then
    echo "GNU GAS unexpectedly accepted '$operand'" >&2
    exit 1
  fi
  if "$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/reject.s" >/dev/null 2>&1; then
    echo "MiniAS unexpectedly accepted '$operand'" >&2
    exit 1
  fi
done
echo 'MINIAS_CFI_EXPRESSION_REJECT=PASS invalid=5 GNU_exact'
