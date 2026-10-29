unsigned long minic_linux_xpaclri_wrong_template(unsigned long ptr) {
    register unsigned long hard_lr asm("x30") = ptr;
    asm("nop" : "+r" (hard_lr));
    return hard_lr;
}
