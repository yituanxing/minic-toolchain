#!/usr/bin/env bash
# Check the actual shared production encoder, including unsigned DWARF values
# above INT64_MAX that the source-expression .uleb128 path cannot represent.
set -Eeuo pipefail
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
compiler=${CC:-cc}

"$compiler" -std=c11 -O2 -ffunction-sections -fdata-sections \
  -Iassembler/src -Ielf/include \
  -c assembler/src/assembler.c -o "$work/assembler.o"
"$compiler" -std=c11 -O2 -ffunction-sections -fdata-sections \
  -Iassembler/src -Ielf/include \
  -c assembler/src/common_lowering.c -o "$work/common_lowering.o"
for unit in common_context common_emission mc_leb; do
  "$compiler" -std=c11 -O2 -ffunction-sections -fdata-sections \
    -Iassembler/src -Ielf/include \
    -c "assembler/src/$unit.c" -o "$work/$unit.o"
done
"$compiler" -std=c11 -Wall -Wextra -Werror \
  -ffunction-sections -fdata-sections -Wl,--gc-sections \
  -Iassembler/src -Ielf/include \
  tests/assembler/probe-shared-dwarf-leb.c \
  "$work/assembler.o" "$work/common_lowering.o" \
  "$work/common_context.o" "$work/common_emission.o" "$work/mc_leb.o" \
  -o "$work/probe"
"$work/probe"
