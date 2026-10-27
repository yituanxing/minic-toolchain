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
"$AARCH64_READELF" -Wr "$work/mini.o" | grep -q 'R_AARCH64_CALL26'
"$AARCH64_READELF" -Wr "$work/mini.o" | grep -q 'R_AARCH64_JUMP26'
"$AARCH64_READELF" -Wr "$work/mini.o" | grep -q 'R_AARCH64_ABS64'
"$AARCH64_LD" -static -e _start -o "$work/mini.elf" "$work/mini.o"
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
echo 'MINIAS_AARCH64_VSLICE=PASS ELF64=1 gnu_bytes=equal relocation_call26_jump26_abs64=1 ld=pass qemu=exit0'
