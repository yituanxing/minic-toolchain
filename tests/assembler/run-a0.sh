#!/bin/sh
set -eu

: "${MINIAS:?MINIAS must point to minic-as}"
: "${BUILD_DIR:?BUILD_DIR must be set}"

work="$BUILD_DIR/tests/assembler/a0"
mkdir -p "$work"

sh tests/assembler/probe-riscv-absolute-relocs.sh
bash tests/assembler/probe-minias-atomic-output.sh
bash tests/assembler/probe-mc-deferred-shift-gnu.sh
bash tests/assembler/probe-elf-sectionkey-unique-inherited-group-gnu.sh

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



# Explicit rv64i/lp64 must be fail-closed for EVERY MiniAS encoder family,
# not just annotate an ELF header. Check real machine words produced from
# pseudos, invalid extension instructions and silent GNU .option escapes.
cat >"$work/strict-i-positive.s" <<'EOF_I'
.text
.globl strict_i_positive
strict_i_positive:
    li a0, 37
    mv a1, a0
    add a0, a0, a1
    call strict_i_target
    ret
strict_i_target:
    ret
EOF_I
"$MINIAS" -march=rv64i -mabi=lp64 -o "$work/strict-i-positive.o" \
    "$work/strict-i-positive.s"
# ELF e_flags 0x0 is exactly RV64I LP64 soft float and no RVC.
# Some readelf builds omit the decoded 'soft-float ABI' suffix for 0x0.
if ! readelf -h "$work/strict-i-positive.o" |
    grep -Eq 'Flags:.*0x0([^[:xdigit:]]|$)'; then
    echo 'MiniAS strict-I expected ELF flags=0x0' >&2
    readelf -h "$work/strict-i-positive.o" >&2
    exit 1
fi
test -s "$work/strict-i-positive.o"
readelf -A "$work/strict-i-positive.o" | grep -q 'Tag_RISCV_arch: "rv64i2p1"' || {
    echo 'MiniAS explicit RV64I is missing its ELF ISA attribute' >&2
    readelf -A "$work/strict-i-positive.o" >&2
    exit 1
}

for bad in mul div fadd.s fadd.d amoadd.w c.li csrrw fence.i sfence.vma pause; do
    case "$bad" in
        mul) instruction='mul a0, a0, a1' ;;
        div) instruction='div a0, a0, a1' ;;
        fadd.s) instruction='fadd.s fa0, fa1, fa2' ;;
        fadd.d) instruction='fadd.d fa0, fa1, fa2' ;;
        amoadd.w) instruction='amoadd.w a0, a1, (a2)' ;;
        c.li) instruction='c.li a0, 1' ;;
        csrrw) instruction='csrrw a0, 0x300, a1' ;;
        fence.i) instruction='fence.i' ;;
        sfence.vma) instruction='sfence.vma' ;;
        pause) instruction='pause' ;;
    esac
    printf '.text\n.globl test_i\ntest_i:\n    %s\n' "$instruction" >"$work/strict-i-bad.s"
    rm -f "$work/strict-i-bad.o"
    if "$MINIAS" -march=rv64i -mabi=lp64 -o "$work/strict-i-bad.o" \
        "$work/strict-i-bad.s" >"$work/strict-i-bad.out" \
        2>"$work/strict-i-bad.err"; then
        echo "MiniAS accepted non-I machine instruction under rv64i: $bad" >&2
        exit 1
    fi
    test ! -e "$work/strict-i-bad.o"
    grep -Eq 'rv64i-unsupported-machine-encoding|rv64i-unaligned-executable-section|unsupported-instruction' \
        "$work/strict-i-bad.err" || {
        echo "MiniAS strict-I expected rejection diagnostic not found: $work/strict-i-bad.err" >&2
        cat "$work/strict-i-bad.err" >&2
        exit 1
    }
done
for escape in '.insn r 51, 0, 1, a0, a0, a1' '.word 0x02050533' \
              '.option arch,+m' '.option arch, +v' '.option arch,+zbb' \
              '.option rvc' '.org 8'; do
    printf '.text\n.globl test_i\ntest_i:\n    %s\n' "$escape" >"$work/strict-i-escape.s"
    rm -f "$work/strict-i-escape.o"
    if "$MINIAS" -march=rv64i -mabi=lp64 -o "$work/strict-i-escape.o" \
        "$work/strict-i-escape.s" >"$work/strict-i-escape.out" \
        2>"$work/strict-i-escape.err"; then
        echo "MiniAS accepted ISA escape under rv64i: $escape" >&2
        exit 1
    fi
    test ! -e "$work/strict-i-escape.o"
    grep -q 'unsupported-isa-' "$work/strict-i-escape.err" || {
        echo "MiniAS strict-I expected rejection diagnostic not found: $work/strict-i-escape.err" >&2
        cat "$work/strict-i-escape.err" >&2
        exit 1
    }
done
cat >"$work/strict-i-bad-abi.s" <<'EOF_I'
.text
ret
EOF_I
if "$MINIAS" -mabi=lp64d -march=rv64i -o "$work/strict-i-bad-abi.o" \
    "$work/strict-i-bad-abi.s" 2>"$work/strict-i-bad-abi.err"; then
    echo 'MiniAS rv64i accepted lp64d' >&2
    exit 1
fi
grep -q 'incompatible-riscv-isa-abi' "$work/strict-i-bad-abi.err" || {
        echo "MiniAS strict-I expected rejection diagnostic not found: $work/strict-i-bad-abi.err" >&2
        cat "$work/strict-i-bad-abi.err" >&2
        exit 1
    }
echo 'MINIAS_RV64I_ISA=PASS pseudos=1 negative_m_f_d_a_c_csr_zifencei=1 escapes=5 wrong_abi=1'

# Regression: GNU RISC-V assembler fills executable .p2align with NOPs,
# not 0x0000 (C.UNIMP). A zero-fill bug in the Zbb strcmp fast path passes
# ELF/symbol checks but traps before the Linux boot banner in real QEMU.
cat >"$work/code-alignment.s" <<'EOF_ALIGN'
.text
.globl code_alignment
.type code_alignment, @function
code_alignment:
    nop
    .p2align 3
    nop
    .2byte 0x0001
    .p2align 2
    nop
.size code_alignment, .-code_alignment
EOF_ALIGN
"$MINIAS" -o "$work/code-alignment.o" "$work/code-alignment.s"
alignment_text_hex="$(
    readelf -x .text "$work/code-alignment.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]+$/ && length($i) <= 8 && length($i) % 2 == 0) printf "%s", $i}'
)"
test "$alignment_text_hex" = "1300000013000000130000000100010013000000" || {
    echo "MiniAS executable .p2align must contain RISC-V NOPs, not C.UNIMP" >&2
    exit 1
}
cat >"$work/data-alignment.s" <<'EOF_DATAALIGN'
.data
.byte 0x55
.balign 4
.byte 0x66
EOF_DATAALIGN
"$MINIAS" -o "$work/data-alignment.o" "$work/data-alignment.s"
alignment_data_hex="$(
    readelf -x .data "$work/data-alignment.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]+$/ && length($i) <= 8 && length($i) % 2 == 0) printf "%s", $i}'
)"
test "$alignment_data_hex" = "5500000066" || {
    echo "MiniAS data alignment must remain zero-filled" >&2
    exit 1
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
grep -q 'R_RISCV_PCREL_LO12_I.*\.Lminias_private_' "$work/reloc.txt"
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

cat >"$work/note-section.s" <<'EOF'
.section .note.Linux,"a",@note
.align 2
.long 6
.long 4
.long 0
.asciz "Linux"
.align 2
.long 0
EOF
"$MINIAS" -o "$work/note-section.o" "$work/note-section.s"
readelf -SW "$work/note-section.o" >"$work/note-section.sections"
grep -Eq '\] \.note\.Linux[[:space:]]+NOTE[[:space:]]' "$work/note-section.sections"

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
# Numeric local labels whose branches are fully resolved at assembly time are
# private implementation details. Match GNU as: do not retain the generated
# .Lminias_num_* names in the final ELF symbol table unless a relocation still
# needs them.
! grep -q '.Lminias_num_0_1' "$work/inline-atomic-loop.txt"
! grep -q '.Lminias_num_1_1' "$work/inline-atomic-loop.txt"

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
grep -q 'R_RISCV_SUB64.*\.Lminias_private_' "$work/extern-diff.txt"
grep -q 'R_RISCV_ADD32.*external_delta_target' "$work/extern-diff.txt"
grep -q 'R_RISCV_SUB32.*\.Lminias_private_' "$work/extern-diff.txt"
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
grep -q 'R_RISCV_ADD32.*\.Lminias_private_' "$work/conditional-alt.txt"
grep -q 'R_RISCV_SUB32.*\.Lminias_private_' "$work/conditional-alt.txt"
grep -q 'R_RISCV_ADD16.*\.Lminias_private_' "$work/conditional-alt.txt"
grep -q 'R_RISCV_SUB16.*\.Lminias_private_' "$work/conditional-alt.txt"

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
if readelf -Ws "$work/irp.o" | grep -q '.L__gpr_num_x31'; then
    echo "unexpected temporary local leaked: .L__gpr_num_x31" >&2
    exit 1
fi

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
# Floating-point stores are opcode 0x27 (STORE-FP), not integer STORE
# 0x23. The old golden accidentally preserved a MiniAS fsd/fsw codegen bug.
test "$native_gas_forms_hex" = "4d5a073005002734150007a1c50027a835007390320073002010"
readelf -Ws "$work/native-gas-forms.o" | grep -Eq 'FUNC[[:space:]]+GLOBAL.* native_gas_forms$'

cat >"$work/scalar-fp.s" <<'EOF'
.option norvc
.text
.globl scalar_fp
.type scalar_fp, @function
scalar_fp:
  fcvt.d.w ft0, t0
  fcvt.d.wu ft1, t1
  fcvt.d.l ft2, t2
  fcvt.d.lu ft3, s0
  fcvt.w.d a0, fa0, rtz
  fcvt.wu.d a1, fa1, rtz
  fcvt.l.d a2, fa2, rtz
  fcvt.lu.d a3, fa3, rtz
  fcvt.d.s ft4, ft5
  fcvt.s.d ft6, ft7
  fadd.d ft0, ft1, ft2
  fsub.d ft3, ft4, ft5
  fmul.d ft6, ft7, fa0
  fdiv.d fa1, fa2, fa3
  fsgnjn.d ft0, ft1, ft2
  feq.d a4, ft0, ft1
  flt.d a5, ft2, ft3
  fle.d a6, ft4, ft5
  fmv.x.d a7, fa0
  fmv.d.x fa1, t0
  fmv.x.w a0, fa2
  fmv.w.x fa3, t1
  ret
.size scalar_fp, . - scalar_fp
EOF
"$MINIAS" -o "$work/scalar-fp.o" "$work/scalar-fp.s"
scalar_fp_hex="$(
    readelf -x .text "$work/scalar-fp.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]{8}$/) printf "%s", $i}'
)"
test -n "$scalar_fp_hex"
if command -v riscv64-linux-gnu-as >/dev/null 2>&1; then
    riscv64-linux-gnu-as -march=rv64gc -o "$work/scalar-fp.gnu.o" "$work/scalar-fp.s"
    scalar_fp_gnu_hex="$(
        readelf -x .text "$work/scalar-fp.gnu.o" |
        awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]{8}$/) printf "%s", $i}'
    )"
    if test "$scalar_fp_hex" != "$scalar_fp_gnu_hex"; then
        echo "MINIAS_A0_FP_GNU_DIFF=FAIL" >&2
        echo "mini=$scalar_fp_hex" >&2
        echo "gnu=$scalar_fp_gnu_hex" >&2
        exit 1
    fi
    echo "MINIAS_A0_FP_GNU_DIFF=PASS instructions=22"
