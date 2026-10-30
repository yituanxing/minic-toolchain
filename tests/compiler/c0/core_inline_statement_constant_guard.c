/* Linux KVM: an always-inline helper requires a compile-time request number,
 * but the call argument is the value of a provably pure GNU ({ ... }) block.
 * An unreachable diagnostic branch inside do/while(0) is not a side effect. */
#define GUARDED_REQUEST(n) \
    ({ do { \
        extern void impossible_request_index(void) \
            __attribute__((error("request index outside compile-time range"))); \
        if ((unsigned int)(n) >= 8U) impossible_request_index(); \
    } while (0); (unsigned int)((n) + 8U); })

static inline __attribute__((always_inline)) unsigned int
checked_request(unsigned int req) {
    extern void impossible_dynamic_request(void)
        __attribute__((error("KVM request must be a compile-time constant")));
    if (!__builtin_constant_p(req) || (req & 1024U))
        impossible_dynamic_request();
    return req;
}

volatile unsigned int runtime_source = 7;
static int observed_calls;
static volatile int observed_condition_side_effects;

static inline __attribute__((always_inline)) int caller_parameter_constant(int v) {
    return __builtin_constant_p(v);
}

static unsigned int increment_and_return(void) {
    observed_calls++;
    return runtime_source;
}

int statement_constant_guard_positive(void) {
    unsigned int first = checked_request(GUARDED_REQUEST(0));
    unsigned int second = checked_request(GUARDED_REQUEST(3));
    unsigned int third = checked_request(GUARDED_REQUEST(6));
    return first == 8U && second == 11U && third == 14U;
}

/* A volatile read, call, or asm means the statement block must fail closed
 * for the compile-time-constant specialization proof. The runtime expression
 * is still evaluated once, retaining its side effects. */
int statement_constant_guard_negative_volatile(void) {
    return caller_parameter_constant(
               ({ unsigned int x = runtime_source; x; })) == 0;
}

int statement_constant_guard_negative_call(void) {
    int known;
    observed_calls = 0;
    known = caller_parameter_constant(({ increment_and_return(); }));
    return known == 0 && observed_calls == 1;
}

int statement_constant_guard_negative_asm(void) {
    int known = caller_parameter_constant(
        ({ __asm__ __volatile__("" ::: "memory"); runtime_source; }));
    return known == 0;
}

/* The guard has a constant false VALUE but a non-constant EFFECT. Folding
 * ({ do { if ((++counter, 0)) diagnostic(); } while (0); 4; }) to an ICE
 * would silently discard the increment. */
int statement_constant_guard_negative_side_effecting_false(void) {
    int known;
    observed_condition_side_effects = 0;
    known = caller_parameter_constant(
        ({ do {
            extern void impossible_side_effect_guard(void)
                __attribute__((error("unreachable but effectful condition")));
            if ((++observed_condition_side_effects, 0))
                impossible_side_effect_guard();
        } while (0); 4; }));
    /* __builtin_constant_p is optimization-level dependent; the required
     * observable property is that the volatile increment executes once. */
    return (known == 0 || known == 1) &&
           observed_condition_side_effects == 1;
}

int main(void) {
    return statement_constant_guard_positive() &&
           statement_constant_guard_negative_volatile() &&
           statement_constant_guard_negative_call() &&
           statement_constant_guard_negative_asm() &&
           statement_constant_guard_negative_side_effecting_false() ? 0 : 1;
}
