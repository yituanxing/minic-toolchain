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

cat >"$work/ecall.s" <<'EOF'
.text
.globl ecall_user
.type ecall_user, @function
ecall_user:
  ecall
  ebreak
  ret
.size ecall_user, .-ecall_user
EOF
"$MINIAS" -o "$work/ecall.o" "$work/ecall.s"
ecall_hex="$(
    readelf -x .text "$work/ecall.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]+$/ && length($i) <= 8 && length($i) % 2 == 0) printf "%s", $i}'
)"
test "$ecall_hex" = "730000007300100067800000"

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
  csrrw a1, sstatus, a0
  amoor.w a0, a1, (a2)
  amoxor.w a0, a1, (a2)
  amoor.d.aqrl a0, a1, (a2)
  csrw 0x8, t3
  csrw 0xf, t6
  csrr a2, 0xc00 + 16 + 8 + 4 + 2 + 1
  ret
.size csr_amo, .-csr_amo
EOF
"$MINIAS" -o "$work/csr-amo.o" "$work/csr-amo.s"
csr_amo_hex="$(
    readelf -x .text "$work/csr-amo.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]+$/ && length($i) <= 8 && length($i) % 2 == 0) printf "%s", $i}'
)"
test "$csr_amo_hex" = "73b50510f31505102f25b6402f25b6202f35b64673108e007390ff007326f0c167800000"

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

cat >"$work/branch-pseudos.s" <<'EOF'
.text
.globl branch_pseudos
.type branch_pseudos, @function
branch_pseudos:
  bltz a0, 1f
  bgez a1, 1f
  bgtz a2, 1f
  blez a3, 1f
  bgt a4, a5, 1f
  ble a6, a7, 1f
  bgtu t0, t1, 1f
  bleu t2, t3, 1f
1:
  ret
.size branch_pseudos, .-branch_pseudos
EOF
"$MINIAS" -o "$work/branch-pseudos.o" "$work/branch-pseudos.s"
branch_pseudo_hex="$(
    readelf -x .text "$work/branch-pseudos.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]+$/ && length($i) <= 8 && length($i) % 2 == 0) printf "%s", $i}'
)"
test "$branch_pseudo_hex" = "6340050263de0500634cc000635ad00063c8e70063d608016364530063727e0067800000"

cat >"$work/extern.s" <<'EOF'
.extern __minic_deferred_asm_immediate_140_0
.text
.globl extern_user
.type extern_user, @function
extern_user:
  call __minic_deferred_asm_immediate_140_0
  ret
.size extern_user, .-extern_user
EOF
"$MINIAS" -o "$work/extern.o" "$work/extern.s"
readelf -Ws "$work/extern.o" >"$work/extern.txt"
grep -Eq 'GLOBAL[[:space:]]+DEFAULT[[:space:]]+UND[[:space:]]+__minic_deferred_asm_immediate_140_0$' "$work/extern.txt"
readelf -Wr "$work/extern.o" | grep -q 'R_RISCV_CALL_PLT.*__minic_deferred_asm_immediate_140_0'

cat >"$work/extern-diff.s" <<'EOF'
.extern external_delta_target
.data
.globl delta64
.type delta64, @object
delta64:
  .dword external_delta_target - .
.size delta64, 8
.globl delta32
.type delta32, @object
delta32:
  .word external_delta_target - .
.size delta32, 4
.globl abs32
.type abs32, @object
abs32:
  .4byte external_delta_target + 4
.size abs32, 4
EOF
"$MINIAS" -o "$work/extern-diff.o" "$work/extern-diff.s"
readelf -Wr "$work/extern-diff.o" >"$work/extern-diff.txt"
grep -q 'R_RISCV_ADD64.*external_delta_target' "$work/extern-diff.txt"
grep -q 'R_RISCV_SUB64.*Lminias_expr' "$work/extern-diff.txt"
grep -q 'R_RISCV_ADD32.*external_delta_target' "$work/extern-diff.txt"
grep -q 'R_RISCV_SUB32.*Lminias_expr' "$work/extern-diff.txt"
grep -q 'R_RISCV_32.*external_delta_target.*+ 4' "$work/extern-diff.txt"