fi

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
readelf -Ws "$work/native-set-size.o" >"$work/native-set-size.sym"
grep -Eq 'FUNC[[:space:]]+GLOBAL.* native_set_size$' "$work/native-set-size.sym"
if grep -q '[[:space:]]\.Lnative_set_size$' "$work/native-set-size.sym"; then
    echo "unexpected temporary local leaked: .Lnative_set_size" >&2
    exit 1
fi

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
# GNU GAS riscv_make_nops: an odd executable gap emits one zero byte,
# then one C.NOP (if a 2B remainder exists), followed by 32-bit NOPs.
# A raw .byte in .text must not force the entire 15B alignment to zero.
test "$align_expr_hex" = "0100010013000000130000001300000002"

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
.option arch, +v
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

cat >"$work/native-zbb-tail-memexpr.s" <<'EOF'
.text
.extern external_tail
.globl native_zbb_tail_memexpr
.type native_zbb_tail_memexpr, @function
native_zbb_tail_memexpr:
  .option push
  .option arch, +zbb
  orc.b a0, a1
  rev8 a2, a3
  ctz a4, a5
  .option pop
  ld t0, (0 + 8)(a0)
  sd t1, ((0 * 8) - (2 * 8))(t3)
  and t2, t2, 8 -1
  tail external_tail
.size native_zbb_tail_memexpr, . - native_zbb_tail_memexpr
EOF
"$MINIAS" -o "$work/native-zbb-tail-memexpr.o" "$work/native-zbb-tail-memexpr.s"
native_zbb_tail_memexpr_hex="$(
    readelf -x .text "$work/native-zbb-tail-memexpr.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]{8}$/) printf "%s", $i}'
)"
test "$native_zbb_tail_memexpr_hex" = "13d5752813d6866b139717608332850023386efe93f373001703000067000300"
readelf -r "$work/native-zbb-tail-memexpr.o" | grep -Eq 'R_RISCV_CALL_PLT[[:space:]]+.*external_tail'
test "$(readelf -Ws "$work/native-zbb-tail-memexpr.o" | awk '$8=="native_zbb_tail_memexpr" {print $3}')" = "32"

cat >"$work/native-jalr-symbol-load.s" <<'EOF'
.text
.extern native_load_word
.extern native_load_dword
.globl native_jalr_symbol_load
.type native_jalr_symbol_load, @function
native_jalr_symbol_load:
  jalr t1
  jalr zero, 0x0(t0)
  lw a1, native_load_word
  ld a2, native_load_dword
.size native_jalr_symbol_load, . - native_jalr_symbol_load
EOF
"$MINIAS" -o "$work/native-jalr-symbol-load.o" "$work/native-jalr-symbol-load.s"
native_jalr_symbol_load_hex="$(
    readelf -x .text "$work/native-jalr-symbol-load.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]{8}$/) printf "%s", $i}'
)"
echo "MINIAS_A0_NATIVE_JALR_HEX=$native_jalr_symbol_load_hex"
native_jalr_relocs="$(readelf -Wr "$work/native-jalr-symbol-load.o")"
printf '%s\n' "$native_jalr_relocs"
native_jalr_size="$(readelf -Ws "$work/native-jalr-symbol-load.o" | awk '$8=="native_jalr_symbol_load" {print $3}')"
echo "MINIAS_A0_NATIVE_JALR_SIZE=$native_jalr_size"
test "$native_jalr_symbol_load_hex" = "e7000300678002009705000083a505001706000003360600"
printf '%s\n' "$native_jalr_relocs" | grep -Eq 'R_RISCV_PCREL_HI20[[:space:]]+.*native_load_word'
printf '%s\n' "$native_jalr_relocs" | grep -Eq 'R_RISCV_PCREL_HI20[[:space:]]+.*native_load_dword'
test "$(printf '%s\n' "$native_jalr_relocs" | grep -c 'R_RISCV_PCREL_LO12_I')" -eq 2
test "$native_jalr_size" = "24"

cat >"$work/native-forward-short.s" <<'EOF'
.data
.short section_count
section_table:
.space 80
section_table_end:
.set section_count, (section_table_end - section_table) / 40
EOF
"$MINIAS" -o "$work/native-forward-short.o" "$work/native-forward-short.s"
native_forward_data="$(readelf -x .data "$work/native-forward-short.o")"
native_forward_symbols="$(readelf -Ws "$work/native-forward-short.o")"
printf '%s\n' "$native_forward_data"
printf '%s\n' "$native_forward_symbols" | grep 'section_count' || true
native_forward_value="$(printf '%s\n' "$native_forward_symbols" | awk '$8=="section_count" {print $2}')"
echo "MINIAS_A0_NATIVE_FORWARD_VALUE=$native_forward_value"
printf '%s\n' "$native_forward_data" | grep -Eq '0x00000000[[:space:]]+0200'
test "$native_forward_value" = "0000000000000002"

cat >"$work/native-mixed-macro.s" <<'EOF'
.text
.macro _asm_extable, insn, fixup
  .pushsection __ex_table, "a"
  .balign 4
  .long ((\insn) - .)
  .long ((\fixup) - .)
  .short 1
  .short 0
  .popsection
.endm
.macro fixup op reg addr lbl
100:
  \op \reg, \addr
  _asm_extable 100b, \lbl
.endm
.globl native_mixed_macro
native_mixed_macro:
  fixup lb a5, 0(a1), 10f
10:
  ret
EOF
"$MINIAS" -o "$work/native-mixed-macro.o" "$work/native-mixed-macro.s"
native_mixed_macro_text="$(
    readelf -x .text "$work/native-mixed-macro.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]{8}$/) printf "%s", $i}'
)"
echo "MINIAS_A0_NATIVE_MIXED_HEX=$native_mixed_macro_text"
native_mixed_sections="$(readelf -SW "$work/native-mixed-macro.o")"
native_mixed_extable="$(readelf -x __ex_table "$work/native-mixed-macro.o")"
printf '%s\n' "$native_mixed_sections" | grep '__ex_table' || true
printf '%s\n' "$native_mixed_extable"
test "$native_mixed_macro_text" = "8387050067800000"
test "$(printf '%s\n' "$native_mixed_sections" | grep -Ec '\] __ex_table[[:space:]]')" -eq 1
test "$(printf '%s\n' "$native_mixed_extable" | grep -c '0x00000000')" -ge 1

cat >"$work/native-divided-difference.s" <<'EOF'
.data
.long (section_table_div - .) / 8
.space 12
section_table_div:
.byte 0
EOF
"$MINIAS" -o "$work/native-divided-difference.o" "$work/native-divided-difference.s"
native_divided_data="$(
    readelf -x .data "$work/native-divided-difference.o" |
    awk '/0x00000000/ {print $2}'
)"
test "$native_divided_data" = "02000000"

cat >"$work/pic-la-got.s" <<'EOF'
.option pic
.text
.extern minias_external_data
.globl pic_la_got
.type pic_la_got, @function
pic_la_got:
  la a0, minias_external_data
  ret
.size pic_la_got, . - pic_la_got
EOF
"$MINIAS" -o "$work/pic-la-got.o" "$work/pic-la-got.s"
pic_la_got_hex="$(
    readelf -x .text "$work/pic-la-got.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]{8}$/) printf "%s", $i}'
)"
pic_la_got_relocs="$(readelf -Wr "$work/pic-la-got.o")"
printf '%s\n' "$pic_la_got_relocs" | grep -Eq 'R_RISCV_GOT_HI20[[:space:]]+.*minias_external_data'
printf '%s\n' "$pic_la_got_relocs" | grep -Eq 'R_RISCV_PCREL_LO12_I'
test "$pic_la_got_hex" = "170500000335050067800000"

