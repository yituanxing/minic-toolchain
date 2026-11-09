/* Linux 6.6.143 i2c-pxa: of_device_id.data is a pointer, while the
 * destination enum is a 32-bit signed integer on LP64 AArch64. */
enum pxa_mode { PXA_MODE_INVALID = -1, PXA_MODE_DEFAULT = 0, PXA_MODE_FAST = 2 };

enum pxa_mode linux_pointer_to_enum(const void *data) {
    return (enum pxa_mode)data;
}

int linux_pointer_to_i32(const void *data) {
    return (int)data;
}

unsigned int linux_pointer_to_u32(const void *data) {
    return (unsigned int)data;
}

unsigned long linux_pointer_to_u64(const void *data) {
    return (unsigned long)data;
}

_Bool linux_pointer_to_bool(const void *data) {
    return (_Bool)data;
}

#ifdef MINI_POINTER_NARROW_NEGATIVE
/* C does not allow direct pointer-to-floating scalar casts. */
float invalid_pointer_float(const void *data) {
    return (float)data;
}
#endif

#ifdef MINI_POINTER_NARROW_NATIVE_TEST
#include <stdio.h>

int main(void) {
    static const unsigned long samples[] = {
        0UL, 1UL, 0x7fffffffUL, 0x80000000UL, 0xffffffffUL,
        0x100000001UL, 0xabcdef1234567890UL
    };
    unsigned i;
    if (sizeof(void *) != 8U || sizeof(int) != 4U)
        return 77;
    for (i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i) {
        const void *p = (const void *)samples[i];
        const int low_signed = (int)(unsigned int)samples[i];
        const unsigned int low_unsigned = (unsigned int)samples[i];
        if ((int)linux_pointer_to_enum(p) != low_signed ||
            linux_pointer_to_i32(p) != low_signed ||
            linux_pointer_to_u32(p) != low_unsigned ||
            linux_pointer_to_u64(p) != samples[i] ||
            linux_pointer_to_bool(p) != (samples[i] != 0UL)) {
            fprintf(stderr, "pointer narrow mismatch at sample %u\n", i);
            return 1;
        }
    }
    return 0;
}
#endif
