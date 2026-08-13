int file_asm_target;

asm(".section \".export_symbol\",\"a\" ; __export_symbol_file_asm_target: ; "
    ".asciz \"\" ; .ascii \"\" \"\\0\" ; .balign 8 ; .quad file_asm_target ; .previous");

__asm__(".section \".minic.fileasm\",\"a\" ; __minic_file_asm_second: ; "
        ".ascii \"%\" ; .previous");

__asm(".section \".minic.fileasm\",\"a\" ; __minic_file_asm_third: ; .previous");
