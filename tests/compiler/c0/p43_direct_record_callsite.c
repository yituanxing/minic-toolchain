/* A single source FunctionBody supplies three independent specialization
 * dimensions to Record-returning direct CALLs.  The leaf CALL is a shared
 * source ExpressionId, even when its enclosing middle is cloned for callers
 * in different sections.  The caller owns the executable CALL identity. */
struct P43Record {
    long value;
};

static int p43_first = 11;
static int p43_second = 22;
static int p43_third = 33;

static __attribute__((always_inline)) inline struct P43Record
p43_record_leaf(int *pointer, int delta, _Bool select_first) {
    struct P43Record result;
    result.value = (long)*pointer +
        (__builtin_constant_p(delta) ? (long)delta : 900L) +
        (select_first ? 100L : 200L);
    return result;
}

static __attribute__((always_inline)) inline struct P43Record
p43_record_middle(int *pointer, int delta, _Bool select_first) {
    return p43_record_leaf(pointer, delta, select_first);
}

__attribute__((section(".head.text")))
long p43_record_head(void) {
    struct P43Record result = p43_record_middle(&p43_first, 3, 1);
    return result.value;
}

__attribute__((section(".init.text")))
long p43_record_init(void) {
    struct P43Record result = p43_record_middle(&p43_second, 5, 0);
    return result.value;
}

__attribute__((section(".head.text")))
long p43_record_head_other(void) {
    struct P43Record result = p43_record_middle(&p43_third, 7, 1);
    return result.value;
}

__attribute__((section(".head.text")))
long p43_record_head_reuse(void) {
    struct P43Record result = p43_record_middle(&p43_first, 3, 1);
    return result.value;
}

/* Runtime values must not inherit an unrelated caller's entry facts. */
static long p43_record_dynamic(int *pointer, int delta, int flag) {
    struct P43Record result = p43_record_middle(pointer, delta, (_Bool)flag);
    return result.value;
}

int main(void) {
    volatile int dynamic_delta = 5;
    volatile int dynamic_flag = 1;
    if (p43_record_head() != 114L) return 11;
    if (p43_record_init() != 227L) return 12;
    if (p43_record_head_other() != 140L) return 13;
    if (p43_record_head_reuse() != 114L) return 14;
    if (p43_record_dynamic(&p43_third, dynamic_delta, dynamic_flag) != 1033L)
        return 15;
    return 0;
}