if command -v riscv64-linux-gnu-as >/dev/null 2>&1; then
    cat >"$work/pic-la-got.gnu.s" <<'EOF'
.option pic
.option norvc
.text
.extern minias_external_data
.globl pic_la_got
.type pic_la_got, @function
pic_la_got:
  la a0, minias_external_data
  ret
.size pic_la_got, . - pic_la_got
EOF
    riscv64-linux-gnu-as -march=rv64gc -o "$work/pic-la-got.gnu.o" "$work/pic-la-got.gnu.s"
    pic_la_gnu_hex="$(
        readelf -x .text "$work/pic-la-got.gnu.o" |
        awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]{8}$/) printf "%s", $i}'
    )"
    test "$pic_la_got_hex" = "$pic_la_gnu_hex"
    readelf -Wr "$work/pic-la-got.gnu.o" | grep -Eq 'R_RISCV_GOT_HI20[[:space:]]+.*minias_external_data'
    echo "MINIAS_A0_PIC_LA_GNU_DIFF=PASS relocation=GOT_HI20 sequence=auipc+ld"
fi

# Genuine GNU default-NOPIC regression from Linux head.S:
# prior MiniAS used GOT_HI20 for undefined names even though GNU as uses
# PCREL_HI20 by default, causing OpenSBI->Linux pre-banner stalls.
cat >"$work/nopic-la-head.s" <<'EOF'
.option norvc
.text
.extern minias_early_boot_external
.extern minias_pic_external
.globl minias_nopic_entry
minias_nopic_entry:
  la a0, minias_early_boot_external
  .option push
  .option pic
  la a1, minias_pic_external
  .option pop
  la a2, minias_early_boot_external
  ret
EOF
"$MINIAS" -o "$work/nopic-la-head.o" "$work/nopic-la-head.s"
nopic_relocs="$(readelf -Wr "$work/nopic-la-head.o")"
test "$(printf '%s\n' "$nopic_relocs" |
  grep -c 'R_RISCV_PCREL_HI20.*minias_early_boot_external')" -eq 2
test "$(printf '%s\n' "$nopic_relocs" |
  grep -c 'R_RISCV_GOT_HI20.*minias_pic_external')" -eq 1
test "$(printf '%s\n' "$nopic_relocs" | grep -c 'R_RISCV_PCREL_LO12_I')" -eq 3
if command -v riscv64-linux-gnu-as >/dev/null 2>&1; then
    riscv64-linux-gnu-as -march=rv64gc -o "$work/nopic-la-head.gnu.o" "$work/nopic-la-head.s"
    gnu_nopic_relocs="$(readelf -Wr "$work/nopic-la-head.gnu.o")"
    test "$(printf '%s\n' "$gnu_nopic_relocs" |
      grep -c 'R_RISCV_PCREL_HI20.*minias_early_boot_external')" -eq 2
    test "$(printf '%s\n' "$gnu_nopic_relocs" |
      grep -c 'R_RISCV_GOT_HI20.*minias_pic_external')" -eq 1
    mini_nopic_hex="$(readelf -x .text "$work/nopic-la-head.o" |
      awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]{8}$/) printf "%s", $i}')"
    gnu_nopic_hex="$(readelf -x .text "$work/nopic-la-head.gnu.o" |
      awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]{8}$/) printf "%s", $i}')"
    test "$mini_nopic_hex" = "$gnu_nopic_hex"
    echo "MINIAS_A0_NOPIC_LA_GNU_DIFF=PASS pcrel_external=2 pic_got=1 scoped_push_pop=1 code=GNU-exact"
fi

cat >"$work/native-symbol-addend-expr.s" <<'EOF'
.text
.extern native_addend_base
.globl native_symbol_addend_expr
native_symbol_addend_expr:
  la sp, native_addend_base + ((1 << (12)) << (2 + 0))
EOF
"$MINIAS" -o "$work/native-symbol-addend-expr.o" "$work/native-symbol-addend-expr.s"
native_symbol_addend_hex="$(
    readelf -x .text "$work/native-symbol-addend-expr.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]{8}$/) printf "%s", $i}'
)"
test "$native_symbol_addend_hex" = "1701000013010100"
native_symbol_addend_relocs="$(readelf -Wr "$work/native-symbol-addend-expr.o")"
printf '%s\n' "$native_symbol_addend_relocs" | grep -Eq 'R_RISCV_PCREL_HI20[[:space:]]+.*native_addend_base \+ 4000'
printf '%s\n' "$native_symbol_addend_relocs" | grep -Eq 'R_RISCV_PCREL_LO12_I'

cat >"$work/native-add-sub-immediate.s" <<'EOF'
.text
.globl native_add_sub_immediate
native_add_sub_immediate:
  add a3, a3, 8
  sub a4, a4, 16
EOF
"$MINIAS" -o "$work/native-add-sub-immediate.o" "$work/native-add-sub-immediate.s"
native_add_sub_immediate_hex="$(
    readelf -x .text "$work/native-add-sub-immediate.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]{8}$/) printf "%s", $i}'
)"
echo "MINIAS_A0_NATIVE_ADD_SUB_HEX=$native_add_sub_immediate_hex"
test "$native_add_sub_immediate_hex" = "93868600130707ff"

cat >"$work/native-cfi-boundary.s" <<'EOF'
.text
.globl native_cfi_boundary
native_cfi_boundary:
  .cfi_startproc
  nop
  .cfi_signal_frame
  ret
  .cfi_endproc
EOF
"$MINIAS" -o "$work/native-cfi-boundary.o" "$work/native-cfi-boundary.s"
native_cfi_hex="$(
    readelf -x .text "$work/native-cfi-boundary.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]{8}$/) printf "%s", $i}'
)"
test "$native_cfi_hex" = "1300000067800000"

cat >"$work/native-cfi-unclosed.s" <<'EOF'
.text
.cfi_startproc
nop
EOF
if "$MINIAS" -o "$work/native-cfi-unclosed.o" "$work/native-cfi-unclosed.s" 2>"$work/native-cfi-unclosed.err"; then
    echo "expected unterminated CFI rejection" >&2
    exit 1
fi
grep -q 'unterminated-directive:.cfi_startproc' "$work/native-cfi-unclosed.err"

cat >"$work/gcc-vdso-word-ops.s" <<'EOF'
.text
.globl gcc_vdso_word_ops
.type gcc_vdso_word_ops, @function
gcc_vdso_word_ops:
  addw a0, a1, a2
  subw a3, a4, a5
  sllw a6, a7, t0
  srlw t1, t2, t3
  sraw t4, t5, t6
  mulw a0, a1, a2
  divw a3, a4, a5
  divuw a6, a7, t0
  remw t1, t2, t3
  remuw t4, t5, t6
  slliw a0, a1, 7
  srliw a2, a3, 11
  sraiw a4, a5, 3
  ret
.size gcc_vdso_word_ops, . - gcc_vdso_word_ops
EOF
"$MINIAS" -o "$work/gcc-vdso-word-ops.o" "$work/gcc-vdso-word-ops.s"
gcc_vdso_word_hex="$(
    readelf -x .text "$work/gcc-vdso-word-ops.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]{8}$/) printf "%s", $i}'
)"
test "$gcc_vdso_word_hex" = "3b85c500bb06f7403b9858003bd3c301bb5eff413b85c502bb46f7023bd858023be3c303bb7eff031b9575001bd6b6001bd7374067800000"

cat >"$work/gcc-vdso-plt.s" <<'EOF'
.text
.extern riscv_hwprobe
.globl gcc_vdso_plt
.type gcc_vdso_plt, @function
gcc_vdso_plt:
  call riscv_hwprobe@plt
  ret
.size gcc_vdso_plt, . - gcc_vdso_plt
EOF
"$MINIAS" -o "$work/gcc-vdso-plt.o" "$work/gcc-vdso-plt.s"
readelf -Wr "$work/gcc-vdso-plt.o" | grep -Eq 'R_RISCV_CALL_PLT[[:space:]]+.*riscv_hwprobe'
if readelf -Ws "$work/gcc-vdso-plt.o" | grep -q 'riscv_hwprobe@plt'; then
    echo "unexpected @plt suffix retained in symbol table" >&2
    exit 1
fi

cat >"$work/gcc-size-before-label.s" <<'EOF'
.data
.globl gcc_size_before_label
.type gcc_size_before_label, @object
.size gcc_size_before_label, 8
gcc_size_before_label:
  .dword 0x1122334455667788
EOF
"$MINIAS" -o "$work/gcc-size-before-label.o" "$work/gcc-size-before-label.s"
test "$(readelf -Ws "$work/gcc-size-before-label.o" | awk '$8=="gcc_size_before_label" {print $3}')" = "8"

cat >"$work/gcc-sgt-pseudos.s" <<'EOF'
.text
.globl gcc_sgt_pseudos
.type gcc_sgt_pseudos, @function
gcc_sgt_pseudos:
  sgt a0, a1, a2
  sgtu a3, a4, a5
.size gcc_sgt_pseudos, . - gcc_sgt_pseudos
EOF
"$MINIAS" -o "$work/gcc-sgt-pseudos.o" "$work/gcc-sgt-pseudos.s"
gcc_sgt_hex="$(
    readelf -x .text "$work/gcc-sgt-pseudos.o" |
    awk '/0x[0-9a-f]+/ {for (i=2; i<=NF; ++i) if ($i ~ /^[0-9a-f]{8}$/) printf "%s", $i}'
)"
test "$gcc_sgt_hex" = "3325b600b3b6e700"

