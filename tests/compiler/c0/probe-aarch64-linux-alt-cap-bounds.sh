#!/usr/bin/env bash
# Verify the two *different* Linux arm64 ELF feature alternatives through
# their entire legal capability encoding range, including the callback bit.
# This complements the GNU/QEMU one-value execution probe. The machine must
# never truncate/alias out-of-range cpucaps into an apparently valid .hword.
set -Eeuo pipefail
minic="${1:?expected minic-cc}"
test -x "$minic"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
source="$root/tests/compiler/c0/aarch64-linux-cap-alternative.c"

compile_variant() {
    local label="$1" cap="$2"
    sed "s/38UL/${cap}UL/g" "$source" >"$work/$label.c"
    cc -E -P -x c "$work/$label.c" -o "$work/$label.i"
    "$minic" --target=aarch64-linux -S "$work/$label.i" \
        -o "$work/$label.s"
}

for cap in 0 1 31 127 255 1023 4095 16383 32767; do
    compile_variant "cap$cap" "$cap"
    asm="$work/cap$cap.s"
    # Non-callback cap: one 16-bit CPU feature value; callback cap: bit15
    # signals patch-via-callback instead of replacing an instruction.
    grep -Fq " .hword $cap" "$asm"
    grep -Fq " .hword (1 << 15) | ($cap)" "$asm"
    test "$(grep -Fc '.pushsection .altinstructions' "$asm")" -eq 2
    test "$(grep -Fc '.word alt_cb_patch_nops- .' "$asm")" -eq 1
    test "$(grep -Fc '.word 663f - .' "$asm")" -eq 1
    test "$(grep -Fc '.byte 662b-661b' "$asm")" -eq 2
    test "$(grep -Fc '.byte 664f-663f' "$asm")" -eq 2
    test "$(grep -Fc '.previous' "$asm")" -eq 1
    test "$(grep -Fc '.subsection 1' "$asm")" -eq 1
    # Both asm-goto branches must name real emitted Core block labels;
    # neither may be erased or redirected to an invented local label.
    test "$(grep -Ec $'^\tb\t\\.Lmini_arm_f[0-9]+_b[0-9]+' "$asm")" -eq 2
done

# 32768 already overlaps the callback marker. 65535/65536 test hword
# overflow, and 4294967295 catches host-int coercion or silent wrapping.
# The source is otherwise legitimate: a reject must come from the target
# number-width contract, and must not leave behind a misleading .s file.
for cap in 32768 32769 65535 65536 4294967295; do
    sed "s/38UL/${cap}UL/g" "$source" >"$work/reject$cap.c"
    cc -E -P -x c "$work/reject$cap.c" -o "$work/reject$cap.i"
    if "$minic" --target=aarch64-linux -S "$work/reject$cap.i" \
        -o "$work/reject$cap.s" >"$work/reject$cap.log" 2>&1; then
        echo "Linux arm64 cap $cap silently truncated into .hword" >&2
        exit 1
    fi
    test ! -e "$work/reject$cap.s"
done

# GNU's accepted immediate expression can be assembly-checked when the
# actual cross assembler is present. This is NOT a QEMU/kernel proof.
if [[ "${MINIC_SOURCE_ONLY:-0}" != 1 ]]; then
    command -v aarch64-linux-gnu-gcc >/dev/null
    for cap in 0 127 4095 32767; do
        aarch64-linux-gnu-gcc -c -x assembler "$work/cap$cap.s" \
            -o "$work/cap$cap.o"
        readelf -W -S "$work/cap$cap.o" | grep -Fq '.altinstructions'
        # Two 12-byte Linux alt_instr records: one direct alternate and one
        # callback alternate, with relocation expressions left for the link.
        size_hex="$(objdump -h "$work/cap$cap.o" |
            awk '$2 == ".altinstructions" { print $3 }')"
        test "$size_hex" == '00000018'
        readelf -r "$work/cap$cap.o" | grep -Fq 'alt_cb_patch_nops'
    done
    echo 'AARCH64_ALT_CAP_BOUNDS=PASS caps=9 rejected=5 GAS_ELF=4 alt_size=24B'
else
    echo 'AARCH64_ALT_CAP_BOUNDS=PASS caps=9 rejected=5 source=only'
fi