cat >"$work/jal.s" <<'EOF'
.extern external_jump_target
.text
.globl jal_user
.type jal_user, @function
jal_user:
  jal zero, 1f
  jal ra, external_jump_target
1:
  ret
.size jal_user, .-jal_user
EOF
"$MINIAS" -o "$work/jal.o" "$work/jal.s"
readelf -Wr "$work/jal.o" >"$work/jal.txt"
grep -q 'R_RISCV_JAL.*external_jump_target' "$work/jal.txt"

cat >"$work/jal-subsection.s" <<'EOF'
.text
.globl jal_subsection_target
.type jal_subsection_target, @function
jal_subsection_target:
  ret
.size jal_subsection_target, .-jal_subsection_target
.subsection 1
.globl jal_subsection_user
.type jal_subsection_user, @function
jal_subsection_user:
  j jal_subsection_target
  ret
.size jal_subsection_user, .-jal_subsection_user
EOF
"$MINIAS" -o "$work/jal-subsection.o" "$work/jal-subsection.s"
readelf -Wr "$work/jal-subsection.o" >"$work/jal-subsection.txt"
grep -q 'R_RISCV_JAL.*jal_subsection_target' "$work/jal-subsection.txt"

cat >"$work/high-numeric-labels.s" <<'EOF'
.text
.globl high_numeric_labels
.type high_numeric_labels, @function
high_numeric_labels:
  886 : addi a0, a0, 1
  beqz a0, 887f
  bnez a0, 886b
  887 : ret
.size high_numeric_labels, .-high_numeric_labels
EOF
"$MINIAS" -o "$work/high-numeric-labels.o" "$work/high-numeric-labels.s"
readelf -Ws "$work/high-numeric-labels.o" >"$work/high-numeric-labels.txt"
grep -q '.Lminias_num_886_1' "$work/high-numeric-labels.txt"
grep -q '.Lminias_num_887_1' "$work/high-numeric-labels.txt"

cat >"$work/conditional-alt.s" <<'EOF'
.text
.globl conditional_alt
.type conditional_alt, @function
conditional_alt:
  886 : nop
  887 :
.if 1 == 1
  .pushsection .alternative,"a"
  .4byte ((886b) - .)
  .4byte ((888f) - .)
  .2byte ((889f) - (888f))
  .popsection
.else
  definitely_not_an_instruction
.endif
  888 : nop
  889 : ret
.size conditional_alt, .-conditional_alt
EOF
"$MINIAS" -o "$work/conditional-alt.o" "$work/conditional-alt.s"
readelf -Wr "$work/conditional-alt.o" >"$work/conditional-alt.txt"
grep -q 'R_RISCV_ADD32.*Lminias_num_888_1' "$work/conditional-alt.txt"
grep -q 'R_RISCV_SUB32.*Lminias_expr' "$work/conditional-alt.txt"
grep -q 'R_RISCV_ADD16.*Lminias_num_889_1' "$work/conditional-alt.txt"
grep -q 'R_RISCV_SUB16.*Lminias_num_888_1' "$work/conditional-alt.txt"

cat >"$work/subsection.s" <<'EOF'
.text
.globl subsection_layout
.type subsection_layout, @object
subsection_layout:
  .byte 0x11
.subsection 1
subsection_one:
  .byte 0x22
.previous
subsection_zero_tail:
  .byte 0x33
.size subsection_layout, 3
EOF
"$MINIAS" -o "$work/subsection.o" "$work/subsection.s"
subsection_hex="$(
    readelf -x .text "$work/subsection.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]+$/ && length($i) <= 8 && length($i) % 2 == 0) printf "%s", $i}'
)"
test "$subsection_hex" = "113322"
test "$(readelf -Ws "$work/subsection.o" | awk '$8=="subsection_zero_tail" {print $2}')" = "0000000000000001"
test "$(readelf -Ws "$work/subsection.o" | awk '$8=="subsection_one" {print $2}')" = "0000000000000002"

