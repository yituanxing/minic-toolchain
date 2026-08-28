#!/bin/sh
set -eu

: "${MINIAS:?MINIAS must point to minic-as}"
: "${BUILD_DIR:?BUILD_DIR must be set}"

work="$BUILD_DIR/tests/assembler/a0"
mkdir -p "$work"

assemble_and_check() {
    name="$1"
    immediate="$2"
    expected_hex="$3"

    cat >"$work/$name.s" <<EOF
.text
.globl main
.type main, @function
main:
  li a0, $immediate
  ret
.size main, .-main
EOF

    "$MINIAS" -o "$work/$name.o" "$work/$name.s"

    readelf -h "$work/$name.o" | grep -q 'REL (Relocatable file)'
    readelf -h "$work/$name.o" | grep -q 'RISC-V'
    readelf -s "$work/$name.o" | grep -Eq '[[:space:]]8[[:space:]]+FUNC[[:space:]]+GLOBAL.* main$'

    actual_hex="$(
        readelf -x .text "$work/$name.o" |
        awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]{8}$/) printf "%s", $i}'
    )"
    test "$actual_hex" = "$expected_hex"
}

assemble_and_check return_0 0 1305000067800000
assemble_and_check return_42 42 1305a00267800000

cat >"$work/data.s" <<'EOF'
.section .rodata,"a"
.align 0
.globl foo
.type foo, @object
foo:
  .word 1
  .word -2, 0x12345678
  .zero 3
.size foo, .-foo
EOF
"$MINIAS" -o "$work/data.o" "$work/data.s"
readelf -h "$work/data.o" | grep -q 'RISC-V'
readelf -s "$work/data.o" | grep -Eq '[[:space:]]15[[:space:]]+OBJECT[[:space:]]+GLOBAL.* foo'
data_hex="$(
    readelf -x .rodata "$work/data.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]+$/ && length($i) <= 8 && length($i) % 2 == 0) printf "%s", $i}'
)"
test "$data_hex" = "01000000feffffff78563412000000"

cat >"$work/reloc.s" <<'EOF'
.text
.globl reloc_user
.type reloc_user, @function
reloc_user:
  lla a0, external_object
  call external_func
  ret
.size reloc_user, .-reloc_user
.section .data,"aw"
.globl ptr
.type ptr, @object
ptr:
  .dword external_object + 8
.size ptr, 8
EOF
"$MINIAS" -o "$work/reloc.o" "$work/reloc.s"
readelf -h "$work/reloc.o" | grep -q 'RISC-V'
readelf -Wr "$work/reloc.o" >"$work/reloc.txt"
grep -q 'R_RISCV_PCREL_HI20.*external_object' "$work/reloc.txt"
grep -q 'R_RISCV_PCREL_LO12_I.*Lminias_pcrel' "$work/reloc.txt"
grep -q 'R_RISCV_64.*external_object.*+ 8' "$work/reloc.txt"
grep -q 'R_RISCV_CALL_PLT.*external_func' "$work/reloc.txt"
if grep -q 'R_RISCV_RELAX' "$work/reloc.txt"; then
    echo "unexpected relax relocation in A0 compiler-style object" >&2
    exit 1
fi

cat >"$work/string-pseudo.s" <<'EOF'
.text
.globl boolize
.type boolize, @function
boolize:
  snez a0, a1
  ret
.size boolize, .-boolize
.section .rodata,"a"
msg:
  .asciz "A\n\x42\101"
raw:
  .ascii "B" "C"
EOF
"$MINIAS" -o "$work/string-pseudo.o" "$work/string-pseudo.s"
text_hex="$(
    readelf -x .text "$work/string-pseudo.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]+$/ && length($i) <= 8 && length($i) % 2 == 0) printf "%s", $i}'
)"
rodata_hex="$(
    readelf -x .rodata "$work/string-pseudo.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]+$/ && length($i) <= 8 && length($i) % 2 == 0) printf "%s", $i}'
)"
test "$text_hex" = "3335b00067800000"
test "$rodata_hex" = "410a4241004243"

echo "MINIAS_A0=PASS objects=5 format=ELF64-RISCV-ET_REL relocations=4 strings=2 pseudos=1"
