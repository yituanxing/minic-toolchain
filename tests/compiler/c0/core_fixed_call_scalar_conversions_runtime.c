#include <stdio.h>

unsigned int core_m3_integer_conversion(void);
const volatile void *core_m3_pointer_qualification(const void *value);
const void *core_m3_null_pointer(void);
_Bool core_m3_pointer_bool(const void *value);
unsigned long core_m3_read_word_at_a_time(const void *address);

int main(void) {
    unsigned long word;
    int value;

    word = 0x1020304050607080UL;
    value = 17;
    (void)printf("%u %d %d %d %d %lu\n",
                 core_m3_integer_conversion(),
                 core_m3_pointer_qualification(&value) == &value,
                 core_m3_null_pointer() == 0,
                 core_m3_pointer_bool(&value),
                 core_m3_pointer_bool(0),
                 core_m3_read_word_at_a_time(&word));
    return 0;
}
