#include <stdio.h>

int core_m14_bitwise_and(int left, int right);
int core_m14_compound_int(int value, int right);
unsigned char core_m14_compound_uchar(unsigned char value, unsigned int right);
_Bool core_m14_compound_bool(_Bool value, _Bool right);
int core_m14_single_lvalue(int *value, int right);
_Bool core_m14_linux_tail(int value);

static int pick_calls;

int *core_m14_pick(int *value) {
    ++pick_calls;
    return value;
}

_Bool core_m14_report(int value) {
    return value != 0;
}

int main(void) {
    int value = 127;
    int single_result;

    single_result = core_m14_single_lvalue(&value, 15);
    printf("%d %d %u %d %d %d %d %d %d\n",
           core_m14_bitwise_and(0x5a, 0x3c),
           core_m14_compound_int(0x7f, 0x35),
           (unsigned int)core_m14_compound_uchar(0xf3U, 0x5aU),
           core_m14_compound_bool(1, 1),
           core_m14_compound_bool(1, 0),
           single_result,
           value,
           pick_calls,
           (int)core_m14_linux_tail(5) * 10 + (int)core_m14_linux_tail(0));
    return 0;
}
