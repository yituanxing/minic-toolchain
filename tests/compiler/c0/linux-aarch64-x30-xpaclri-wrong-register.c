unsigned long minic_linux_xpaclri_wrong_register(unsigned long ptr) {
    register unsigned long hard_frame_pointer asm("x29") = ptr;
    asm("hint #7" : "+r" (hard_frame_pointer));
    return hard_frame_pointer;
}
