/* Linux's 64-bit XSAVE/XRSTOR operations need exact ModRM bytes, EAX/EDX
 * feature masks, and a type-15 exception-table repair entry. */
struct xstate_image { unsigned char image[1024]; };

#define XSTATE_FAULT(opcode) \
    "1:" ".byte " opcode "\n\t" \
    "xor %[err], %[err]\n" "2:\n\t" \
    " .pushsection \"__ex_table\",\"a\"\n" \
    " .balign 4\n" \
    " .long (" "1b" ") - .\n" \
    " .long (" "2b" ") - .\n" \
    " .long " "15" " \n" \
    " .popsection\n"

int linux_xsave64(struct xstate_image *p, unsigned int low, unsigned int high) {
    int err;
    asm volatile(XSTATE_FAULT("0x48, 0x0f,0xae,0x27")
                 : [err] "=a"(err)
                 : "D"(p), "m"(*p), "a"(low), "d"(high) : "memory");
    return err;
}

int linux_xrstor64(struct xstate_image *p, unsigned int low, unsigned int high) {
    int err;
    asm volatile(XSTATE_FAULT("0x48, 0x0f,0xae,0x2f")
                 : [err] "=a"(err)
                 : "D"(p), "m"(*p), "a"(low), "d"(high) : "memory");
    return err;
}

int linux_xrstors64(struct xstate_image *p, unsigned int low, unsigned int high) {
    int err;
    asm volatile(XSTATE_FAULT("0x48, 0x0f,0xc7,0x1f")
                 : [err] "=a"(err)
                 : "D"(p), "m"(*p), "a"(low), "d"(high) : "memory");
    return err;
}

int linux_xsaves64(struct xstate_image *p, unsigned int low, unsigned int high) {
    int err;
    asm volatile(XSTATE_FAULT("0x48, 0x0f,0xc7,0x2f")
                 : [err] "=a"(err)
                 : "D"(p), "m"(*p), "a"(low), "d"(high) : "memory");
    return err;
}
