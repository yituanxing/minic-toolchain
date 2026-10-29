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

# Use GNU GAS as byte-level oracle where a RISC-V cross-assembler exists.
if "$have_gnu" && command -v riscv64-linux-gnu-objcopy >/dev/null; then
    riscv64-linux-gnu-as -march=rv32e -mabi=ilp32e \
        -o "$work/symbols.gnu.o" "$work/symbols.s"
    riscv64-linux-gnu-objcopy -O binary --only-section=.text \
        "$work/symbols.o" "$work/symbols.text"
    riscv64-linux-gnu-objcopy -O binary --only-section=.text \
        "$work/symbols.gnu.o" "$work/symbols.gnu.text"
    cmp "$work/symbols.text" "$work/symbols.gnu.text"
    echo 'MINIAS_RV32E_TYPED_REGISTERS=PASS GNU_bytes=equal symbols=3 negatives=10'
else
    echo 'MINIAS_RV32E_TYPED_REGISTERS=PASS GNU_bytes=SKIP(no-cross-tools) symbols=3 negatives=10'
fi
