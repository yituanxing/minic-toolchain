.text
.globl _start
.type _start,@function
_start:
    /* MiniAS emits pcaddi but does not yet expose PCREL20_S2 .reloc.
     * The dedicated oracle transplants the ELF RELA entry below onto this
     * exact PCADDI instruction without altering its symbol or addend. */
    pcaddi $r14,0
    .long weak_optional_data
.size _start,.-_start
.weak weak_optional_data