cat >"$work/alternative-org.s" <<'EOF'
.text
.globl alternative_org
.type alternative_org, @function
alternative_org:
886:
  nop
887:
.subsection 1
888:
  nop
889:
  .org . - (887b - 886b) + (889b - 888b)
  .org . - (889b - 888b) + (887b - 886b)
.previous
  ret
.size alternative_org, .-alternative_org
EOF
"$MINIAS" -o "$work/alternative-org.o" "$work/alternative-org.s"
readelf -h "$work/alternative-org.o" | grep -q 'RISC-V'
alternative_org_hex="$(
    readelf -x .text "$work/alternative-org.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]+$/ && length($i) <= 8 && length($i) % 2 == 0) printf "%s", $i}'
)"
test "$alternative_org_hex" = "130000006780000013000000"

cat >"$work/sfence-vma.s" <<'EOF'
.text
.globl sfence_vma_family
.type sfence_vma_family, @function
sfence_vma_family:
  sfence.vma
  sfence.vma a0
  sfence.vma a0, a1
  ret
.size sfence_vma_family, .-sfence_vma_family
EOF
"$MINIAS" -o "$work/sfence-vma.o" "$work/sfence-vma.s"
sfence_hex="$(
    readelf -x .text "$work/sfence-vma.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]+$/ && length($i) <= 8 && length($i) % 2 == 0) printf "%s", $i}'
)"
test "$sfence_hex" = "73000012730005127300b51267800000"

cat >"$work/rept.s" <<'EOF'
.section .rodata,"a"
.globl rept_bytes
.type rept_bytes, @object
rept_bytes:
.rept 2
  .byte 0xaa
  .rept 3
    .byte 0xbb
  .endr
.endr
.size rept_bytes, 8
.text
.globl rept_nops
.type rept_nops, @function
rept_nops:
.rept 7
  nop
.endr
  ret
.size rept_nops, .-rept_nops
EOF
"$MINIAS" -o "$work/rept.o" "$work/rept.s"
rept_hex="$(
    readelf -x .rodata "$work/rept.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]+$/ && length($i) <= 8 && length($i) % 2 == 0) printf "%s", $i}'
)"
test "$rept_hex" = "aabbbbbbaabbbbbb"
test "$(readelf -Ws "$work/rept.o" | awk '$8=="rept_nops" {print $3}')" = "32"

cat >"$work/irp.s" <<'EOF'
.section .rodata,"a"
.globl irp_bytes
.type irp_bytes, @object
irp_bytes:
.irp num,0,1,2,31
  .equ .L__gpr_num_x\num, \num
  .byte \num
.endr
  .equ .L__gpr_num_x0, 0
  .short (3)
  .short (((.L__gpr_num_x1 << 0) | (.L__gpr_num_x2 << 5)))
.size irp_bytes, 8
EOF
"$MINIAS" -o "$work/irp.o" "$work/irp.s"
irp_hex="$(
    readelf -x .rodata "$work/irp.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]+$/ && length($i) <= 8 && length($i) % 2 == 0) printf "%s", $i}'
)"
test "$irp_hex" = "0001021f03004100"
readelf -Ws "$work/irp.o" | grep -q '.L__gpr_num_x31'

cat >"$work/vsetvl.s" <<'EOF'
.text
.globl vsetvl_user
.type vsetvl_user, @function
vsetvl_user:
  vsetvl x0, t5, t4
  ret
.size vsetvl_user, .-vsetvl_user
EOF
"$MINIAS" -o "$work/vsetvl.o" "$work/vsetvl.s"
vsetvl_hex="$(
    readelf -x .text "$work/vsetvl.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]+$/ && length($i) <= 8 && length($i) % 2 == 0) printf "%s", $i}'
)"
test "$vsetvl_hex" = "5770df8167800000"