cat >"$work/rv32-elf.s" <<'EOF'
.text
.extern rv32_external
.globl rv32_entry
.type rv32_entry, @function
rv32_entry:
  call rv32_external
  ret
.size rv32_entry, . - rv32_entry
EOF
"$MINIAS" -march=rv32imac -mabi=ilp32 -o "$work/rv32-elf.o" "$work/rv32-elf.s"
readelf -h "$work/rv32-elf.o" >"$work/rv32-elf.hdr"
grep -q 'Class:[[:space:]]*ELF32' "$work/rv32-elf.hdr"
grep -q 'Machine:[[:space:]]*RISC-V' "$work/rv32-elf.hdr"
grep -q 'REL (Relocatable file)' "$work/rv32-elf.hdr"
readelf -Wr "$work/rv32-elf.o" | grep -Eq 'R_RISCV_CALL_PLT[[:space:]]+.*rv32_external'

cat >"$work/abi-flags.s" <<'EOF'
.text
.globl abi_flags_probe
.type abi_flags_probe, @function
abi_flags_probe:
  ret
.size abi_flags_probe, . - abi_flags_probe
EOF
"$MINIAS" -march=rv64gc -mabi=lp64d -o "$work/abi-lp64d.o" "$work/abi-flags.s"
readelf -h "$work/abi-lp64d.o" >"$work/abi-lp64d.hdr"
grep -q 'Flags:.*0x5.*RVC.*double-float ABI' "$work/abi-lp64d.hdr"

"$MINIAS" -march=rv64gc -mabi=lp64f -o "$work/abi-lp64f.o" "$work/abi-flags.s"
readelf -h "$work/abi-lp64f.o" >"$work/abi-lp64f.hdr"
grep -q 'Flags:.*0x3.*RVC.*single-float ABI' "$work/abi-lp64f.hdr"

"$MINIAS" -march=rv32e -mabi=ilp32e -o "$work/abi-ilp32e.o" "$work/abi-flags.s"
readelf -h "$work/abi-ilp32e.o" >"$work/abi-ilp32e.hdr"
grep -q 'Class:[[:space:]]*ELF32' "$work/abi-ilp32e.hdr"
grep -q 'Flags:.*0x8.*RVE' "$work/abi-ilp32e.hdr"

if "$MINIAS" -march=rv64gc -mabi=lp64bogus -o "$work/abi-invalid.o" "$work/abi-flags.s" \
     >"$work/abi-invalid.stdout" 2>"$work/abi-invalid.stderr"; then
    echo "MINIAS_A0_ABI_INVALID=FAIL" >&2
    exit 1
fi
grep -q 'unsupported-abi:lp64bogus' "$work/abi-invalid.stderr"
echo "MINIAS_A0_ABI_FLAGS=PASS lp64d=0x5 lp64f=0x3 ilp32e=0x8"

cat >"$work/gcc-lanchor-set.s" <<'EOF'
.bss
.align 3
.set .LANCHOR0, . + 0
.type anchored_object, @object
.size anchored_object, 16
anchored_object:
  .zero 16
.text
.globl gcc_lanchor_user
.type gcc_lanchor_user, @function
gcc_lanchor_user:
  lla a0, .LANCHOR0
  ret
.size gcc_lanchor_user, . - gcc_lanchor_user
EOF
"$MINIAS" -o "$work/gcc-lanchor-set.o" "$work/gcc-lanchor-set.s"
readelf -Ws "$work/gcc-lanchor-set.o" >"$work/gcc-lanchor-set.sym"
grep -Eq '[[:space:]]\.LANCHOR0$' "$work/gcc-lanchor-set.sym"
readelf -Wr "$work/gcc-lanchor-set.o" | grep -Eq 'R_RISCV_PCREL_HI20[[:space:]]+.*\.LANCHOR0'

cat >"$work/far-conditional-branch.s" <<'EOF'
.text
.globl far_conditional_branch
.type far_conditional_branch, @function
far_conditional_branch:
  beq a0, a1, .Lfar_target
  .space 5000
.Lfar_target:
  ret
.size far_conditional_branch, . - far_conditional_branch
EOF
"$MINIAS" -o "$work/far-conditional-branch.o" "$work/far-conditional-branch.s"
far_branch_prefix="$(
    readelf -x .text "$work/far-conditional-branch.o" |
    awk '/0x00000000/ {print $2 $3; exit}'
)"
test "$far_branch_prefix" = "6314b5006f10c038"
test "$(readelf -Ws "$work/far-conditional-branch.o" | awk '$8=="far_conditional_branch" {print $3}')" = "5012"


# An executable subsection follows an odd-size read-only section in the
# MiniC driver. The ELF input section itself must advertise instruction
# alignment; otherwise a linker may place its first RV64 instruction at an
# odd address (observed main=0x1059 during driver QEMU, exit 132).
cat >"$work/exec-subsection-align.s" <<'EOF'
.section .rodata,"a"
.byte 0x42
.section .text.main,"ax",@progbits
.globl exec_subsection_align
.type exec_subsection_align, @function
exec_subsection_align:
  li a0, 42
  ret
.size exec_subsection_align, . - exec_subsection_align
EOF
"$MINIAS" -o "$work/exec-subsection-align.o" "$work/exec-subsection-align.s"
readelf -SW "$work/exec-subsection-align.o" >"$work/exec-subsection-align.sections"
exec_alignment=$(awk 'index($0, ".text.main") { print $NF; exit }' "$work/exec-subsection-align.sections")
if test "$exec_alignment" != 4; then
    echo "MINIAS_EXEC_SUBSECTION_ALIGN=FAIL section=.text.main actual=$exec_alignment expected=4" >&2
    exit 1
fi
echo "MINIAS_EXEC_SUBSECTION_ALIGN=PASS section=.text.main align=$exec_alignment"


# Validate ISA/ABI and ELFCLASS together. Previous CLI allowed -mabi to
# silently override RV32/RV64 output class and claimed unsupported float ABIs.
cat >"$work/profile-target.s" <<'EOF_PROFILE'
.text
.globl profile_target
profile_target:
    ret
EOF_PROFILE
for opts in 'rv32imac lp64' 'rv64gc ilp32' 'rv64gc lp64q' \
            'rv64i lp64f' 'rv64garbage lp64' 'rv32e ilp32'; do
    set -- $opts
    if "$MINIAS" "-march=$1" "-mabi=$2" -o "$work/profile-bad.o" \
        "$work/profile-target.s" >/dev/null 2>"$work/profile-bad.err"; then
        echo "MiniAS accepted invalid ISA/ABI: $opts" >&2
        exit 1
    fi
done
"$MINIAS" -march=rv64gc -mabi=lp64d -o "$work/profile-64.o" "$work/profile-target.s"
"$MINIAS" -mabi=ilp32 -march=rv32imac -o "$work/profile-32.o" "$work/profile-target.s"
readelf -h "$work/profile-64.o" | grep -q 'Class:.*ELF64'
readelf -h "$work/profile-32.o" | grep -q 'Class:.*ELF32'
echo 'MINIAS_ISA_ABI_PROFILE=PASS negative=6 positive=2'

# Macro-expanded instructions share their original source line number.
# Relaxing one branch must not also enlarge a different invocation.
cat >"$work/relax-identity.s" <<'EOF_RELAX'
.text
.macro test_branch dest
    beq a0, a1, \dest
.endm
.globl relax_identity
relax_identity:
    test_branch far_away
    test_branch nearby
.globl after_branches
after_branches:
nearby:
    .space 8192
far_away:
    ret
EOF_RELAX
"$MINIAS" -o "$work/relax-identity.o" "$work/relax-identity.s"
after_value="$(readelf -sW "$work/relax-identity.o" |
    awk '$NF == "after_branches" {print $2}')"
test "$after_value" = '000000000000000c' || {
    echo "MiniAS branch relaxation wrongly keyed by source line: $after_value" >&2
    exit 1
}
echo 'MINIAS_RELAX_STATEMENT_ID=PASS macro_instructions=2 expanded=1'


# Shared integer grammar: RISC-V immediates and symbolic ELF relocation
# addends must use the same parentheses, precedence and unsigned semantics.
cat >"$work/shared-expression.s" <<'EOF_SHARED_EXPR'
.text
.globl shared_expression
shared_expression:
    li a0, ((1 << 10) | (3 * 7))
    ret
.data
.globl shared_expression_ptr
shared_expression_ptr:
    .quad shared_expression + ((1 << 3) | 1)
EOF_SHARED_EXPR
"$MINIAS" -march=rv64i -mabi=lp64 -o "$work/shared-expression.o" "$work/shared-expression.s"
readelf -Wr "$work/shared-expression.o" |
    grep -Eq 'R_RISCV_64[[:space:]]+.*shared_expression[[:space:]]+\+[[:space:]]+9'
if command -v riscv64-linux-gnu-as >/dev/null 2>&1 &&
   command -v riscv64-linux-gnu-objcopy >/dev/null 2>&1; then
    riscv64-linux-gnu-as -march=rv64i -mabi=lp64 -o "$work/shared-expression.gnu.o" "$work/shared-expression.s"
    riscv64-linux-gnu-objcopy -j .text -O binary \
        "$work/shared-expression.gnu.o" "$work/shared-expression.gnu.bin"
    riscv64-linux-gnu-objcopy -j .text -O binary \
        "$work/shared-expression.o" "$work/shared-expression.bin"
    cmp "$work/shared-expression.gnu.bin" "$work/shared-expression.bin"
    echo 'MINIAS_SHARED_INT_EXPRESSION=PASS addend=9 GNU_AS_text_equal=true'
