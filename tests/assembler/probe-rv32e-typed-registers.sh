#!/usr/bin/env bash
# RV32E restricts actual GPR operands, not linker symbol spellings.
# Accepting symbolic a6 is critical for GCC-generated names and macros.
set -Eeuo pipefail
: "$MINIAS"
command -v readelf >/dev/null
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
have_gnu=false
if command -v riscv64-linux-gnu-as >/dev/null; then have_gnu=true; fi

cat >"$work/symbols.s" <<'SRC'
.text
.globl a6
a6: nop
s2: nop
x16: nop
.macro jump_to target
  beq x1,x0,\target
.endm
jump_to s2
la x1,a6
jal x1,x16
call s2
SRC

"$MINIAS" --target=riscv -march=rv32e -mabi=ilp32e \
    -o "$work/symbols.o" "$work/symbols.s"
readelf -h "$work/symbols.o" | grep -q 'Class:.*ELF32'
readelf -sW "$work/symbols.o" | grep -q ' a6$'
readelf -Wr "$work/symbols.o" | grep -q 'R_RISCV_CALL_PLT'
"$MINIAS" --target=riscv -mabi=ilp32e \
    -o "$work/abi-only.o" "$work/symbols.s"

# The same spelling in a typed register slot must still fail, including
# memory base, symbolic STORE temporary, and multiword pseudo destinations.
for source in \
    'add a6,a0,a1' \
    'li x16,1' \
    'la t3,a6' \
    'lw a0,0(s2)' \
    'sw a0,a6,s2' \
    'jalr a0,0(s2)' \
    'mv a0,s2'; do
    printf '.text\na6: nop\n%s\n' "$source" >"$work/bad.s"
    if "$MINIAS" --target=riscv -march=rv32e -mabi=ilp32e \
        -o "$work/bad.o" "$work/bad.s" >"$work/bad.out" \
        2>"$work/bad.err"; then
        echo "RV32E accepted a forbidden typed GPR: $source" >&2
        exit 1
    fi
    grep -q 'riscv32e-invalid-register' "$work/bad.err"
    if "$have_gnu"; then
        if riscv64-linux-gnu-as -march=rv32e -mabi=ilp32e \
            -o "$work/bad.gnu.o" "$work/bad.s" \
            >"$work/bad.gnu.out" 2>"$work/bad.gnu.err"; then
            echo "GNU GAS unexpectedly accepted RV32E register: $source" >&2
            exit 1
        fi
    fi
done

cat >"$work/bad-macro.s" <<'SRC'
.macro add_bad reg
  add \reg,a0,a1
.endm
.text
add_bad a6
SRC
if "$MINIAS" --target=riscv -march=rv32e -mabi=ilp32e \
    -o "$work/bad-macro.o" "$work/bad-macro.s" \
    >"$work/bad-macro.out" 2>"$work/bad-macro.err"; then
    echo 'RV32E accepted forbidden GPR in expanded macro' >&2
    exit 1
fi
grep -q 'riscv32e-invalid-register' "$work/bad-macro.err"
if "$have_gnu"; then
    if riscv64-linux-gnu-as -march=rv32e -mabi=ilp32e \
        -o "$work/bad-macro.gnu.o" "$work/bad-macro.s" \
        >"$work/bad-macro.gnu.out" 2>"$work/bad-macro.gnu.err"; then
        echo 'GNU GAS unexpectedly accepted RV32E macro register' >&2
        exit 1
    fi
fi

# E ABI restriction is independent of an explicit -march. In contrast,
# the same register is permitted on full RV32I.
printf '.text\nli x16,1\n' >"$work/abi-register.s"
if "$MINIAS" --target=riscv -mabi=ilp32e \
    -o "$work/abi-register.o" "$work/abi-register.s" \
    >"$work/abi-register.out" 2>"$work/abi-register.err"; then
    echo 'ILP32E accepted x16 without an explicit -march' >&2
    exit 1
fi
grep -q 'riscv32e-invalid-register' "$work/abi-register.err"
"$MINIAS" --target=riscv -march=rv32i -mabi=ilp32 \
    -o "$work/rv32i-register.o" "$work/abi-register.s"

printf '.text\nc.li x16,1\n' >"$work/compressed-bad.s"
if "$MINIAS" --target=riscv -march=rv32ec -mabi=ilp32e \
    -o "$work/compressed-bad.o" "$work/compressed-bad.s" \
    >"$work/compressed-bad.out" 2>"$work/compressed-bad.err"; then
    echo 'RV32EC accepted x16 in a compressed instruction' >&2
    exit 1
fi
grep -q 'riscv32e-invalid-register' "$work/compressed-bad.err"
if "$have_gnu"; then
    if riscv64-linux-gnu-as -march=rv32ec -mabi=ilp32e \
        -o "$work/compressed-bad.gnu.o" "$work/compressed-bad.s" \
        >"$work/compressed-bad.gnu.out" 2>"$work/compressed-bad.gnu.err"; then
        echo 'GNU GAS unexpectedly accepted RV32EC register x16' >&2
        exit 1
    fi
fi

