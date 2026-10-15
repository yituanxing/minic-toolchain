#include <stdio.h>

typedef unsigned short core_m22_u16;

unsigned int core_m22_or(unsigned int left, unsigned int right);
unsigned int core_m22_shift_left(unsigned int value, unsigned int count);
unsigned int core_m22_shift_right_unsigned(unsigned int value, unsigned int count);
int core_m22_shift_right_signed(int value, unsigned int count);
core_m22_u16 core_m22_fswab16(core_m22_u16 val);

int main(void) {
    printf("or=%u\n", core_m22_or(0x1200U, 0x0034U));
    printf("shl=%u\n", core_m22_shift_left(0x12U, 8U));
    printf("shru=%u\n", core_m22_shift_right_unsigned(0x123400U, 8U));
    printf("shrs=%d\n", core_m22_shift_right_signed(-256, 8U));
    printf("swab=%u,%u,%u\n",
           (unsigned int)core_m22_fswab16((core_m22_u16)0x1234U),
           (unsigned int)core_m22_fswab16((core_m22_u16)0x00ffU),
           (unsigned int)core_m22_fswab16((core_m22_u16)0xa500U));
    return 0;
}