else
    # A0 still validates relocation addends on minimal frozen-corpus runners.
    # GNU differential is mandatory in minias-target-contract.yml, which
    # installs the cross assembler and objcopy explicitly.
    echo 'MINIAS_SHARED_INT_EXPRESSION=PASS addend=9 GNU_AS_text_equal=SKIP(toolchain-unavailable)'
fi


# Layout dispatch must be target-owned across distinct macro expansions
# and repeated short/long branches, not a hard-coded RISC-V parser in core.
cat >"$work/relax-many-macro.s" <<'EOF_RELAX_MANY'
.text
.macro branch_to dest
  beq a0, a1, \dest
.endm
.globl relax_many_macro
relax_many_macro:
.rept 3
  branch_to far_target
.endr
.globl relax_many_after_long
relax_many_after_long:
  branch_to near_target
near_target:
  ret
.space 6000
far_target:
  ret
EOF_RELAX_MANY
"$MINIAS" -o "$work/relax-many-macro.o" "$work/relax-many-macro.s"
multi_offset="$(readelf -sW "$work/relax-many-macro.o" |
    awk '$NF == "relax_many_after_long" {print $2}')"
test "$multi_offset" = '0000000000000018' || {
    echo "MiniAS target-owned relaxation yielded wrong multiple-branch layout: $multi_offset" >&2
    exit 1
}
echo 'MINIAS_RELAX_TARGET_HOOK=PASS long=3 short=1 macro_rept=3'


# Explicit RISC-V ISA profiles must constrain opcode families and advertise
# an ELF arch attribute matching the selected -march, not just set e_flags.
cat >"$work/isa-qualified.s" <<'EOF_ISA_QUAL'
.text
.globl isa_qualified
isa_qualified:
    mul a0, a1, a2
    ret
EOF_ISA_QUAL
"$MINIAS" -march=rv64im -mabi=lp64 -o "$work/isa-m.o" "$work/isa-qualified.s"
readelf -A "$work/isa-m.o" | grep -q 'Tag_RISCV_arch: "rv64i2p1_m2p0_zmmul1p0"'
for opts in 'rv64i mul a0,a1,a2' 'rv64im csrr a0,mstatus' \
            'rv64im orc.b a0,a1' 'rv64im c.li a0,1' \
            'rv64im fence.i' 'rv64i vsetvl a0,a1,a2'; do
    isa_arch=${opts%% *}
    isa_instruction=${opts#* }
    printf '.text\n%s\n' "$isa_instruction" >"$work/isa-negative.s"
    if "$MINIAS" "-march=$isa_arch" -mabi=lp64 -o "$work/isa-negative.o" \
        "$work/isa-negative.s" >/dev/null 2>"$work/isa-negative.err"; then
        echo "MiniAS accepted forbidden ISA opcode: $opts" >&2
        exit 1
    fi
done
for arch in rv64id rv64iq rv64gfoo rv64i_zmadeup rv64i_zbb1p0; do
    if "$MINIAS" "-march=$arch" -mabi=lp64 \
        -o "$work/isa-bad-profile.o" "$work/isa-qualified.s" \
        >/dev/null 2>"$work/isa-bad-profile.err"; then
        echo "MiniAS silently accepted unqualified ISA string: $arch" >&2
        exit 1
    fi
done
cat >"$work/isa-gc.s" <<'EOF_GC'
.text
.globl gc
gc:
    fadd.d fa0, fa1, fa2
    c.li a0, 1
    ret
EOF_GC
"$MINIAS" -march=rv64gc -mabi=lp64d -o "$work/isa-gc.o" "$work/isa-gc.s"
readelf -h "$work/isa-gc.o" | grep -q 'Flags:.*0x5'
readelf -A "$work/isa-gc.o" |
    grep -q 'Tag_RISCV_arch: "rv64i2p1_m2p0_a2p1_f2p2_d2p2_c2p0_zicsr2p0_zifencei2p0_zmmul1p0"'
printf '.option arch, +m\n.text\nmul a0, a1, a2\n' >"$work/isa-option.s"
if "$MINIAS" -march=rv64i -o "$work/isa-option.o" \
    "$work/isa-option.s" 2>"$work/isa-option.err"; then
    echo 'MiniAS ignored .option arch under explicit ISA selection' >&2
    exit 1
fi
printf '.text\n.word 0x02050533\n' >"$work/isa-raw.s"
if "$MINIAS" -march=rv64i -o "$work/isa-raw.o" \
    "$work/isa-raw.s" 2>"$work/isa-raw.err"; then
    echo 'MiniAS permitted raw executable ISA escape under strict RV64I' >&2
    exit 1
fi
"$MINIAS" -march=rv64im -o "$work/isa-raw-gnu-compatible.o" \
    "$work/isa-raw.s"
echo 'MINIAS_ISA_FEATURES=PASS RV64IM_RV64GC=1 ELF_ATTRIBUTES=2 negative=13'


# Even with M enabled, RV32 must never emit RV64 instructions. RV32E must
# reject all absent integer registers through both number and ABI spelling.
cat >"$work/isa-rv32im-legal.s" <<'EOF_RV32M'
.text
.globl rv32m
rv32m:
    mul a0, a1, a2
    ret
EOF_RV32M
"$MINIAS" -march=rv32im -mabi=ilp32 \
    -o "$work/isa-rv32im-legal.o" "$work/isa-rv32im-legal.s"
readelf -A "$work/isa-rv32im-legal.o" | grep -q 'rv32i2p1_m2p0'
for bad in 'ld a0, 0(a1)' 'addiw a0, a1, 1' 'mulw a0, a1, a2'; do
    printf '.text\n%s\n' "$bad" >"$work/isa-rv32im-bad.s"
    if "$MINIAS" -march=rv32im -mabi=ilp32 \
        -o "$work/isa-rv32im-bad.o" "$work/isa-rv32im-bad.s" \
        >"$work/isa-rv32im-bad.out" 2>"$work/isa-rv32im-bad.err"; then
        echo "MiniAS accepted RV64 opcode in RV32IM: $bad" >&2
        exit 1
    fi
done
for bad in 'addi a7, a0, 1' 'addi x16, x1, 1' 'addi s2, s0, 1'; do
    printf '.text\n%s\n' "$bad" >"$work/isa-e-bad.s"
    if "$MINIAS" -march=rv32e -mabi=ilp32e \
        -o "$work/isa-e-bad.o" "$work/isa-e-bad.s" \
        >"$work/isa-e-bad.out" 2>"$work/isa-e-bad.err"; then
        echo "MiniAS accepted register absent from RV32E: $bad" >&2
        exit 1
    fi
done
echo 'MINIAS_ISA_XLEN_RV32E=PASS rv32im_m=1 rv64_ops_blocked=3 rv32e_registers_blocked=3'


# Relaxation is now in-place for stable fragment sources: instruction growth
# must re-evaluate .size end anchors and .p2align padding without a reparse.
cat >"$work/fragment-inplace.s" <<'EOF_FRAG'
.text
.globl fragment_inplace
.type fragment_inplace, @function
fragment_inplace:
    beq a0, a1, far_fragment
    nop
.size fragment_inplace, .-fragment_inplace
.p2align 4
.globl fragment_aligned
fragment_aligned:
    nop
.space 5000
far_fragment:
    ret
EOF_FRAG
MINIAS_TRACE_RELAYOUT=1 "$MINIAS" -o "$work/fragment-inplace.o" \
    "$work/fragment-inplace.s" 2>"$work/fragment-inplace.trace"
grep -q '^minias-fragment-relayout:inplace-passes=' \
    "$work/fragment-inplace.trace" || {
    echo 'MiniAS failed to use persistent in-place fragment layout' >&2
    cat "$work/fragment-inplace.trace" >&2
    exit 1
}
frag_size="$(readelf -sW "$work/fragment-inplace.o" |
    awk '$NF == "fragment_inplace" { print $3 }')"
frag_align="$(readelf -sW "$work/fragment-inplace.o" |
    awk '$NF == "fragment_aligned" { print $2 }')"
test "$frag_size" = '12' && test "$frag_align" = '0000000000000010' || {
    echo "MiniAS fragment .size/.p2align shifted: size=$frag_size align=$frag_align" >&2
    exit 1
}
echo 'MINIAS_FRAGMENT_INPLACE=PASS branch_growth=4 size=12 aligned_offset=16'

# A literal .org is a retained absolute MCFragment position constraint:
# branch growth reduces its padding without re-opening the original source.
cat >"$work/fragment-org-fallback.s" <<'EOF_FRAG_ORG'
.text
.globl fragment_org_fallback
fragment_org_fallback:
    beq a0, a1, far_org
.org 32
.space 5000
far_org:
    ret
EOF_FRAG_ORG
MINIAS_TRACE_RELAYOUT=1 "$MINIAS" -o "$work/fragment-org-fallback.o" \
    "$work/fragment-org-fallback.s" 2>"$work/fragment-org-fallback.trace"
if ! grep -q 'minias-fragment-relayout:inplace-passes=' \
    "$work/fragment-org-fallback.trace" ||
   ! grep -q 'reparse=0' "$work/fragment-org-fallback.trace"; then
    echo 'MiniAS did not preserve fixed .org during in-place relaxation' >&2
    exit 1
fi
org_far_value="$(readelf -sW "$work/fragment-org-fallback.o" |
    awk '$NF == "far_org" { print $2 }')"
test "$org_far_value" = '00000000000013a8' || {
    echo "MiniAS fixed .org target drifted: far=$org_far_value" >&2
    exit 1
}

