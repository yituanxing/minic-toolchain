#!/usr/bin/env bash
# The exact Linux 6.6.143 CONFIG_AS_HAS_NON_CONST_LEB128 Kconfig probe
# must assemble in .text despite occupying only one byte. Compare the actual
# bytes and relocations against GAS, rather than merely accepting Kconfig.
set -Eeuo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mini=${MINIAS:-"$root/build/release/bin/minic-as"}
gas=${AARCH64_AS:-aarch64-linux-gnu-as}
objcopy=${AARCH64_OBJCOPY:-aarch64-linux-gnu-objcopy}
test -x "$mini"
command -v "$gas" >/dev/null
command -v "$objcopy" >/dev/null

work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

# Kconfig invokes AS on this GCC-generated sequence without an explicit
# section; the assembler must retain its implicit executable .text section.
fixture="$root/tests/assembler/fixtures/loong-linux-uleb-kbuild-feature-probe.S"
"$mini" --target=aarch64 -o "$work/mini-probe.o" "$fixture"
"$gas" -o "$work/gnu-probe.o" "$fixture"
for who in mini gnu; do
  "$objcopy" -j .text -O binary "$work/$who-probe.o" "$work/$who-probe.bin"
  test "$(od -An -tx1 "$work/$who-probe.bin" | tr -d ' \n')" = 00
done
cmp "$work/mini-probe.bin" "$work/gnu-probe.bin"

# Exercise true deferred layout, including a ULEB which grows from 1 to 2
# bytes after forward-symbol resolution and a negative signed difference.
# Unlike the Kconfig zero-width-difference example this cannot succeed by
# recognizing one hard-coded source spelling.
cat >"$work/nonconstant.s" <<'ASM'
.text
.globl leb_probe
leb_probe:
    .uleb128 .Lend - .Lstart
.Lstart:
    .space 128, 0x11
.Lend:
    .sleb128 .Lstart - .Lend
    .uleb128 .Lend - .Lstart + 16384
after_variable_leb:
    nop
.data
    .sleb128 -65
ASM
"$mini" --target=aarch64 -o "$work/mini-deferred.o" "$work/nonconstant.s"
"$gas" -o "$work/gnu-deferred.o" "$work/nonconstant.s"
for section in .text .data; do
  for who in mini gnu; do
    "$objcopy" -j "$section" -O binary \
      "$work/$who-deferred.o" "$work/$who-$section.bin"
  done
  cmp "$work/mini-$section.bin" "$work/gnu-$section.bin"
done
for who in mini gnu; do
  readelf -sW "$work/$who-deferred.o" | awk \
    '$8=="after_variable_leb" {print $8,$2}' >"$work/$who-deferred-symbols"
done
cmp "$work/mini-deferred-symbols" "$work/gnu-deferred-symbols"
# In this AArch64 source all differences are local and absolute. No RELA
# entries may remain after assembly; hidden relocations would invalidate a
# byte-only comparison.
for who in mini gnu; do
  if readelf -rW "$work/$who-deferred.o" | grep -q 'R_AARCH64_'; then
    echo "$who retained unexpected deferred LEB relocation" >&2
    exit 1
  fi
done

# GAS automatically inserts AArch64 instruction alignment after byte-sized
# data in an executable section. Check the resulting machine code bytes, not
# an invented rejection rule. The emitted nop itself must stay 4-byte aligned.
cat >"$work/misaligned.s" <<'ASM'
.text
    .uleb128 0
    nop
ASM
for who in mini gnu; do
  if [[ "$who" == mini ]]; then
    "$mini" --target=aarch64 -o "$work/$who-misaligned.o" \
      "$work/misaligned.s"
  else
    "$gas" -o "$work/$who-misaligned.o" "$work/misaligned.s"
  fi
  "$objcopy" -j .text -O binary "$work/$who-misaligned.o" \
    "$work/$who-aligned.bin"
  test "$(od -An -tx1 "$work/$who-aligned.bin" | tr -d ' \n')" = 000000001f2003d5
done
cmp "$work/mini-aligned.bin" "$work/gnu-aligned.bin"

# Labels immediately preceding automatically aligned instructions retain
# the position BEFORE the hidden padding in GAS. The following .inst checks
# that a raw machine word gets the same treatment as a mnemonic instruction.
cat >"$work/anchors.s" <<'ASM'
.text
    .byte 0x7f
before_nop:
    nop
    .byte 0x7e
before_inst:
    .inst 0xd503201f
ASM
for who in mini gnu; do
  if [[ "$who" == mini ]]; then
    "$mini" --target=aarch64 -o "$work/$who-anchors.o" "$work/anchors.s"
  else
    "$gas" -o "$work/$who-anchors.o" "$work/anchors.s"
  fi
  "$objcopy" -j .text -O binary "$work/$who-anchors.o" \
    "$work/$who-anchors.bin"
  readelf -sW "$work/$who-anchors.o" | awk \
    '$8=="before_nop" || $8=="before_inst" {print $8,$2}' | sort \
    >"$work/$who-anchors-symbols"
  # GNU can emit redundant adjacent $d markers for the explicit data and
  # its hidden alignment bytes. Normalize consecutive identical mapping
  # states, keeping all actual code/data transition offsets significant.
  readelf -sW "$work/$who-anchors.o" | awk \
    '$7=="1" && $8~/^\$[dx](\.[0-9]+)?$/ {print $2,substr($8,2,1)}' | \
    sort -k1,1 | awk '$2!=last {print;last=$2}' \
    >"$work/$who-anchors-mappings"
done
cmp "$work/mini-anchors.bin" "$work/gnu-anchors.bin"
cmp "$work/mini-anchors-symbols" "$work/gnu-anchors-symbols"
cmp "$work/mini-anchors-mappings" "$work/gnu-anchors-mappings"

# GNU's implicit code alignment belongs to executable sections, not to the
# opcode grammar globally. Even A64 machine instructions inside .data are
# allowed at odd offsets and must not gain a silent three-byte padding.
cat >"$work/data-instruction.s" <<'ASM'
.data
    .byte 0x7f
    nop
    .byte 0x7e
    .inst 0xd503201f
ASM
for who in mini gnu; do
  if [[ "$who" == mini ]]; then
    "$mini" --target=aarch64 -o "$work/$who-data-instruction.o" \
      "$work/data-instruction.s"
  else
    "$gas" -o "$work/$who-data-instruction.o" \
      "$work/data-instruction.s"
  fi
  "$objcopy" -j .data -O binary "$work/$who-data-instruction.o" \
    "$work/$who-data-instruction.bin"
done
cmp "$work/mini-data-instruction.bin" "$work/gnu-data-instruction.bin"

echo 'MINIAS_AARCH64_NONCONST_LEB128_GNU=PASS linux_kconfig_exact=1 deferred_width=exact signed=exact text_data=exact auto_aligned_instruction=exact prepadding_labels=exact inst=exact odd_data_instruction=exact'
