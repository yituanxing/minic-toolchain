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

int caller_one(void) { return dispatch_inline_local(0); }
int caller_two(void) { return dispatch_inline_local(1); }
