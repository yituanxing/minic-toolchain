#include <stdint.h>

extern uint32_t x86_p2_kvm_div_frac(uint32_t dividend, uint32_t divisor);
extern int x86_p2_kvm_clear_steal_preempted(volatile unsigned char *address,
                                            unsigned char *previous);
extern int x86_p2_kvm_put_user_byte(unsigned char value,
                                    volatile unsigned char *address);
extern int x86_p2_kvm_get_user_byte(volatile unsigned char *address,
                                    unsigned char *value);

int main(void)
{
    static const uint32_t inputs[][2] = {
        {0, 1}, {1, 3}, {2, 3}, {3, 4}, {6, 7}, {100, 101},
        {65535, 65536}, {0x7fffffffU, 0x80000000U},
        {0xfffffffeU, 0xffffffffU}
    };
    volatile unsigned char memory = 0x5a;
    unsigned char previous = 0, loaded = 0;
    unsigned int index;

    for (index = 0; index < sizeof(inputs) / sizeof(inputs[0]); ++index) {
        const uint32_t dividend = inputs[index][0];
        const uint32_t divisor = inputs[index][1];
        const uint32_t exact = (uint32_t)(((uint64_t)dividend << 32) / divisor);
        if (x86_p2_kvm_div_frac(dividend, divisor) != exact) return 1;
    }
    if (x86_p2_kvm_clear_steal_preempted(&memory, &previous)) return 2;
    if (previous != 0x5a || memory != 0) return 3;
    if (x86_p2_kvm_put_user_byte(0x83, &memory)) return 4;
    if (memory != 0x83) return 5;
    if (x86_p2_kvm_get_user_byte(&memory, &loaded)) return 6;
    if (loaded != 0x83) return 7;
    return 0;
}
