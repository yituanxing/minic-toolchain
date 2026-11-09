/* GNU constructor semantics: two distinct linkages, no implicit libc CRT. */
static volatile unsigned int constructed;

__attribute__((constructor))
static void ctor_private(void) {
    constructed |= 1U;
}

__attribute__((constructor))
void ctor_external(void) {
    constructed |= 2U;
}

static void should_not_run(void) {
    constructed |= 8U;
}

int main(void) {
    if (constructed != 3U) {
        return 17;
    }
    /* The unmarked function is reachable in the object but must not
     * appear in .init_array. Prevent a dead-code-only negative check. */
    if (constructed == 0U) {
        should_not_run();
    }
    return constructed == 3U ? 0 : 18;
}
