/* Linux 6.6.143 fs/udf/inode.c udf_current_aext checked-addition
 * contract: a constant sizeof size_t, le32_to_cpu's u32, and signed int. */
typedef unsigned int __u32;
int loong_linux_checked_add_i32_u32(int lhs, __u32 rhs, int *result) {
    return __builtin_add_overflow(lhs, rhs, result);
}
int loong_linux_checked_add_u32_i32(__u32 lhs, int rhs, int *result) {
    return __builtin_add_overflow(lhs, rhs, result);
}
int loong_linux_udf_parameter_like(unsigned long fixed_size,
                                   __u32 disk_length, int *result) {
    return __builtin_add_overflow(fixed_size, disk_length, result);
}
int loong_linux_udf_cast_constant_like(__u32 disk_length, int *result) {
    return __builtin_add_overflow((unsigned long)8, disk_length, result);
}
struct allocExtDesc {
    __u32 previousAllocExtLocation;
    __u32 lengthAllocDescs;
};
int loong_linux_udf_current_aext_overflow(__u32 disk_length, int *result) {
    return __builtin_add_overflow(sizeof(struct allocExtDesc),
                                  disk_length, result);
}
int loong_linux_udf_current_aext_local(__u32 disk_length) {
    int alen;
    if (__builtin_add_overflow(sizeof(struct allocExtDesc),
                               disk_length, &alen))
        return -1;
    return alen;
}

#ifdef MINIC_LOONG_UNSUPPORTED_DIFFERENT_SHAPE
int loong_linux_checked_sub_i32_u32(int lhs, __u32 rhs, int *result) {
    return __builtin_sub_overflow(lhs, rhs, result);
}
int loong_linux_checked_add_i32_u32_to_u16(int lhs, __u32 rhs,
                                           unsigned short *result) {
    return __builtin_add_overflow(lhs, rhs, result);
}
#endif

#ifdef MINIC_GNU_NATIVE_ORACLE
#include <limits.h>
#include <stdio.h>
int main(void) {
    const int signed_cases[] = { INT_MIN, -32768, -1, 0, 1, INT_MAX - 1, INT_MAX };
    const __u32 unsigned_cases[] = { 0U, 1U, 7U, INT_MAX, 0x80000000U, 0xffffffffU };
    unsigned long long state = 0x9e3779b97f4a7c15ULL;
    unsigned long long tested = 0ULL;
    for (unsigned int i = 0U; i < 10000U; ++i) {
        int a;
        __u32 b;
        if (i < sizeof(signed_cases) / sizeof(signed_cases[0]))
            a = signed_cases[i];
        else {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            a = (int)(unsigned int)state;
        }
        for (unsigned int j = 0U;
             j < sizeof(unsigned_cases) / sizeof(unsigned_cases[0]); ++j) {
            b = unsigned_cases[j];
            const long long sum = (long long)a + (long long)b;
            const int flag = sum < INT_MIN || sum > INT_MAX;
            const int low = (int)(unsigned int)sum;
            int lhs_out = 0, rhs_out = 0;
            if (loong_linux_checked_add_i32_u32(a, b, &lhs_out) != flag ||
                loong_linux_checked_add_u32_i32(b, a, &rhs_out) != flag ||
                lhs_out != low || rhs_out != low) {
                fprintf(stderr, "GNU i32/u32 overflow mismatch i=%u j=%u\n",
                        i, j);
                return 1;
            }
            ++tested;
        }
    }
    printf("MINIC_LOONG_UDF_GNU_NATIVE=PASS checked_pairs=%llu orientations=2\n",
           tested);
    return 0;
}
#endif
