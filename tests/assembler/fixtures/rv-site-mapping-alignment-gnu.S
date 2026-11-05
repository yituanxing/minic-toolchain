/* Zero-width fill and code alignment can create transient GNU mapping
 * events. The final symbol list must follow GNU's fragment reconciliation. */
.text
.globl rv_mapping_align_entry
rv_mapping_align_entry:
    addi a0,a0,1
    .fill 1,0,0
    addi a0,a0,2
    .word 0x00000013
    .p2align 3
    addi a0,a0,3
    .fill 1,0,0
    addi a0,a0,4
