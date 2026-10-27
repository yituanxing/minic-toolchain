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
.arch armv8-a
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
# Type-and-size sensitive AArch64 integer/memory matrix: compare with GNU AS
# rather than trusting constants or treating successful ELF output as enough.
cat >"$work/integer-matrix.s" <<'A64_INT'
.arch armv8-a
.text
.globl integer_matrix
integer_matrix:
    mov x0, x1
    mov x29, sp
    mov sp, x29
    mov x2, 51711
    add x0, x0, x2
    add sp, sp, #16
    sub w2, w2, #1
    cmp x0, x1
    cmp w0, #5
    and x0, x0, x2
    orr w0, w1, w2
    eor x0, x0, x1
    ldr x2, [x0, #8]
    ldr w2, [sp, #12]
    str w1, [sp, #16]
    ldrb w1, [x0]
    strb w1, [x0, #1]
    ldrh w1, [x0, #2]
    strh w1, [x0, #4]
    stp x29, x30, [sp, -16]!
    ldp x29, x30, [sp], 16
    ret
A64_INT
"$MINIAS" --target=aarch64 -march=armv8-a -mabi=lp64 -o "$work/int-mini.o" "$work/integer-matrix.s"
"$AARCH64_AS" -march=armv8-a -o "$work/int-gnu.o" "$work/integer-matrix.s"
"$AARCH64_OBJCOPY" -j .text -O binary "$work/int-mini.o" "$work/int-mini.bin"
"$AARCH64_OBJCOPY" -j .text -O binary "$work/int-gnu.o" "$work/int-gnu.bin"
cmp "$work/int-mini.bin" "$work/int-gnu.bin"
for bad in 'add x0, w0, x1' 'cmp x0, #4096' 'ldr x0, [x1, -8]' \
           'stp x29, x30, [sp, -520]!' 'mov x0, #65536'; do
    printf '.arch armv8-a\n.text\n%s\n' "$bad" >"$work/bad-integer.s"
    if "$MINIAS" --target=aarch64 -o "$work/bad-integer.o" \
         "$work/bad-integer.s" >/dev/null 2>"$work/bad-integer.err"; then
        echo "MiniAS unexpectedly accepted invalid AArch64 integer operand: $bad" >&2
        exit 1
    fi
done
echo 'MINIAS_AARCH64_INTEGER_MATRIX=PASS gnu_text_bytes=equal instruction_families=11 strict_invalid=5'

# Invalid ISA/ABI and unsupported instructions must fail, not silently be
# assembled as RISC-V bytes or advertise a false ELF machine.
for option in '-march=rv64gc' '-mabi=ilp32' '-march=armv9-a'; do
    if "$MINIAS" --target=aarch64 "$option" -o "$work/bad.o" \
        "$work/vertical.s" >"$work/bad.stdout" 2>"$work/bad.stderr"; then
        echo "AARCH64_VSLICE=FAIL accepted invalid $option" >&2
        exit 1
    fi
done
printf '.arch armv9-a\n.text\nnop\n' >"$work/unsupported-arch.s"
if "$MINIAS" --target=aarch64 -o "$work/unsupported-arch.o" \
    "$work/unsupported-arch.s" >"$work/unsupported-arch.out" \
    2>"$work/unsupported-arch.err"; then
    echo 'AARCH64_VSLICE=FAIL accepted unsupported .arch feature' >&2
    exit 1
fi
grep -q 'unsupported-aarch64-architecture' "$work/unsupported-arch.err"
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