# A location-relative .org is a typed MC position constraint: the dot moves
# when the branch grows, and its relative padding stays exactly 28 bytes.
cat >"$work/fragment-org-dot.s" <<'EOF_FRAG_ORG_DOT'
.text
.globl fragment_org_dot
fragment_org_dot:
    beq a0, a1, far_org_dot
.org . + 28
.space 5000
far_org_dot:
    ret
EOF_FRAG_ORG_DOT
MINIAS_TRACE_RELAYOUT=1 "$MINIAS" -o "$work/fragment-org-dot.o" \
    "$work/fragment-org-dot.s" 2>"$work/fragment-org-dot.trace"
grep -q 'minias-fragment-relayout:inplace-passes=' \
    "$work/fragment-org-dot.trace" || {
    echo 'MiniAS failed to retain typed dot-relative .org' >&2
    exit 1
}
grep -q 'minias-fragment-graph:anchors_built=1.*reparse=0' \
    "$work/fragment-org-dot.trace"
echo 'MINIAS_FRAGMENT_ORG_POLICY=PASS fixed_inplace=1 dot_inplace=1'


# One typed-fixup object exercises semantic RISC-V CALL_PLT, PCREL_HI20,
# PCREL_LO12_I and ABS64 through the same late ELF relocation mapper.
cat >"$work/typed-fixup.s" <<'EOF_TYPED_FIXUP'
.text
.globl typed_fixup
typed_fixup:
    call external_function
    lla a0, external_data
    ret
.data
.globl typed_pointer
typed_pointer:
    .quad external_data + 8
EOF_TYPED_FIXUP
"$MINIAS" -o "$work/typed-fixup.o" "$work/typed-fixup.s"
typed_relocs="$(readelf -Wr "$work/typed-fixup.o")"
printf '%s\n' "$typed_relocs" | grep -q 'R_RISCV_CALL_PLT'
printf '%s\n' "$typed_relocs" | grep -q 'R_RISCV_PCREL_HI20'
printf '%s\n' "$typed_relocs" | grep -q 'R_RISCV_PCREL_LO12_I'
printf '%s\n' "$typed_relocs" | grep -q 'R_RISCV_64'
printf '%s\n' "$typed_relocs" | grep -q 'external_data[[:space:]]*+[[:space:]]*8'
echo 'MINIAS_TYPED_FIXUP=PASS call_plt=1 pcrel_hi20=1 pcrel_lo12_i=1 abs64_addend8=1'


# The RV64 production integer/branch/memory instructions now travel
# through typed MCInst. Test all opcode-format families against genuine
# GNU GAS bytes, including symbolic JAL, short conditional branches,
# immediate variants, RV64 word ops, and M-extension arithmetic.
cat >"$work/riscv-mcinst.s" <<'EOF_RV_MC'
.text
.globl riscv_mcinst
riscv_mcinst:
    add a0, a1, a2
    sub a3, a4, a5
    sll t0, t1, t2
    srl s0, s1, s2
    and a0, a1, a2
    or a0, a1, a2
    xor a0, a1, a2
    slt a0, a1, a2
    sltu a0, a1, a2
    mul a0, a1, a2
    mulh a0, a1, a2
    mulhu a0, a1, a2
    div a0, a1, a2
    remu a0, a1, a2
    addw a0, a1, a2
    subw a0, a1, a2
    mulw a0, a1, a2
    divuw a0, a1, a2
    addi a0, a1, -101
    addiw a0, a1, 101
    xori a0, a1, 1023
    sltiu a0, a1, 200
    slli a0, a1, 12
    srli a0, a1, 33
    srai a0, a1, 3
    slliw a0, a1, 6
    sraiw a0, a1, 7
    sext.w a0, a1
    lui a0, 0x123
    auipc a1, 0x456
    lb a0, 2(a1)
    lh a0, 4(a1)
    lw a0, 8(a1)
    lwu a0, 16(a1)
    ld a0, 24(a1)
    sb a0, 2(a1)
    sh a0, 4(a1)
    sw a0, 8(a1)
    sd a0, 16(a1)
    beq a0, a1, branch_target
    bne a0, a1, branch_target
    bgeu a0, a1, branch_target
    beqz a0, branch_target
    bgt a0, a1, branch_target
    jal ra, branch_target
    j branch_target
branch_target:
    ret
EOF_RV_MC
"$MINIAS" -march=rv64im -mabi=lp64 -o "$work/riscv-mcinst.o" \
    "$work/riscv-mcinst.s"
# GNU GAS requires explicit -i mnemonics for immediate instructions, but
# preserve legacy MiniAS arithmetic-with-immediate compatibility separately.
cat >"$work/riscv-mcinst-alias.s" <<'EOF_RV_ALIAS'
.text
.globl rv_mc_alias
rv_mc_alias:
    add a0, a1, 4
    sub a0, a1, 4
    and a0, a1, 3
    srl a0, a1, 4
    ret
EOF_RV_ALIAS
"$MINIAS" -march=rv64im -mabi=lp64 \
    -o "$work/riscv-mcinst-alias.o" "$work/riscv-mcinst-alias.s"
if command -v riscv64-linux-gnu-as >/dev/null 2>&1 &&
   command -v riscv64-linux-gnu-objcopy >/dev/null 2>&1; then
    riscv64-linux-gnu-as -march=rv64im -mabi=lp64 \
        -o "$work/riscv-mcinst.gnu.o" "$work/riscv-mcinst.s"
    riscv64-linux-gnu-objcopy -j .text -O binary \
        "$work/riscv-mcinst.o" "$work/riscv-mcinst.bin"
    riscv64-linux-gnu-objcopy -j .text -O binary \
        "$work/riscv-mcinst.gnu.o" "$work/riscv-mcinst.gnu.bin"
    cmp "$work/riscv-mcinst.bin" "$work/riscv-mcinst.gnu.bin"
    echo 'MINIAS_RV_MCINST=PASS GNU_AS_bytes_equal=1 typed_R_I_U_load_store_branch_jal=1'
else
    echo 'MINIAS_RV_MCINST=PASS GNU_AS_bytes_equal=SKIP(toolchain-unavailable)'
fi


# Reject malformed typed instruction forms at parse time rather than
# emitting a silently truncated machine instruction or a bogus fixup.
for rv_bad in 'add a0, a1' 'addi a0, a1' 'addi a0, a1, 4096' \
              'beq a0, a1' 'jal x32, invalid_target' \
              'sw a0, 0(x32)' 'slliw a0, a1, 32'; do
    printf '.text\n%s\n' "$rv_bad" >"$work/rv-typed-bad.s"
    if "$MINIAS" -march=rv64im -mabi=lp64 \
        -o "$work/rv-typed-bad.o" "$work/rv-typed-bad.s" \
        >"$work/rv-typed-bad.out" 2>"$work/rv-typed-bad.err"; then
        echo "MiniAS accepted invalid typed operand form: $rv_bad" >&2
        exit 1
    fi
done
echo 'MINIAS_RV_MCINST_NEGATIVE=PASS bad_register_imm_branch_memory=7'


# One consolidated typed MCInst sweep over F/D, V, A and Zicsr. GNU AS
# bytes must agree for machine instructions; the test never substitutes
# a hand-written expected encoding or counts "PR opened" as a validation.
cat >"$work/riscv-fp-atomic-vector-mcinst.s" <<'EOF_RV_EXT_MC'
.option norvc
.text
.globl riscv_fp_atomic_vector_mcinst
riscv_fp_atomic_vector_mcinst:
    fadd.s ft0, ft1, ft2
    fsub.s ft3, ft4, ft5, rtz
    fmul.d fa0, fa1, fa2
    fdiv.d fa3, fa4, fa5, rne
    fsgnjn.d ft0, ft1, ft2
    feq.d a0, fa0, fa1
    flt.d a1, fa1, fa2
    fle.d a2, fa2, fa3
    fmv.x.w a0, ft0
    fmv.w.x ft0, a0
    fmv.x.d a1, fa0
    fmv.d.x fa0, a1
    fcvt.d.w fa0, a0
    fcvt.w.d a0, fa0, rtz
    fcvt.d.s fa0, ft0
    fcvt.s.d ft0, fa0
    flw ft0, 4(a0)
    fld ft1, 8(a0)
    fsw ft0, 12(a0)
    fsd ft1, 16(a0)
    amoadd.w a0, a1, (a2)
    amoswap.d a0, a1, (a2)
    lr.w t0, (a0)
    sc.w.rl t0, t1, (a0)
    csrr t0, mstatus
    csrrw t0, mstatus, t1
    csrrc t0, mstatus, t1
    csrw mstatus, t0
    csrs mstatus, t0
    csrc mstatus, t0
    vsetvli t0, zero, e8, m1, ta, ma
    vsetvl t0, a0, a1
    vmv.v.i v1, 7
    vle8.v v1, (a0)
    vse16.v v2, (a1), v0.t
    ret
EOF_RV_EXT_MC
"$MINIAS" -march=rv64gcv -mabi=lp64d \
    -o "$work/riscv-fp-atomic-vector-mcinst.o" \
    "$work/riscv-fp-atomic-vector-mcinst.s"
if command -v riscv64-linux-gnu-as >/dev/null 2>&1 &&
   command -v riscv64-linux-gnu-objcopy >/dev/null 2>&1; then
    riscv64-linux-gnu-as -march=rv64gcv -mabi=lp64d \
        -o "$work/riscv-fp-atomic-vector-mcinst.gnu.o" \
        "$work/riscv-fp-atomic-vector-mcinst.s"
    riscv64-linux-gnu-objcopy -j .text -O binary \
        "$work/riscv-fp-atomic-vector-mcinst.o" \
        "$work/riscv-fp-atomic-vector-mcinst.bin"
    riscv64-linux-gnu-objcopy -j .text -O binary \
        "$work/riscv-fp-atomic-vector-mcinst.gnu.o" \
        "$work/riscv-fp-atomic-vector-mcinst.gnu.bin"
    cmp "$work/riscv-fp-atomic-vector-mcinst.bin" \
        "$work/riscv-fp-atomic-vector-mcinst.gnu.bin"
    echo 'MINIAS_RV_MCINST_EXT=PASS GNU_AS_FD_A_V_CSR_equal=1'
