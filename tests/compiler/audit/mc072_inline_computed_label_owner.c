/* MC-072: Valid GNU C. A static label table belongs to one function body;
 * an integer-specialized inline clone must not leave its relocations without
 * a matching source-label symbol definition. Do not use always_inline here:
 * GCC rejects forcing a copy of a function with static &&label storage. */
static inline int dispatch_inline_local(int n) {
    static void *const jumps[] = {&&alpha, &&beta};
    if (n < 0 || n > 1) return 0;
    goto *jumps[n];
alpha:
    return 10;
beta:
    return 20;
}

/* Negative control: an unused inline owner must not publish either its
 * static table or aliases for its private labels into the module. */
static inline int unused_dispatch_inline_local(int n) {
    static void *const dead_jumps[]
        __attribute__((section(".data.mc072_dead_owner"))) = {&&dead_a, &&dead_b};
    goto *dead_jumps[n & 1];
dead_a:
    return 91;
dead_b:
    return 92;
}

int caller_one(void) { return dispatch_inline_local(0); }
int caller_two(void) { return dispatch_inline_local(1); }

#ifdef MC072_RUNTIME
int main(void) {
    return caller_one() == 10 && caller_two() == 20 ? 0 : 1;
}
#endif
