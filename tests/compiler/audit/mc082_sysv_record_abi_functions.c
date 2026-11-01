#include "mc082_sysv_record_abi.h"

#define MC082_FUNCTIONS(N) \
    struct MC082_R##N mc082_make_##N(unsigned char seed) { \
        struct MC082_R##N result = {0}; \
        unsigned int index; \
        for (index = 0U; index < N; ++index) \
            result.bytes[index] = (unsigned char)(seed + index * 3U); \
        return result; \
    } \
    unsigned int mc082_check_##N(struct MC082_R##N value) { \
        unsigned int index, checksum = 0U; \
        for (index = 0U; index < N; ++index) \
            checksum += value.bytes[index]; \
        return checksum; \
    }
MC082_SIZES(MC082_FUNCTIONS)
#undef MC082_FUNCTIONS

struct MC082_Packed5 mc082_make_packed5(unsigned int value) {
    struct MC082_Packed5 result = {0};
    result.word = value;
    result.tail = 0xa5U;
    return result;
}

unsigned int mc082_check_packed5(struct MC082_Packed5 value) {
    return value.word == 0x12345678U && value.tail == 0xa5U ? 42U : 1U;
}

struct MC082_Aligned8 mc082_make_aligned8(unsigned char seed) {
    struct MC082_Aligned8 result = {0};
    result.bytes[0] = seed;
    result.bytes[1] = (unsigned char)(seed + 1U);
    result.bytes[2] = (unsigned char)(seed + 2U);
    return result;
}

unsigned int mc082_check_aligned8(struct MC082_Aligned8 value) {
    return (unsigned int)value.bytes[0] + value.bytes[1] + value.bytes[2];
}

struct MC082_Padded8 mc082_make_padded8(unsigned int value) {
    struct MC082_Padded8 result = {0};
    result.tag = 0xa7U;
    result.word = value;
    return result;
}

unsigned int mc082_check_padded8(struct MC082_Padded8 value) {
    return value.tag == 0xa7U && value.word == 0x12345678U ? 42U : 1U;
}

struct MC082_Padded16 mc082_make_padded16(unsigned long long value) {
    struct MC082_Padded16 result = {0};
    result.tag = 0xb8U;
    result.word = value;
    return result;
}

unsigned int mc082_check_padded16(struct MC082_Padded16 value) {
    return value.tag == 0xb8U && value.word == 0x1234567887654321ULL ? 42U : 1U;
}

unsigned int mc082_stack3(unsigned long a, unsigned long b, unsigned long c,
                          unsigned long d, unsigned long e, unsigned long f,
                          struct MC082_R3 value) {
    return (unsigned int)(a + b + c + d + e + f) +
           value.bytes[0] + value.bytes[1] + value.bytes[2];
}

unsigned int mc082_stack9(unsigned long a, unsigned long b, unsigned long c,
                          unsigned long d, unsigned long e, struct MC082_R9 value) {
    return (unsigned int)(a + b + c + d + e) + mc082_check_9(value);
}

unsigned int mc082_stack15(unsigned long a, unsigned long b, unsigned long c,
                           unsigned long d, unsigned long e, struct MC082_R15 value) {
    return (unsigned int)(a + b + c + d + e) + mc082_check_15(value);
}
