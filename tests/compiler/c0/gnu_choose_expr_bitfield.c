static int linux_build_bug_shape(unsigned long offset, unsigned long size) {
    return (int)sizeof(struct {
        int : (-!!(__builtin_choose_expr(
            (sizeof(int) ==
             sizeof(*(8 ? ((void *)((long)((offset) > (size - 1)) * 0l)) : (int *)8))),
            (offset) > (size - 1),
            0)));
        int payload;
    });
}

_Static_assert(sizeof(void) == 1, "GNU sizeof(void) must remain byte-sized");
_Static_assert((~0U) == 0xffffffffU, "typed consteval keeps unsigned-int width");
_Static_assert((unsigned long long)(~0U) == 0x00000000ffffffffULL,
               "integer conversion must preserve source width before widening");

int main(void) {
    return linux_build_bug_shape(2UL, 1UL) > 0 ? 0 : 1;
}
