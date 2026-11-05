SECTIONS {
    . = 0x9000000000220000;
    .text : { *(.text) *(.text.*) }
    .notes : { *(.note.*) }
    .rodata : { *(.rodata) *(.rodata.*) }
    .data : { *(.data) *(.data.*) }
    .bss : { *(.bss) *(.bss.*) }
}
