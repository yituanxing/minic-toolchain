/* File ISA has C. Local .option norvc/rvc changes only the effective site
 * ISA, while source DATA/INSN alternation must retain exact mapping names. */
.text
.globl rv_mapping_rvc_entry
rv_mapping_rvc_entry:
    .word 0x00000013
    addi a0,a0,1
.option norvc
    addi a0,a0,2
.option rvc
    c.li a0,3
.option push
.option arch,+zbb
    ctz a1,a1
.option pop
    .word 0x00000013
    .insn r 0x33, 0, 0, a0, a0, a1
    addi a0,a0,3
