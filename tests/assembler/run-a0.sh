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

echo "MINIAS_A0=PASS objects=2 format=ELF64-RISCV-ET_REL"