cat >"$work/vsetvli.s" <<'EOF'
.text
.globl vsetvli_user
.type vsetvli_user, @function
vsetvli_user:
  vsetvli t0, x0, e8, m8, ta, ma
  ret
.size vsetvli_user, .-vsetvli_user
EOF
"$MINIAS" -o "$work/vsetvli.o" "$work/vsetvli.s"
vsetvli_hex="$(
    readelf -x .text "$work/vsetvli.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]+$/ && length($i) <= 8 && length($i) % 2 == 0) printf "%s", $i}'
)"
test "$vsetvli_hex" = "d772300c67800000"

cat >"$work/fence-i.s" <<'EOF'
.text
.globl fence_i_user
.type fence_i_user, @function
fence_i_user:
  fence.i
  ret
.size fence_i_user, .-fence_i_user
EOF
"$MINIAS" -o "$work/fence-i.o" "$work/fence-i.s"
fence_i_hex="$(
    readelf -x .text "$work/fence-i.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]+$/ && length($i) <= 8 && length($i) % 2 == 0) printf "%s", $i}'
)"
test "$fence_i_hex" = "0f10000067800000"

cat >"$work/u64-data.s" <<'EOF'
.data
.dword 18446744073709551615
.dword 9223372036854775808
.dword 18446744073709551416
EOF
"$MINIAS" -o "$work/u64-data.o" "$work/u64-data.s"
u64_hex="$(
    readelf -x .data "$work/u64-data.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]{8}$/) printf "%s", $i}'
)"
test "$u64_hex" = "ffffffffffffffff000000000000008038ffffffffffffff"

cat >"$work/aliases-pseudos.s" <<'EOF'
.text
.globl alias_target
.type alias_target, @function
alias_target:
  move a0, a1
  add a1, a0, 0
  ret
.size alias_target, .-alias_target
.globl alias_entry
.type alias_entry, @function
.set alias_entry, alias_target
.weak weak_alias
.type weak_alias, @function
.set weak_alias, alias_target
.globl forward_alias
.type forward_alias, @function
.set forward_alias, forward_target
.type forward_target, @function
forward_target:
  ret
.size forward_target, .-forward_target
EOF
"$MINIAS" -o "$work/aliases-pseudos.o" "$work/aliases-pseudos.s"
alias_hex="$(
    readelf -x .text "$work/aliases-pseudos.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]{8}$/) printf "%s", $i}'
)"
test "$alias_hex" = "13850500b30505006780000067800000"
target_value="$(readelf -Ws "$work/aliases-pseudos.o" | awk '$8=="alias_target" {print $2}')"
alias_value="$(readelf -Ws "$work/aliases-pseudos.o" | awk '$8=="alias_entry" {print $2}')"
weak_value="$(readelf -Ws "$work/aliases-pseudos.o" | awk '$8=="weak_alias" {print $2}')"
forward_value="$(readelf -Ws "$work/aliases-pseudos.o" | awk '$8=="forward_alias" {print $2}')"
forward_target_value="$(readelf -Ws "$work/aliases-pseudos.o" | awk '$8=="forward_target" {print $2}')"
test "$target_value" = "$alias_value"
test "$target_value" = "$weak_value"
test "$forward_value" = "$forward_target_value"
readelf -Ws "$work/aliases-pseudos.o" | grep -Eq 'FUNC[[:space:]]+GLOBAL.* alias_entry$'
readelf -Ws "$work/aliases-pseudos.o" | grep -Eq 'FUNC[[:space:]]+WEAK.* weak_alias$'

cat >"$work/immediate-expr.s" <<'EOF'
.text
addi t0, zero, 2*8
addi t1, zero, -2*8
sll t1, t1, 16
ret
EOF
"$MINIAS" -o "$work/immediate-expr.o" "$work/immediate-expr.s"
immediate_expr_hex="$(
    readelf -x .text "$work/immediate-expr.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]{8}$/) printf "%s", $i}'
)"
test "$immediate_expr_hex" = "93020001130300ff1313030167800000"

