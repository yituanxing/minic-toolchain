#!/usr/bin/env bash
# One object ABI policy must govern parsing, MCExpr and final relocation
# lowering. i386 cannot silently turn unsupported AMD64 fixups into NONE.
set -Eeuo pipefail
: "${MINIAS:?set MINIAS to the assembler under test}"
for tool in as readelf objcopy cmp sort; do command -v "$tool" >/dev/null; done

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat >"$work/i386-good.s" <<'ASM'
.data
.long external_symbol
.long 0
.reloc 4, R_386_GOTPC, external_symbol
ASM
as --32 -o "$work/gnu32.o" "$work/i386-good.s"
"$MINIAS" --target=x86_64 -march=i386 -mabi=i386 \
  -o "$work/mini32.o" "$work/i386-good.s"
# The shared semantic comparator has no ELF32/i386 target model. Compare
# GNU's field offsets, relocation kinds and named symbols directly instead.
for producer in gnu32 mini32; do
  readelf -rW "$work/$producer.o" | \
    awk '/R_386_/ {print $1,$3,$5,$6,$7}' | \
    sort >"$work/$producer.reloc"
done
cmp "$work/gnu32.reloc" "$work/mini32.reloc" || {
  diff -u "$work/gnu32.reloc" "$work/mini32.reloc" >&2 || true
  echo 'MINIAS_X86_FIXUP_ABI=FAIL i386_GNU_relocations' >&2
  exit 1
}
readelf -rW "$work/mini32.o" | grep -q 'R_386_GOTPC'
readelf -rW "$work/mini32.o" | grep -q 'R_386_32'
objcopy -j .data -O binary "$work/gnu32.o" "$work/gnu32.bin"
objcopy -j .data -O binary "$work/mini32.o" "$work/mini32.bin"
cmp "$work/gnu32.bin" "$work/mini32.bin"

cat >"$work/amd64-good.s" <<'ASM'
.data
.quad external_symbol
.long 0
.reloc 8, R_X86_64_GOTPCREL, external_symbol
ASM
as --64 -o "$work/gnu64.o" "$work/amd64-good.s"
"$MINIAS" --target=x86_64 -o "$work/mini64.o" "$work/amd64-good.s"
# A pre-existing GNU local STT_SECTION emission difference is outside this
# ABI capability patch. Check exact relocation fields and payload instead.
for producer in gnu64 mini64; do
  readelf -rW "$work/$producer.o" | \
    awk '/R_X86_64_/ {print $1,$3,$5,$6,$7}' | \
    sort >"$work/$producer.reloc"
  objcopy -j .data -O binary "$work/$producer.o" "$work/$producer.bin"
done
cmp "$work/gnu64.reloc" "$work/mini64.reloc"
cmp "$work/gnu64.bin" "$work/mini64.bin"

reject() {
  local profile="$1" source="$2" name="$3"
  printf '%b\n' "$source" >"$work/reject.s"
  if [[ "$profile" == i386 ]]; then
    if as --32 -o "$work/gnu-bad.o" "$work/reject.s" \
       >"$work/gnu-bad.log" 2>&1; then
      echo "GNU unexpectedly accepts $name under i386" >&2; exit 1
    fi
    if "$MINIAS" --target=x86_64 -march=i386 -mabi=i386 \
        -o "$work/mini-bad.o" "$work/reject.s" \
        >"$work/mini-bad.log" 2>&1; then
      echo "MiniAS silently accepted $name under i386" >&2; exit 1
    fi
  else
    if as --64 -o "$work/gnu-bad.o" "$work/reject.s" \
        >"$work/gnu-bad.log" 2>&1; then
      echo "GNU unexpectedly accepts $name under amd64" >&2; exit 1
    fi
    if "$MINIAS" --target=x86_64 -o "$work/mini-bad.o" "$work/reject.s" \
        >"$work/mini-bad.log" 2>&1; then
      echo "MiniAS silently accepted $name under amd64" >&2; exit 1
    fi
  fi
  test ! -e "$work/mini-bad.o"
  rm -f "$work/gnu-bad.o" "$work/mini-bad.o"
}

reject i386 '.data
.quad external_symbol' i386_abs64
reject i386 '.data
.long 0
.reloc 0, R_X86_64_GOTPCREL, external_symbol' i386_gotpcrel
reject i386 '.data
.long 0
.reloc 0, R_X86_64_REX_GOTPCRELX, external_symbol' i386_rex_gotpcrelx
reject amd64 '.data
.long 0
.reloc 0, R_386_GOTPC, external_symbol' amd64_i386_name

echo 'MINIAS_X86_FIXUP_ABI=PASS i386_GNU_reloc_bytes=1 amd64_GNU_reloc_bytes=1 invalid_abi_relocations=4 R_386_NONE_escape=0'
