_Static_assert(__builtin_clzll(1ULL) == 63, "clzll one");
_Static_assert(__builtin_clzll(16ULL) == 59, "clzll sixteen");
_Static_assert(__builtin_clzll(0x8000000000000000ULL) == 0, "clzll top bit");

struct MutexLike {
    long state;
};

struct LinuxShape {
    struct MutexLike open_file_mutex[
        1 << (2 * (__builtin_constant_p(64 < 32 ? 64 : 32)
                       ? ((64 < 32 ? 64 : 32) < 2
                              ? 0
                              : 63 - __builtin_clzll(64 < 32 ? 64 : 32))
                       : 0))];
};

_Static_assert(sizeof(((struct LinuxShape *)0)->open_file_mutex) / sizeof(struct MutexLike) == 1024,
               "Linux kernfs lock array bound");

static int runtime_clzll_ull(unsigned long long value) {
    return __builtin_clzll(value);
}

static int runtime_clzll_uint(unsigned int value) {
    return __builtin_clzll(value);
}

static int runtime_clzll_int(int value) {
    return __builtin_clzll(value);
}

int main(void) {
    return runtime_clzll_ull(1ULL) == 63 &&
                   runtime_clzll_ull(16ULL) == 59 &&
                   runtime_clzll_ull(0x8000000000000000ULL) == 0 &&
                   runtime_clzll_ull(0x00f0000000000000ULL) == 8 &&
                   runtime_clzll_uint(0x80000000U) == 32 &&
                   runtime_clzll_int(-1) == 0
               ? 0
               : 1;
}
