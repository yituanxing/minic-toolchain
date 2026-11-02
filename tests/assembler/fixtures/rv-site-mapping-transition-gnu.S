/* Mapping-symbol diagnostic: the file-wide ISA remains rv32/64i2p1 while
 * the lexical instruction ISA changes inside push/pop regions. Keep real
 * instructions and ordinary data in executable sections so GNU's mapping
 * symbol placement can be compared without guessing it in the test. */
.option norvc
.text
.globl rv_mapping_entry
rv_mapping_entry:
    addi a0,a0,1
.option push
.option arch,+zbb
    ctz a0,a0
.option push
.option arch,+v
    vsetvli a1,a0,e8,m1,ta,ma
.option pop
    orc.b a2,a2
.option pop
    addi a3,a3,2
    .word 0x00000013
    addi a4,a4,3

/* Exercise final offset order rather than source-text subsection order. */
.subsection 2
.option push
.option arch,+v
    vsetvli a5,a5,e8,m1,ta,ma
.option pop
    .word 0x00000013
.subsection 0
    addi a6,a6,4

/* Local ISA changes are not allowed to upgrade file Tag_RISCV_arch even
 * when the region is the first text in a distinct executable section. */
.section .text.mapping_extra,"ax",@progbits
.option push
.option arch,+zbb
    ctz a0,a0
.option pop
    .word 0x00000013
    addi a0,a0,5