# A privileged CSR register slot is still GPR-typed even though its
# instruction family is introduced by the Zicsr extension.
printf '.text\ncsrrw a0,0x300,a6\n' >"$work/csr-bad.s"
if "$MINIAS" --target=riscv -march=rv32e_zicsr -mabi=ilp32e \
    -o "$work/csr-bad.o" "$work/csr-bad.s" \
    >"$work/csr-bad.out" 2>"$work/csr-bad.err"; then
    echo 'RV32E+Zicsr accepted a6 in a CSR register operand' >&2
    exit 1
fi
grep -q 'riscv32e-invalid-register' "$work/csr-bad.err"
if "$have_gnu"; then
    if riscv64-linux-gnu-as -march=rv32e_zicsr -mabi=ilp32e \
        -o "$work/csr-bad.gnu.o" "$work/csr-bad.s" \
        >"$work/csr-bad.gnu.out" 2>"$work/csr-bad.gnu.err"; then
        echo 'GNU GAS unexpectedly accepted RV32E CSR register a6' >&2
        exit 1
    fi
fi

# A CSR zimm is an encoded five-bit constant, not an integer register.
# RV32E must permit zimm=16..31 while continuing to reject x16..x31 in
# the corresponding CSR register forms. Exercise all three immediate opcodes
# and both full and zero-destination aliases through the same typed MC path.
cat >"$work/csr-zimm.s" <<'SRC'
.text
csrrwi a0,mstatus,31
csrrsi a1,mstatus,16
csrrci a2,mstatus,17
csrwi mscratch,31
csrsi mscratch,16
csrci mscratch,17
SRC
"$MINIAS" --target=riscv -march=rv32e_zicsr -mabi=ilp32e \
    -o "$work/csr-zimm.o" "$work/csr-zimm.s"
python3 - "$work/csr-zimm.o" <<'PY'
import re
import struct
import subprocess
import sys

text = subprocess.check_output(['readelf', '-x', '.text', sys.argv[1]], text=True)
payload = bytearray()
for line in text.splitlines():
    match = re.match(r'^\s*0x[0-9a-fA-F]+\s+(.+)$', line)
    if match:
        for cell in match.group(1).split():
            if not re.fullmatch(r'(?:[0-9a-fA-F]{2}){1,4}', cell):
                break
            payload.extend(bytes.fromhex(cell))
expected = bytearray()
for funct3, rd, zimm, csr in [
    (5, 10, 31, 0x300), (6, 11, 16, 0x300),
    (7, 12, 17, 0x300), (5, 0, 31, 0x340),
    (6, 0, 16, 0x340), (7, 0, 17, 0x340)
]:
    expected.extend(struct.pack('<I', 0x73 | (rd << 7) | (funct3 << 12) |
                                (zimm << 15) | (csr << 20)))
assert payload == expected, (payload.hex(), expected.hex())
PY
for instruction in \
    'csrwi mscratch,32' \
    'csrsi mscratch,-1' \
    'csrrwi a0,mstatus,32' \
    'csrrci a6,mstatus,31'; do
    printf '.text\n%s\n' "$instruction" >"$work/csr-zimm-bad.s"
    if "$MINIAS" --target=riscv -march=rv32e_zicsr -mabi=ilp32e \
        -o "$work/csr-zimm-bad.o" "$work/csr-zimm-bad.s" \
        >"$work/csr-zimm-bad.out" 2>"$work/csr-zimm-bad.err"; then
        echo "RV32E incorrectly accepted CSR immediate/register: $instruction" >&2
        exit 1
    fi
done
if "$have_gnu" && command -v riscv64-linux-gnu-objcopy >/dev/null; then
    riscv64-linux-gnu-as -march=rv32e_zicsr -mabi=ilp32e \
        -o "$work/csr-zimm.gnu.o" "$work/csr-zimm.s"
    riscv64-linux-gnu-objcopy -O binary --only-section=.text \
        "$work/csr-zimm.o" "$work/csr-zimm.text"
    riscv64-linux-gnu-objcopy -O binary --only-section=.text \
        "$work/csr-zimm.gnu.o" "$work/csr-zimm.gnu.text"
    cmp "$work/csr-zimm.text" "$work/csr-zimm.gnu.text"
fi

# Use GNU GAS as byte-level oracle where a RISC-V cross-assembler exists.
if "$have_gnu" && command -v riscv64-linux-gnu-objcopy >/dev/null; then
    riscv64-linux-gnu-as -march=rv32e -mabi=ilp32e \
        -o "$work/symbols.gnu.o" "$work/symbols.s"
    riscv64-linux-gnu-objcopy -O binary --only-section=.text \
        "$work/symbols.o" "$work/symbols.text"
    riscv64-linux-gnu-objcopy -O binary --only-section=.text \
        "$work/symbols.gnu.o" "$work/symbols.gnu.text"
    cmp "$work/symbols.text" "$work/symbols.gnu.text"
    echo 'MINIAS_RV32E_TYPED_REGISTERS=PASS GNU_bytes=equal symbols=3 negatives=10 csr_zimm=6 csr_zimm_rejected=4'
else
    echo 'MINIAS_RV32E_TYPED_REGISTERS=PASS GNU_bytes=SKIP(no-cross-tools) symbols=3 negatives=10 csr_zimm=6 csr_zimm_rejected=4'
fi
