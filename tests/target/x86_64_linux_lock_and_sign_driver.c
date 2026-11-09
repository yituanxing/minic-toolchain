#include <stdint.h>
#include <stdio.h>

_Bool minic_lock_and_sign(long, volatile unsigned long *);

int main(void) {
    unsigned int bit, value;
    for (bit = 0U; bit != 8U; ++bit) {
        for (value = 0U; value != 256U; ++value) {
            const unsigned char expected = (unsigned char)
                (value & ~(1U << bit));
            volatile unsigned long memory = 0x1234567800000000UL | value;
            const _Bool sign = minic_lock_and_sign((long)bit,&memory);
            if ((unsigned char)memory != expected ||
                sign != ((_Bool)((expected & 0x80U) != 0U))) {
                fprintf(stderr,"lock and sign: bit=%u initial=%u result=%lx sign=%u\n",
                    bit,value,memory,(unsigned int)sign);
                return 1;
            }
            if ((memory & ~0xffUL) != 0x1234567800000000UL)
                return 2;
        }
    }
    return 0;
}
