#include <stdint.h>
#include <stdio.h>

struct minic_ipv6_words { uint64_t q[2]; };
uint64_t minic_ipv6_checksum(uint64_t,
    const struct minic_ipv6_words *, const struct minic_ipv6_words *);

static uint64_t reference(uint64_t rest,
    const struct minic_ipv6_words *src,
    const struct minic_ipv6_words *dst) {
    uint64_t words[] = {src->q[0],src->q[1],dst->q[0],dst->q[1]};
    uint64_t carry = 0;
    unsigned int i;
    for (i = 0; i < 4; ++i) {
        const __uint128_t sum = (__uint128_t)rest + words[i] + carry;
        rest = (uint64_t)sum;
        carry = (uint64_t)(sum >> 64);
    }
    return rest + carry;
}

int main(void) {
    static const struct {
        uint64_t rest;
        struct minic_ipv6_words src,dst;
    } cases[] = {
        {0, {{0,0}}, {{0,0}}},
        {0x123456789abcdef0ULL,
            {{0x3333333333333333ULL,0x8888888888888888ULL}},
            {{0x5555555555555555ULL,0x9999999999999999ULL}}},
        {1, {{UINT64_MAX,UINT64_MAX}}, {{1,UINT64_MAX}}},
        {UINT64_MAX, {{UINT64_MAX,UINT64_MAX}},
            {{UINT64_MAX,UINT64_MAX}}},
        {0x1122334455667788ULL, {{UINT64_MAX,1}}, {{UINT64_MAX,1}}}
    };
    unsigned int i;
    for (i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
        const uint64_t got = minic_ipv6_checksum(
            cases[i].rest,&cases[i].src,&cases[i].dst);
        const uint64_t expected = reference(
            cases[i].rest,&cases[i].src,&cases[i].dst);
        if (got != expected) {
            fprintf(stderr,"IPv6 checksum case %u: got %llx expected %llx\n",
                i,(unsigned long long)got,(unsigned long long)expected);
            return 1;
        }
    }
    return 0;
}
