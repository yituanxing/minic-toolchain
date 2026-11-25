#include <stdint.h>
#include <stdio.h>

int minic_add_predicate_i32(int, int);
int minic_add_predicate_i8(int, int);
int minic_add_predicate_u32(unsigned int, unsigned int);
int minic_sub_predicate_i64(long long, long long);
int minic_mul_predicate_u32(unsigned int, unsigned int);
extern int minic_overflow_predicate_side_effect;

int main(void) {
    uint64_t seed = UINT64_C(0x5bd1e995bd1e995);
    unsigned int i;
    for (i = 0U; i < 10000U; ++i) {
        const int32_t a = (int32_t)seed;
        const int32_t b = (int32_t)(seed >> 16U);
        const uint32_t u = (uint32_t)seed;
        const uint32_t v = (uint32_t)(seed >> 32U);
        const int64_t wide = (int64_t)seed;
        const int64_t other = (int64_t)(seed * UINT64_C(0x9e3779b97f4a7c15));
        if (minic_add_predicate_i32(a, b) !=
                __builtin_add_overflow_p(a, b, (int)0) ||
            minic_add_predicate_i8(a, b) !=
                __builtin_add_overflow_p(a, b, (signed char)0) ||
            minic_add_predicate_u32(u, v) !=
                __builtin_add_overflow_p(u, v, (unsigned int)0) ||
            minic_sub_predicate_i64(wide, other) !=
                __builtin_sub_overflow_p(wide, other, (long long)0) ||
            minic_mul_predicate_u32(u, v) !=
                __builtin_mul_overflow_p(u, v, (unsigned int)0) ||
            minic_overflow_predicate_side_effect != 0) {
            (void)fprintf(stderr, "overflow predicate mismatch iteration %u\n", i);
            return 1;
        }
        seed = seed * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    }
    return 0;
}
