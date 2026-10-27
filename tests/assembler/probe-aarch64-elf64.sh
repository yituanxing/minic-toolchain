#!/usr/bin/env bash
set -Eeuo pipefail
: "${MINIAS:?MINIAS must be set}"
: "${AARCH64_AS:=aarch64-linux-gnu-as}"
: "${AARCH64_LD:=aarch64-linux-gnu-ld}"
: "${AARCH64_OBJCOPY:=aarch64-linux-gnu-objcopy}"
: "${AARCH64_READELF:=aarch64-linux-gnu-readelf}"
: "${QEMU_AARCH64:=qemu-aarch64}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# One real AArch64 instruction stream, ISA-specific immediate syntax,
# an external call relocation, a local jump and generic data relocations.
cat >"$work/vertical.s" <<'SRC'
.text
.globl _start
.type _start, @function
_start:
    nop // AArch64 inline comment
    mov x0, #0
    bl after_call
    b exit_code
after_call:
    ret
exit_code:
    mov x8, #93
    svc #0
.globl external_probe
external_probe:
    bl external_function
    b external_branch
.globl mc_operand_probe
.type mc_operand_probe, @function
mc_operand_probe:
    mov w3, #1234
    ret x9
.size mc_operand_probe, .-mc_operand_probe
.data
.globl function_pointer
function_pointer:
    .quad after_call
SRC
"$MINIAS" --target=aarch64 -march=armv8-a -mabi=lp64 \
    -o "$work/mini.o" "$work/vertical.s"
"$AARCH64_AS" -march=armv8-a -o "$work/gnu.o" "$work/vertical.s"
"$AARCH64_READELF" -h "$work/mini.o" | grep -q 'Machine:.*AArch64'
"$AARCH64_READELF" -h "$work/mini.o" | grep -q 'Class:.*ELF64'
"$AARCH64_OBJCOPY" -j .text -O binary "$work/mini.o" "$work/mini-text.bin"
"$AARCH64_OBJCOPY" -j .text -O binary "$work/gnu.o" "$work/gnu-text.bin"
cmp "$work/mini-text.bin" "$work/gnu-text.bin"
typed_a64_size="$("$AARCH64_READELF" -Ws "$work/mini.o" |
    awk '$NF == "mc_operand_probe" { print $3 }')"
test "$typed_a64_size" = "8"
echo 'MINIAS_AARCH64_PARSED_LAYOUT=PASS typed_4B=2 symbol_size=8'
"$AARCH64_READELF" -Wr "$work/mini.o" | grep -q 'R_AARCH64_CALL26'
"$AARCH64_READELF" -Wr "$work/mini.o" | grep -q 'R_AARCH64_JUMP26'
"$AARCH64_READELF" -Wr "$work/mini.o" | grep -q 'R_AARCH64_ABS64'
cat >"$work/external.s" <<'EXTERNAL'
.text
.globl external_function
external_function:
    ret
.globl external_branch
external_branch:
    ret
EXTERNAL
"$AARCH64_AS" -o "$work/external.o" "$work/external.s"
"$AARCH64_LD" -static -e _start -o "$work/mini.elf" "$work/mini.o" "$work/external.o"
"$QEMU_AARCH64" "$work/mini.elf"
# Invalid ISA/ABI and unsupported instructions must fail, not silently be
# assembled as RISC-V bytes or advertise a false ELF machine.
for option in '-march=rv64gc' '-mabi=ilp32' '-march=armv9-a'; do
    if "$MINIAS" --target=aarch64 "$option" -o "$work/bad.o" \
        "$work/vertical.s" >"$work/bad.stdout" 2>"$work/bad.stderr"; then
        echo "AARCH64_VSLICE=FAIL accepted invalid $option" >&2
        exit 1
    fi
done
printf '.text\nadd x0, x0, #1\n' >"$work/unsupported.s"
if "$MINIAS" --target=aarch64 -o "$work/unsupported.o" \
    "$work/unsupported.s" >"$work/bad.stdout" 2>"$work/bad.stderr"; then
    echo 'AARCH64_VSLICE=FAIL accepted unsupported instruction' >&2
    exit 1
fi
# MCInst must be validated while parsing, independent of later emit/ELF.
for bad_source in 'ret x31' 'mov x2, #65536' 'b external_function + 2'; do
    printf '.text\n%s\n' "$bad_source" >"$work/invalid-mcinst.s"
    if "$MINIAS" --target=aarch64 -o "$work/invalid-mcinst.o" \
        "$work/invalid-mcinst.s" >"$work/invalid-mcinst.out" \
        2>"$work/invalid-mcinst.err"; then
        echo "MiniAS accepted invalid AArch64 MCInst: $bad_source" >&2
        exit 1
    fi
done
# Only RISC-V's selected ISA gate forbids raw .word in executable
# sections. Keep this acceptance target-owned, not an accidental common
# assembler restriction on AArch64.
printf '.text\n.globl raw_a64\nraw_a64:\n.word 0xd503201f\n' \
    >"$work/aarch64-raw.s"
"$MINIAS" --target=aarch64 -o "$work/aarch64-raw.o" "$work/aarch64-raw.s"
"$AARCH64_AS" -o "$work/aarch64-raw-gnu.o" "$work/aarch64-raw.s"
"$AARCH64_OBJCOPY" -j .text -O binary "$work/aarch64-raw.o" \
    "$work/aarch64-raw.bin"
"$AARCH64_OBJCOPY" -j .text -O binary "$work/aarch64-raw-gnu.o" \
    "$work/aarch64-raw-gnu.bin"
cmp "$work/aarch64-raw.bin" "$work/aarch64-raw-gnu.bin"
echo 'MINIAS_AARCH64_MCINST=PASS typed_reg64_reg32_imm16_symbol=1 invalid_operands=3 target_raw_data=1'

echo 'MINIAS_AARCH64_VSLICE=PASS ELF64=1 gnu_bytes=equal relocation_call26_jump26_abs64=1 ld=pass qemu=exit0'