else
    echo 'MINIAS_RV_MCINST_EXT=PASS GNU_AS_FD_A_V_CSR_equal=SKIP(toolchain-unavailable)'
fi


# Typed RISC-V instructions now bypass the second legacy operand decode
# during layout measurement, so XLEN-specific checks must remain in
# the typed operand parser. Explicit RV32IM shifts cannot use 6-bit shamt.
for rv32_bad in 'slli a0, a1, 32' 'srli a0, a1, 63' \
                'srai a0, a1, 35'; do
    printf '.text\n%s\n' "$rv32_bad" >"$work/rv-typed-xlenshift.s"
    if "$MINIAS" -march=rv32im -mabi=ilp32 \
        -o "$work/rv-typed-xlenshift.o" "$work/rv-typed-xlenshift.s" \
        >"$work/rv-typed-xlenshift.out" 2>"$work/rv-typed-xlenshift.err"; then
        echo "MiniAS accepted RV32 shift above 31: $rv32_bad" >&2
        exit 1
    fi
done
echo 'MINIAS_RV_MCINST_MEASURE=PASS typed_fixed_4B=1 RV32_shamt_rejections=3'


# Production MCInst now covers compressed c.li, fixed supervisor/control,
# integer aliases, fence and jalr addressing forms; no duplicate legacy
# encoder remains. Verify actual GNU machine bytes, not shell-only markers.
cat >"$work/riscv-mcinst-remaining.s" <<'EOF_RV_REMAINING'
.option norvc
.text
.globl rv_remaining_typed
rv_remaining_typed:
    nop
    mv a0, a1
    jr t0
    snez a2, a1
    seqz a3, a2
    neg a4, a3
    negw a5, a4
    not a6, a5
    frcsr t0
    fscsr t1
    jalr ra, 12(a1)
    jalr a0
    jalr a1, a2, 0
    sfence.vma
    sfence.vma t0, t1
    fence
    fence r, w
    wfi
    ecall
    ebreak
    sret
    fence.i
    ret
EOF_RV_REMAINING
# Selected unprivileged -march correctly rejects sfence.vma/sret/wfi;
# default Linux assembly mode accepts privileged machine instructions.
"$MINIAS" -o "$work/riscv-mcinst-remaining.o" \
    "$work/riscv-mcinst-remaining.s"
cat >"$work/riscv-mcinst-compressed.s" <<'EOF_RV_C16'
.text
.globl rv_c16
rv_c16:
    c.li a0, 7
    c.li s4, -13
    c.li a1, 0
EOF_RV_C16
"$MINIAS" -march=rv64gc -mabi=lp64d \
    -o "$work/riscv-mcinst-compressed.o" "$work/riscv-mcinst-compressed.s"
if command -v riscv64-linux-gnu-as >/dev/null 2>&1 &&
   command -v riscv64-linux-gnu-objcopy >/dev/null 2>&1; then
    for kind in remaining compressed; do
        riscv64-linux-gnu-as -march=rv64gc -mabi=lp64d \
            -o "$work/riscv-mcinst-$kind.gnu.o" "$work/riscv-mcinst-$kind.s"
        riscv64-linux-gnu-objcopy -j .text -O binary \
            "$work/riscv-mcinst-$kind.o" "$work/riscv-mcinst-$kind.bin"
        riscv64-linux-gnu-objcopy -j .text -O binary \
            "$work/riscv-mcinst-$kind.gnu.o" "$work/riscv-mcinst-$kind.gnu.bin"
        cmp "$work/riscv-mcinst-$kind.bin" "$work/riscv-mcinst-$kind.gnu.bin"
    done
    echo 'MINIAS_RV_MCINST_ALIASES=PASS GNU_AS_fixed_and_C16_equal=1'
else
    echo 'MINIAS_RV_MCINST_ALIASES=PASS GNU_AS=SKIP(toolchain-unavailable)'
fi
for invalid in 'c.li x0, 1' 'c.li a0, 32' 'jalr ra, 4096(a1)' \
               'jalr a0, a1, -2049' 'mv a0' 'sfence.vma x32, a0' \
               'fence r' 'fscsr x32'; do
    printf '.text\n%s\n' "$invalid" >"$work/riscv-mcinst-remaining-bad.s"
    if "$MINIAS" -o "$work/riscv-mcinst-remaining-bad.o" \
        "$work/riscv-mcinst-remaining-bad.s" \
        >"$work/riscv-mcinst-remaining-bad.out" \
        2>"$work/riscv-mcinst-remaining-bad.err"; then
        echo "MiniAS incorrectly accepted typed operand: $invalid" >&2
        exit 1
    fi
done
echo 'MINIAS_RV_MCINST_ALIASES_NEGATIVE=PASS bad_operands=8'


# Symbolic GPR loads/stores are now two-word typed MCInst fragments,
# not a second string parser inside the old byte emitter. Compare
# exact GNU instructions AND relocation kinds for AUIPC+LOAD/STORE.
cat >"$work/riscv-symbol-memory-mcinst.s" <<'EOF_SYMMEM'
.option norvc
.option norelax
.text
.globl riscv_symbol_memory_mcinst
riscv_symbol_memory_mcinst:
    lb a0, ext_symbol
    lhu a1, ext_symbol+4
    lw a2, ext_symbol
    ld a3, ext_symbol+8
    sb a0, ext_symbol, t0
    sw a1, ext_symbol+4, t1
    sd a2, ext_symbol+8, t2
    ret
EOF_SYMMEM
"$MINIAS" -o "$work/riscv-symbol-memory-mcinst.o" \
    "$work/riscv-symbol-memory-mcinst.s"
readelf -Wr "$work/riscv-symbol-memory-mcinst.o" \
    >"$work/riscv-symbol-memory-mcinst.reloc"
test "$(grep -c 'R_RISCV_PCREL_HI20' \
    "$work/riscv-symbol-memory-mcinst.reloc")" -eq 7
test "$(grep -c 'R_RISCV_PCREL_LO12_I' \
    "$work/riscv-symbol-memory-mcinst.reloc")" -eq 4
test "$(grep -c 'R_RISCV_PCREL_LO12_S' \
    "$work/riscv-symbol-memory-mcinst.reloc")" -eq 3
if command -v riscv64-linux-gnu-as >/dev/null 2>&1 &&
   command -v riscv64-linux-gnu-objcopy >/dev/null 2>&1; then
    riscv64-linux-gnu-as -march=rv64gc -mabi=lp64d \
        -o "$work/riscv-symbol-memory-mcinst.gnu.o" \
        "$work/riscv-symbol-memory-mcinst.s"
    riscv64-linux-gnu-objcopy -j .text -O binary \
        "$work/riscv-symbol-memory-mcinst.o" \
        "$work/riscv-symbol-memory-mcinst.bin"
    riscv64-linux-gnu-objcopy -j .text -O binary \
        "$work/riscv-symbol-memory-mcinst.gnu.o" \
        "$work/riscv-symbol-memory-mcinst.gnu.bin"
    cmp "$work/riscv-symbol-memory-mcinst.bin" \
        "$work/riscv-symbol-memory-mcinst.gnu.bin"
    echo 'MINIAS_RV_MCINST_SYMBOL_MEMORY=PASS AUIPC_LOAD_STORE_GNU_bytes_equal=1 fixups=14'
else
    echo 'MINIAS_RV_MCINST_SYMBOL_MEMORY=PASS GNU_AS=SKIP(toolchain-unavailable) fixups=14'
fi
for invalid in 'ld a0, 2048(a1)' 'sd a0, -2049(a1)' \
               'lb a0, 4096(a1)' 'sw a0, bad.symbol, x32'; do
    printf '.text\n%s\n' "$invalid" >"$work/riscv-symbol-memory-bad.s"
    if "$MINIAS" -o "$work/riscv-symbol-memory-bad.o" \
        "$work/riscv-symbol-memory-bad.s" \
        >"$work/riscv-symbol-memory-bad.out" \
        2>"$work/riscv-symbol-memory-bad.err"; then
        echo "MiniAS accepted invalid memory MCInst: $invalid" >&2
        exit 1
    fi
done
echo 'MINIAS_RV_MCINST_MEMORY_RANGE=PASS GPR_offset_and_register=4'


# Variable-length LI and PIC/NOPIC multiword address pseudos must be emitted
# from the persisted MCInst, not reparsed from source in the backend. Validate
# actual ELF relocation kinds/addends plus GNU AS machine-code equality.
cat >"$work/riscv-multi-mcinst.s" <<'EOF_RV_MULTI'
.option norelax
.text
.globl typed_multi
typed_multi:
    li a0, 2047
    li a1, -2048
    li a2, 2048
    li a3, 0x12345678
    call external_function
    tail external_function
    lla a4, external_data + 8
.option pic
    la a5, external_data
.option nopic
    la a6, external_data + 4
    ret