printf '\001\002\377A' >"$work/incbin.bin"
cat >"$work/incbin.s" <<EOF
.section .rodata,"a"
.byte 0xaa
.incbin "$work/incbin.bin"
.byte 0xbb
EOF
"$MINIAS" -o "$work/incbin.o" "$work/incbin.s"
incbin_hex="$(
    readelf -x .rodata "$work/incbin.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]+$/ && length($i) % 2 == 0) printf "%s", $i}'
)"
test "$incbin_hex" = "aa0102ff41bb"

cat >"$work/raw-insn.s" <<'EOF'
.text
.insn r 115, 0, 12, x0, x0, x0
.insn r 115, 4, 50, t0, t2, x3
.insn i 15, 2, x0, a0, 2
.insn i 15, 2, x0, a0, 1
EOF
"$MINIAS" -o "$work/raw-insn.o" "$work/raw-insn.s"
raw_insn_hex="$(
    readelf -x .text "$work/raw-insn.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]{8}$/) printf "%s", $i}'
)"
test "$raw_insn_hex" = "73000018f3c233640f2025000f201500"

cat >"$work/native-expr-pseudos.s" <<'EOF'
.text
.globl native_expr_pseudos
.type native_expr_pseudos, @function
native_expr_pseudos:
  li t0, 0x00040000 | (0x00006000 | 0x00000600)
  li t1, (1 << (12))
  addi t2, zero, 9*8
  andi a0, a1, ~((8*8)-1)
  addi a2, a3, 8 -1
  not a4, a5
  negw a6, a7
  frcsr t0
.size native_expr_pseudos, . - native_expr_pseudos
EOF
"$MINIAS" -o "$work/native-expr-pseudos.o" "$work/native-expr-pseudos.s"
native_expr_pseudos_hex="$(
    readelf -x .text "$work/native-expr-pseudos.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]{8}$/) printf "%s", $i}'
)"
test "$native_expr_pseudos_hex" = "b76204009b820260371300001b0303009303800413f505fc1386760013c7f7ff3b081041f3223000"
test "$(readelf -Ws "$work/native-expr-pseudos.o" | awk '$8=="native_expr_pseudos" {print $3}')" = "40"

cat >"$work/native-gas-forms.s" <<'EOF'
.text
.globl native_gas_forms
native_gas_forms:
  c.li s4, -13
  fld f0, 0(a0)
  fsd f1, 8(a0)
  flw f2, 12(a1)
  fsw f3, 16(a1)
  fscsr t0
  sret
.type native_gas_forms STT_FUNC
.size native_gas_forms, . - native_gas_forms
.end
EOF
"$MINIAS" -o "$work/native-gas-forms.o" "$work/native-gas-forms.s"
native_gas_forms_hex="$(
    readelf -x .text "$work/native-gas-forms.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]+$/ && length($i) % 2 == 0) printf "%s", $i}'
)"
test "$native_gas_forms_hex" = "4d5a073005002334150007a1c50023a835007390320073002010"
readelf -Ws "$work/native-gas-forms.o" | grep -Eq 'FUNC[[:space:]]+GLOBAL.* native_gas_forms$'

cat >"$work/native-macro-comma.s" <<'EOF'
.text
.macro emit_alt old_c, new_c, vendor, patch, enable
  \old_c
  addi a0, zero, \enable
.endm
emit_alt "nop; nop", "nop", 0, ((12) << 16) | 34, 1
EOF
"$MINIAS" -o "$work/native-macro-comma.o" "$work/native-macro-comma.s"
native_macro_comma_hex="$(
    readelf -x .text "$work/native-macro-comma.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]{8}$/) printf "%s", $i}'
)"
test "$native_macro_comma_hex" = "130000001300000013051000"

cat >"$work/native-set-size.s" <<'EOF'
.text
.globl native_set_size
native_set_size:
  nop
