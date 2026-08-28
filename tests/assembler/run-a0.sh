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
  seqz a1, a2
  neg a2, a3
  srl a3, a4, a5
  sll a4, a5, a6
  ret
.size boolize, .-boolize
.section .rodata,"a"
msg:
  .asciz "A\n\x42\101"
raw:
  .ascii "B" "C"
.previous
  nop
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
test "$text_hex" = "3335b000933516003306d040b356f700339707016780000013000000"
test "$rodata_hex" = "410a4241004243"

cat >"$work/numeric-labels.s" <<'EOF'
.text
.globl numeric_labels
.type numeric_labels, @function
numeric_labels:
  li a0, 0
  beqz a0, 1f
1:
  addi a0, a0, 1
  bnez a0, 1b
1:
  ret
.size numeric_labels, .-numeric_labels
EOF
"$MINIAS" -o "$work/numeric-labels.o" "$work/numeric-labels.s"
readelf -s "$work/numeric-labels.o" >"$work/numeric-labels.txt"
grep -q '.Lminias_num_1_1' "$work/numeric-labels.txt"
grep -q '.Lminias_num_1_2' "$work/numeric-labels.txt"

cat >"$work/isa-next.s" <<'EOF'
.text
.globl isa_next
.type isa_next, @function
isa_next:
  li a0, 0x123456789abcdef0
  sra a0, a1, a2
  fence rw, rw
  csrr a1, sstatus
  ebreak
  pause
  amoadd.w a0, a1, (a2)
  wfi
  amoadd.d a0, a1, (a2)
  csrs sstatus, a0
  amoand.w a0, a1, (a2)
  csrc 0x100, t3
  amoand.d a0, a1, (a2)
  ret
.size isa_next, .-isa_next
EOF
"$MINIAS" -o "$work/isa-next.o" "$work/isa-next.s"
isa_hex="$(
    readelf -x .text "$work/isa-next.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]+$/ && length($i) <= 8 && length($i) % 2 == 0) printf "%s", $i}'
)"
test "$isa_hex" = "13052001131585001305450313158500130565051315850013058507131585001305a509131585001305c50b131585001305e50d131585001305050f33d5c5400f003003f3250010730010000f0000012f25b600730050102f35b600732005102f25b66073300e102f35b66067800000"

cat >"$work/csr-amo.s" <<'EOF'
.text
.globl csr_amo
.type csr_amo, @function
csr_amo:
  csrrc a0, sstatus, a1
  amoor.w a0, a1, (a2)
  amoxor.w a0, a1, (a2)
  amoor.d.aqrl a0, a1, (a2)
  ret
.size csr_amo, .-csr_amo
EOF
"$MINIAS" -o "$work/csr-amo.o" "$work/csr-amo.s"
csr_amo_hex="$(
    readelf -x .text "$work/csr-amo.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]+$/ && length($i) <= 8 && length($i) % 2 == 0) printf "%s", $i}'
)"
test "$csr_amo_hex" = "73b505102f25b6402f25b6202f35b64667800000"

cat >"$work/section-stack.s" <<'EOF'
.text
.globl section_stack
.type section_stack, @function
section_stack:
  nop
.pushsection .rodata.minias,"a"
2:
  .byte 170
  .org 2b + 4
  .byte 187
.popsection
  ret
.size section_stack, .-section_stack
EOF
"$MINIAS" -o "$work/section-stack.o" "$work/section-stack.s"
stack_text="$(
    readelf -x .text "$work/section-stack.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]+$/ && length($i) <= 8 && length($i) % 2 == 0) printf "%s", $i}'
)"
stack_data="$(
    readelf -x .rodata.minias "$work/section-stack.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]+$/ && length($i) <= 8 && length($i) % 2 == 0) printf "%s", $i}'
)"
test "$stack_text" = "1300000067800000"
test "$stack_data" = "aa000000bb"

cat >"$work/local-difference.s" <<'EOF'
.data
1:
  .byte 0
  .word 1b - .
EOF
"$MINIAS" -o "$work/local-difference.o" "$work/local-difference.s"
difference_hex="$(
    readelf -x .data "$work/local-difference.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]+$/ && length($i) <= 8 && length($i) % 2 == 0) printf "%s", $i}'
)"
test "$difference_hex" = "00ffffffff"

cat >"$work/inline-atomic-loop.s" <<'EOF'
.text
.globl inline_atomic_loop
.type inline_atomic_loop, @function
inline_atomic_loop:
  0: lr.w t0, (t2)
  beq t0, t4, 1f
  add t1, t0, t3
  sc.w.rl t1, t1, (t2)
  bnez t1, 0b
  1: ret
.size inline_atomic_loop, .-inline_atomic_loop
EOF
"$MINIAS" -o "$work/inline-atomic-loop.o" "$work/inline-atomic-loop.s"
inline_atomic_hex="$(
    readelf -x .text "$work/inline-atomic-loop.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]+$/ && length($i) <= 8 && length($i) % 2 == 0) printf "%s", $i}'
)"
test "$inline_atomic_hex" = "afa203106388d2013383c2012fa3631ae31803fe67800000"
readelf -s "$work/inline-atomic-loop.o" >"$work/inline-atomic-loop.txt"
grep -q '.Lminias_num_0_1' "$work/inline-atomic-loop.txt"
grep -q '.Lminias_num_1_1' "$work/inline-atomic-loop.txt"

echo "MINIAS_A0=PASS objects=11 format=ELF64-RISCV-ET_REL relocations=4 strings=2 pseudos=6 previous=1 numeric_labels=6 isa_next=13 csr_amo=4 section_stack=1 org=1 local_difference=1 lr_sc=2 inline_labels=2"
