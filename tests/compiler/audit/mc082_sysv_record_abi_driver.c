#include "mc082_sysv_record_abi.h"

int main(void) {
#define MC082_VERIFY(N) do { \
        struct MC082_R##N value = mc082_make_##N(7U); \
        struct MC082_R##N outgoing = {0}; \
        unsigned int index, expected = 0U; \
        for (index = 0U; index < N; ++index) { \
            unsigned char byte = (unsigned char)(7U + index * 3U); \
            if (value.bytes[index] != byte) return N; \
            outgoing.bytes[index] = byte; \
            expected += byte; \
        } \
        if (mc082_check_##N(value) != expected) return 20 + N; \
        if (mc082_check_##N(outgoing) != expected) return 40 + N; \
    } while (0);
    MC082_SIZES(MC082_VERIFY)
#undef MC082_VERIFY
    if (mc082_check_packed5(mc082_make_packed5(0x12345678U)) != 42U)
        return 60;
    if (mc082_check_aligned8(mc082_make_aligned8(7U)) != 24U)
        return 61;
    if (mc082_check_padded8(mc082_make_padded8(0x12345678U)) != 42U)
        return 65;
    if (mc082_check_padded16(mc082_make_padded16(0x1234567887654321ULL)) != 42U)
        return 66;
    {
        struct MC082_R3 three = mc082_make_3(7U);
        struct MC082_R9 nine = mc082_make_9(7U);
        struct MC082_R15 fifteen = mc082_make_15(7U);
        unsigned int (*volatile indirect3)(struct MC082_R3) = mc082_check_3;
        if (mc082_stack3(1UL, 2UL, 3UL, 4UL, 5UL, 6UL, three) !=
            21U + mc082_check_3(three)) return 62;
        if (mc082_stack9(1UL, 2UL, 3UL, 4UL, 5UL, nine) !=
            15U + mc082_check_9(nine)) return 63;
        if (mc082_stack15(1UL, 2UL, 3UL, 4UL, 5UL, fifteen) !=
            15U + mc082_check_15(fifteen)) return 64;
        if (indirect3(three) != mc082_check_3(three)) return 67;
    }
    return 42;
}
