/* Linux arch/loongarch/kernel/unaligned.c: every FPR0..31 must be able
 * to exchange its raw 64-bit payload with a GPR. These are ISA state
 * transfers (not fp/int arithmetic), compile-only on the host. */
#define REGISTER_PAIR(number) \
    unsigned long fpr_read_##number(void) { \
        unsigned long value; \
        __asm__ __volatile__("movfr2gr.d %0, $f" #number "\n\t" \
            : "=r" (value)); \
        return value; \
    } \
    void fpr_write_##number(unsigned long value) { \
        __asm__ __volatile__("movgr2fr.d $f" #number ", %0\n\t" \
            : : "r" (value)); \
    }

REGISTER_PAIR(0)
REGISTER_PAIR(1)
REGISTER_PAIR(2)
REGISTER_PAIR(3)
REGISTER_PAIR(4)
REGISTER_PAIR(5)
REGISTER_PAIR(6)
REGISTER_PAIR(7)
REGISTER_PAIR(8)
REGISTER_PAIR(9)
REGISTER_PAIR(10)
REGISTER_PAIR(11)
REGISTER_PAIR(12)
REGISTER_PAIR(13)
REGISTER_PAIR(14)
REGISTER_PAIR(15)
REGISTER_PAIR(16)
REGISTER_PAIR(17)
REGISTER_PAIR(18)
REGISTER_PAIR(19)
REGISTER_PAIR(20)
REGISTER_PAIR(21)
REGISTER_PAIR(22)
REGISTER_PAIR(23)
REGISTER_PAIR(24)
REGISTER_PAIR(25)
REGISTER_PAIR(26)
REGISTER_PAIR(27)
REGISTER_PAIR(28)
REGISTER_PAIR(29)
REGISTER_PAIR(30)
REGISTER_PAIR(31)