EOF_RV_MULTI
"$MINIAS" -o "$work/riscv-multi-mcinst.o" "$work/riscv-multi-mcinst.s"
readelf -Wr "$work/riscv-multi-mcinst.o" >"$work/riscv-multi-mcinst.reloc"
test "$(grep -c 'R_RISCV_CALL_PLT' "$work/riscv-multi-mcinst.reloc")" -eq 2
test "$(grep -c 'R_RISCV_PCREL_HI20' "$work/riscv-multi-mcinst.reloc")" -eq 2
test "$(grep -c 'R_RISCV_GOT_HI20' "$work/riscv-multi-mcinst.reloc")" -eq 1
test "$(grep -c 'R_RISCV_PCREL_LO12_I' "$work/riscv-multi-mcinst.reloc")" -eq 3
if command -v riscv64-linux-gnu-as >/dev/null 2>&1 &&
   command -v riscv64-linux-gnu-objcopy >/dev/null 2>&1; then
    riscv64-linux-gnu-as -march=rv64im -mabi=lp64 \
        -o "$work/riscv-multi-mcinst.gnu.o" "$work/riscv-multi-mcinst.s"
    riscv64-linux-gnu-objcopy -j .text -O binary \
        "$work/riscv-multi-mcinst.o" "$work/riscv-multi-mcinst.bin"
    riscv64-linux-gnu-objcopy -j .text -O binary \
        "$work/riscv-multi-mcinst.gnu.o" "$work/riscv-multi-mcinst.gnu.bin"
    cmp "$work/riscv-multi-mcinst.bin" "$work/riscv-multi-mcinst.gnu.bin"
    echo 'MINIAS_RV_MULTI_MCINST=PASS GNU_AS_bytes_equal=1 reloc_CALL_GOT_PCREL=8'
else
    echo 'MINIAS_RV_MULTI_MCINST=PASS GNU_AS=SKIP(toolchain-unavailable) reloc=8'
fi
cat >"$work/riscv-multi-rv32.s" <<'EOF_RV_MULTI_32'
.text
.globl rv32_multi
rv32_multi:
    li a0, 0x80000000
    li a1, -2147483648
    li a2, 0x7fffffff
    ret
EOF_RV_MULTI_32
"$MINIAS" -march=rv32im -mabi=ilp32 \
    -o "$work/riscv-multi-rv32.o" "$work/riscv-multi-rv32.s"
if command -v riscv64-linux-gnu-as >/dev/null 2>&1 &&
   command -v riscv64-linux-gnu-objcopy >/dev/null 2>&1; then
    riscv64-linux-gnu-as -march=rv32im -mabi=ilp32 \
        -o "$work/riscv-multi-rv32.gnu.o" "$work/riscv-multi-rv32.s"
    riscv64-linux-gnu-objcopy -j .text -O binary \
        "$work/riscv-multi-rv32.o" "$work/riscv-multi-rv32.bin"
    riscv64-linux-gnu-objcopy -j .text -O binary \
        "$work/riscv-multi-rv32.gnu.o" "$work/riscv-multi-rv32.gnu.bin"
    cmp "$work/riscv-multi-rv32.bin" "$work/riscv-multi-rv32.gnu.bin"
fi
for invalid in 'li a0' 'li x32, 1' 'li a0, bad_symbol' \
               'call' 'tail a0, external_function' \
               'lla x32, external_data' 'la a0'; do
    printf '.text\n%s\n' "$invalid" >"$work/riscv-multi-bad.s"
    if "$MINIAS" -o "$work/riscv-multi-bad.o" "$work/riscv-multi-bad.s" \
        >"$work/riscv-multi-bad.out" 2>"$work/riscv-multi-bad.err"; then
        echo "MiniAS accepted invalid variable pseudo MCInst: $invalid" >&2
        exit 1
    fi
done
printf '.text\nli a0, 0x100000000\n' >"$work/riscv-multi-rv32-overflow.s"
if "$MINIAS" -march=rv32im -mabi=ilp32 \
    -o "$work/riscv-multi-rv32-overflow.o" \
    "$work/riscv-multi-rv32-overflow.s" >/dev/null 2>&1; then
    echo 'MiniAS accepted out-of-width RV32IM li' >&2
    exit 1
fi
echo 'MINIAS_RV_MULTI_MCINST_NEGATIVE=PASS invalid_operands=7 rv32_overflow=1'


# The last raw R/I escape is also target-parsed MCInst: no text-dependent
# RISC-V encoder fallback may survive migration. Compare both GNU forms.
cat >"$work/riscv-raw-mcinst.s" <<'EOF_RV_RAW'
.text
.globl rv_raw
rv_raw:
    .insn r 0x33, 0, 0, a0, a1, a2
    .insn i 0x13, 0, a3, a4, -13
EOF_RV_RAW
"$MINIAS" -o "$work/riscv-raw-mcinst.o" "$work/riscv-raw-mcinst.s"
if command -v riscv64-linux-gnu-as >/dev/null 2>&1 &&
   command -v riscv64-linux-gnu-objcopy >/dev/null 2>&1; then
    riscv64-linux-gnu-as -march=rv64i -mabi=lp64 \
        -o "$work/riscv-raw-mcinst.gnu.o" "$work/riscv-raw-mcinst.s"
    riscv64-linux-gnu-objcopy -j .text -O binary \
        "$work/riscv-raw-mcinst.o" "$work/riscv-raw-mcinst.bin"
    riscv64-linux-gnu-objcopy -j .text -O binary \
        "$work/riscv-raw-mcinst.gnu.o" "$work/riscv-raw-mcinst.gnu.bin"
    cmp "$work/riscv-raw-mcinst.bin" "$work/riscv-raw-mcinst.gnu.bin"
    echo 'MINIAS_RV_RAW_MCINST=PASS GNU_R_I_bytes_equal=1'
else
    echo 'MINIAS_RV_RAW_MCINST=PASS GNU_AS=SKIP(toolchain-unavailable)'
fi
for invalid in '.insn r 0x33, 0, 0, x32, a1, a2' \
               '.insn i 0x13, 0, a0, a1, 4096'; do
    printf '.text\n%s\n' "$invalid" >"$work/riscv-raw-invalid.s"
    if "$MINIAS" -o "$work/riscv-raw-invalid.o" \
        "$work/riscv-raw-invalid.s" >/dev/null 2>&1; then
        echo "MiniAS accepted invalid typed .insn: $invalid" >&2
        exit 1
    fi
done
echo 'MINIAS_RV_RAW_MCINST_NEGATIVE=PASS invalid_raw=2'


# Persisted MCInst must determine its own fragment length *before* section
# layout. This simultaneously covers 4B/8B LI, 8B symbolic memory, local
# branch addressing, ELF symbol .size, and real GNU GAS byte identity.
cat >"$work/riscv-parsed-fragment-size.s" <<'EOF_PARSED_SIZE'
.option norvc
.text
.globl rv_parsed_fragment_size
.type rv_parsed_fragment_size,@function
rv_parsed_fragment_size:
    li a0, -2048
    li a1, 2048
    li a2, 0x12345678
    lw a3, external_data
    beq a0, a1, .Ltyped_size_end
.Ltyped_size_end:
    ret
.size rv_parsed_fragment_size, .-rv_parsed_fragment_size
EOF_PARSED_SIZE
"$MINIAS" -o "$work/riscv-parsed-fragment-size.o" \
    "$work/riscv-parsed-fragment-size.s"
typed_size="$(readelf -Ws "$work/riscv-parsed-fragment-size.o" |
    awk '$NF == "rv_parsed_fragment_size" { print $3 }')"
test "$typed_size" = "36"
if command -v riscv64-linux-gnu-as >/dev/null 2>&1 &&
   command -v riscv64-linux-gnu-objcopy >/dev/null 2>&1; then
    riscv64-linux-gnu-as -march=rv64i -mabi=lp64 \
        -o "$work/riscv-parsed-fragment-size.gnu.o" \
        "$work/riscv-parsed-fragment-size.s"
    riscv64-linux-gnu-objcopy -j .text -O binary \
        "$work/riscv-parsed-fragment-size.o" \
        "$work/riscv-parsed-fragment-size.bin"
    riscv64-linux-gnu-objcopy -j .text -O binary \
        "$work/riscv-parsed-fragment-size.gnu.o" \
        "$work/riscv-parsed-fragment-size.gnu.bin"
    cmp "$work/riscv-parsed-fragment-size.bin" \
        "$work/riscv-parsed-fragment-size.gnu.bin"
    echo 'MINIAS_RV_PARSED_LAYOUT=PASS GNU_AS_bytes_equal=1 symbol_size=36'
else
    echo 'MINIAS_RV_PARSED_LAYOUT=PASS GNU_AS=SKIP(toolchain-unavailable) symbol_size=36'
fi

echo "MINIAS_A0=PASS objects=30 abi_flags=3 format=ELF64-RISCV-ET_REL relocations=23 strings=2 pseudos=17 previous=3 subsection=2 numeric_labels=18 ecall=1 isa_next=13 csr_amo=8 sfence_vma=3 fence_i=1 vsetvl=1 vsetvli=1 vmv_v_i=4 rept=2 nested_rept=1 irp=4 section_stack=1 org=3 local_difference=1 lr_sc=2 inline_labels=2 branch_pseudos=8 extern=1 symbol_minus_dot=4 symbol_difference=1 absolute32=1 jal=2 jal_subsection=1 high_numeric_labels=2 u64_data=3 raw_insn=4 set_alias=3 move=1 numeric_zero=1 immediate_product=2 shift_immediate=1 incbin=1 align_expr=1 native_expr=6 native_zbb_tail_memexpr=1 native_jalr_symbol_load=1 native_forward_short=1 native_mixed_macro=1 native_divided_difference=1 native_symbol_addend_expr=1 native_add_sub_immediate=1 native_cfi_boundary=1 native_gas_forms=1 native_set_size=1 native_macro_comma=1 native_macro_quotes=1 pic_la_got=1 sext_w=1 macro=4 conditional=1"
