#include <stdio.h>

struct CoreBitcastPair {
    int offset;
    int value;
};

void *core_offset_to_ptr(const int *offset);
int core_pointer_read(const int *value);
int core_member_read(struct CoreBitcastPair *pair);
unsigned long core_pointer_bits(const void *value);

int main(void) {
    struct CoreBitcastPair pair;
    void *resolved;
    int direct;
    int member;
    unsigned long bits;

    pair.offset = (int)((char *)&pair.value - (char *)&pair.offset);
    pair.value = 73;
    resolved = core_offset_to_ptr(&pair.offset);
    direct = core_pointer_read(&pair.value);
    member = core_member_read(&pair);
    bits = core_pointer_bits(&pair.value);
    printf("%d %d %d %d\n",
           resolved == (void *)&pair.value,
           direct,
           member,
           bits == (unsigned long)&pair.value);
    return resolved == (void *)&pair.value && direct == 73 && member == 73 &&
                   bits == (unsigned long)&pair.value
               ? 0
               : 1;
}
