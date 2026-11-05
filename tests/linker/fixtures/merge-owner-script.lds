OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)
SECTIONS {
    . = 0x400000;
    .text : { *(.text) }
    .data : { *(.data) }
    .merge_one : { *a.o(.rodata.str1.1) }
    .merge_two : { *b.o(.rodata.str1.1) }
    .constants : { *(.rodata.cst4) }
    .wide2 : { *(.rodata.str2.2) }
    .wide4 : { *(.rodata.str4.4) }
}
