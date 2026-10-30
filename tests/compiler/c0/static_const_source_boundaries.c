/* A static wrapper can be specialized only when its original C identity is
 * not required by address-taking, a dynamic call, recursion, or source labels.
 * The helper's extern call is defined by the GNU owner during runtime diff. */
extern int external_guard_owner(int);
static __attribute__((always_inline)) inline int guarded_inline(int value) {
    if (value > 5)
        return external_guard_owner(value);
    return value + 1;
}

static int address_guard(int x) { return guarded_inline(x); }
static int (*volatile escaped_address_guard)(int) = address_guard;

static int mixed_guard(int x) { return guarded_inline(x) + 1; }
static int dynamic_mixed_guard(int x) { return mixed_guard(x); }

static int recursive_guard(int x) {
    if (x > 1)
        return recursive_guard(x - 1) + 1;
    return guarded_inline(x);
}

static int asm_guard(int x) {
    __asm__ __volatile__("nop" : : "i"(31));
    return guarded_inline(x);
}

static int label_guard(int x) {
    static void *const targets[] = {&&original_source_label};
    goto *targets[0];
original_source_label:
    return guarded_inline(x);
}

int main(void) {
    int total = 0;
    total += address_guard(3);
    total += escaped_address_guard(7);
    total += mixed_guard(2);
    total += dynamic_mixed_guard(6);
    total += recursive_guard(3);
    total += asm_guard(1);
    total += label_guard(2);
    return total == 44 ? 42 : 1;
}
