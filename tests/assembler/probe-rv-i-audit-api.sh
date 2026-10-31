#!/usr/bin/env bash
# Preserve the stronger Base-I encoded-byte audit via an explicit C API.
# Ordinary -march=rv32i/rv64i is tested separately for GNU GAS behavior.
set -Eeuo pipefail
: "${MINIAS:?exact-head MiniAS binary required}"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
build_root=$(dirname "$(dirname "$MINIAS")")
objects=("$build_root"/obj/assembler/src/*.o
         "$build_root"/obj/elf/src/reader.o
         "$build_root"/obj/elf/src/relocatable_writer.o)
cc -std=c11 -Wall -Wextra -Werror -Iassembler/src -Ielf/include \
   tests/assembler/probe-rv-i-audit-api.c "${objects[@]}" \
   -o "$work/rv-i-audit"

checked=0
for xlen in 32 64; do
    printf '.text\naddi a0, a0, 1\nret\n' >"$work/input.s"
    "$work/rv-i-audit" "$work/input.s" "$work/good-$xlen.o" "$xlen"
    test -s "$work/good-$xlen.o"

    for escaped in '.word 0x02050533' '.org 8' \
                   '.insn r 0x33, 0, 1, a0, a0, a1' 'mul a0, a0, a1'; do
        printf '.text\n%s\n' "$escaped" >"$work/input.s"
        if "$work/rv-i-audit" "$work/input.s" "$work/bad-$xlen.o" "$xlen" \
                2>"$work/audit.err"; then
            echo "Base-I audit accepted forbidden encoding: RV$xlen $escaped" >&2
            exit 1
        fi
        test ! -e "$work/bad-$xlen.o"
        if [[ "$escaped" == mul* ]]; then
            grep -Fq "rv${xlen}i-unsupported-machine-encoding" "$work/audit.err"
        else
            grep -Fq 'unsupported-isa-escape:' "$work/audit.err"
        fi
        checked=$((checked + 1))
    done
done

echo "MINIAS_RV_I_AUDIT_API=PASS RV32I_RV64I=2 raw_or_extension_rejected=$checked typed_I_allowed=2"
