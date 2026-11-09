#include <stdint.h>
#include <stdio.h>

int minic_popcount_u32(unsigned int value);
int minic_popcount_ulong(unsigned long value);
int minic_popcount_u64(unsigned long long value);

static int reference_count(uint64_t value) {
    int count = 0;
    while (value != 0U) {
        value &= value - UINT64_C(1);
        ++count;
    }
    return count;
}

int main(void) {
    uint64_t value = UINT64_C(0x9e3779b97f4a7c15);
    unsigned int index;
    for (index = 0; index < 100000U; ++index) {
        value = value * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
        if (minic_popcount_u32((unsigned int)value) !=
                reference_count((uint32_t)value) ||
            minic_popcount_ulong((unsigned long)value) !=
                reference_count((unsigned long)value) ||
            minic_popcount_u64((unsigned long long)value) != reference_count(value)) {
            (void)fprintf(stderr, "popcount mismatch iteration=%u value=%llu\n",
                          index, (unsigned long long)value);
            return 1;
        }
    }
    return 0;
}
