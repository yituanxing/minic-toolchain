/* Oracle: all expected values are unambiguous and observable at runtime. */
#ifndef MC038_FREESTANDING
#include <stdio.h>
#endif

extern int mc038_simple(void);
extern int mc038_deep(void);
extern int mc038_union(void);
extern int mc038_offset(void);
extern int mc038_named(void);
extern int mc038_named_mid(void);
extern int mc038_deep_multi(void);
extern int mc038_named_multi(void);
extern int mc038_dotted_after_positional(void);
extern int mc038_preserve_designated(void);
extern int mc038_preserve_outer(void);
extern int mc038_multiple(void);
extern int mc038_explicit(void);

int main(void) {
    unsigned int failures = 0U;
    if (mc038_simple() != 23) failures |= 1U;
    /* The anonymous outer struct still has `hidden` after promoted `.b`. */
    if (mc038_deep() != 320) failures |= 2U;
    if (mc038_union() != 23) failures |= 4U;
    if (mc038_offset() != 23) failures |= 8U;
    if (mc038_named() != 23) failures |= 16U;
    if (mc038_multiple() != 234) failures |= 32U;
    if (mc038_explicit() != 23) failures |= 64U;
    if (mc038_named_mid() != 23) failures |= 128U;
    if (mc038_deep_multi() != 324) failures |= 256U;
    if (mc038_named_multi() != 234) failures |= 512U;
    if (mc038_dotted_after_positional() != 123) failures |= 1024U;
    if (mc038_preserve_designated() != 2370) failures |= 2048U;
    if (mc038_preserve_outer() != 237) failures |= 4096U;
#ifndef MC038_FREESTANDING
    (void)printf("mc038_failure_mask=0x%x\n", failures);
#endif
    return failures != 0U;
}