.set .Lnative_set_size, . - native_set_size
.size native_set_size, .Lnative_set_size
.type native_set_size STT_FUNC
EOF
"$MINIAS" -o "$work/native-set-size.o" "$work/native-set-size.s"
test "$(readelf -Ws "$work/native-set-size.o" | awk '$8=="native_set_size" {print $3}')" = "4"
readelf -Ws "$work/native-set-size.o" | grep -Eq 'FUNC[[:space:]]+GLOBAL.* native_set_size$'

cat >"$work/native-macro-quotes.s" <<'EOF'
.text
.macro emit_one insn:req
  \insn
.endm
.altmacro
emit_one "nop"
.noaltmacro
EOF
"$MINIAS" -o "$work/native-macro-quotes.o" "$work/native-macro-quotes.s"
native_macro_quotes_hex="$(
    readelf -x .text "$work/native-macro-quotes.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]{8}$/) printf "%s", $i}'
)"
test "$native_macro_quotes_hex" = "13000000"

cat >"$work/align-expr.s" <<'EOF'
.text
.byte 1
.balign (1 << 4)
.byte 2
EOF
"$MINIAS" -o "$work/align-expr.o" "$work/align-expr.s"
align_expr_hex="$(
    readelf -x .text "$work/align-expr.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]+$/ && length($i) % 2 == 0) printf "%s", $i}'
)"
test "$align_expr_hex" = "0100000000000000000000000000000002"

cat >"$work/sext-w.s" <<'EOF'
.text
sext.w a0, a1
EOF
"$MINIAS" -o "$work/sext-w.o" "$work/sext-w.s"
sext_w_hex="$(
    readelf -x .text "$work/sext-w.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]{8}$/) printf "%s", $i}'
)"
test "$sext_w_hex" = "1b850500"

cat >"$work/macro.s" <<'EOF'
.text
.macro inc_one dst:req src
  addi \dst, \src, 1
.endm
.macro add_imm, dst:req, src:req, imm=7
  addi \dst, \src, \imm
.endm
.macro twice op reg
  \op \reg, \reg, 1
  \op \reg, \reg, 1
.endm
inc_one a0 a1
add_imm a2, a3
twice addi a4
EOF
"$MINIAS" -o "$work/macro.o" "$work/macro.s"
macro_hex="$(
    readelf -x .text "$work/macro.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]{8}$/) printf "%s", $i}'
)"
test "$macro_hex" = "13851500138676001307170013071700"

cat >"$work/vector-unit.s" <<'EOF'
.text
.option arch,+v
vsetvli t0, x0, e8, m8, ta, ma
vmv.v.i v0, -1
vmv.v.i v8, -1
vmv.v.i v16, -1
vmv.v.i v24, -1
vse8.v v0, (t3)
vse8.v v8, (t3)
vle16.v v2, (a1)
vle8.v v9, (s0), v0.t
EOF
"$MINIAS" -o "$work/vector-unit.o" "$work/vector-unit.s"
vector_hex="$(
    readelf -x .text "$work/vector-unit.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]{8}$/) printf "%s", $i}'
)"
test "$vector_hex" = "d772300c57b00f5e57b40f5e57b80f5e57bc0f5e27000e0227040e0207d1050287040400"

echo "MINIAS_A0=PASS objects=19 format=ELF64-RISCV-ET_REL relocations=16 strings=2 pseudos=17 previous=3 subsection=2 numeric_labels=18 ecall=1 isa_next=13 csr_amo=8 sfence_vma=3 fence_i=1 vsetvl=1 vsetvli=1 vmv_v_i=4 rept=2 nested_rept=1 irp=4 section_stack=1 org=3 local_difference=1 lr_sc=2 inline_labels=2 branch_pseudos=8 extern=1 symbol_minus_dot=4 symbol_difference=1 absolute32=1 jal=2 jal_subsection=1 high_numeric_labels=2 u64_data=3 raw_insn=4 set_alias=3 move=1 numeric_zero=1 immediate_product=2 shift_immediate=1 incbin=1 align_expr=1 native_expr=6 native_gas_forms=1 native_set_size=1 native_macro_comma=1 native_macro_quotes=1 sext_w=1 macro=4 conditional=1"
