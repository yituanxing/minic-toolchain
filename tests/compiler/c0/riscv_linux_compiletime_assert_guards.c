/* Linux 6.6 jump_label.c and asm-generic/rwonce.h compile-time guards.
 * The calls are invalid if emitted into GNU objects; valid constants must
 * remove them while preserving unsupported dynamic assertions. */
typedef struct {
    int counter;
} mini_atomic_t;

extern void __compiletime_assert_304(void)
    __attribute__((error("BUILD_BUG_ON atomic zero initializer")));
extern void __compiletime_assert_305(void)
    __attribute__((error("BUILD_BUG_ON atomic one initializer")));
extern void __compiletime_assert_329(void)
    __attribute__((error("unsupported READ_ONCE integer access width")));
extern void __compiletime_assert_dynamic(void)
    __attribute__((error("dynamic assertion must not be dropped")));
extern void __compiletime_assert_effectful(void)
    __attribute__((error("effectful initializer must not be elided")));
extern void __compiletime_assert_minmax_signedness(void)
    __attribute__((error("matching int signedness must be proved")));
extern void __compiletime_assert_auto_type_qualifiers(void)
    __attribute__((error("GNU __auto_type dropped the wrong qualifiers")));
extern void __compiletime_assert_minmax_dynamic(void)
    __attribute__((error("genuine signedness mismatch must survive")));

void mini_jump_label_init(void) {
    if (!((int){0} == 0))
        __compiletime_assert_304();
    if (!((int){1} == 1))
        __compiletime_assert_305();
}

struct mini_pid_namespace {
    int memfd_noexec_scope;
    struct mini_pid_namespace *parent;
};

int mini_pidns_memfd_noexec_scope(struct mini_pid_namespace *ns) {
    if (!(sizeof(ns->memfd_noexec_scope) == sizeof(char) ||
          sizeof(ns->memfd_noexec_scope) == sizeof(short) ||
          sizeof(ns->memfd_noexec_scope) == sizeof(int) ||
          sizeof(ns->memfd_noexec_scope) == sizeof(long)))
        __compiletime_assert_329();
    return *(const volatile int *)&ns->memfd_noexec_scope;
}

/* Full Linux 6.6 compiler_types.h / asm-generic/rwonce.h statement-expression
 * placement: the compile-time guard is nested in READ_ONCE inside a for loop.
 * It must not depend on optimizations of the surrounding runtime predicate. */
#define MINI_READ_ONCE(x) ({ \
    do { \
        if (!(sizeof(x) == sizeof(char) || sizeof(x) == sizeof(short) || \
              sizeof(x) == sizeof(int) || sizeof(x) == sizeof(long) || \
              sizeof(x) == sizeof(long long))) \
            __compiletime_assert_329(); \
    } while (0); \
    (*(const volatile __typeof__(x) *)&(x)); \
})

int mini_pidns_memfd_noexec_scope_loop(struct mini_pid_namespace *ns) {
    int scope = 0;
    for (; ns; ns = ns->parent) {
        int value = MINI_READ_ONCE(ns->memfd_noexec_scope);
        scope = scope > value ? scope : value;
    }
    return scope;
}

/* Linux minmax.h uses __auto_type on READ_ONCE's const-volatile lvalue,
 * then compares each local's typeof-derived signedness mask.  GNU C drops
 * the initializer's top-level cv qualifiers on __auto_type, so the mask is
 * a pure type fact even though its initializer must still perform a volatile
 * load.  This is the exact class found in seven Linux 6.6 source owners. */
#define MINI_SIGN_USE(v) \
    ((((typeof(v))(-1)) < (typeof(v))1) \
        ? (2 + (__builtin_constant_p((long long)(v) >= 0) && \
                ((long long)(v) >= 0))) \
        : (1 + 2 * (sizeof(v) < 4)))
#define MINI_MAX_SIGNED(x, y) ({ \
    __auto_type __x = (x); \
    __auto_type __y = (y); \
    do { \
        if (!(!(!((MINI_SIGN_USE(__x)) & (MINI_SIGN_USE(__y)))))) \
            __compiletime_assert_minmax_signedness(); \
    } while (0); \
    (__x > __y ? __x : __y); \
})

int mini_pidns_read_once_max(struct mini_pid_namespace *ns) {
    int scope = 0;
    for (; ns; ns = ns->parent)
        scope = MINI_MAX_SIGNED(scope, MINI_READ_ONCE(ns->memfd_noexec_scope));
    return scope;
}

int mini_pidns_volatile_max(const volatile int *address, int other) {
    return MINI_MAX_SIGNED(other, *address);
}

int mini_pidns_auto_type_qualifiers(const volatile int *address,
                                    const int *const pointer) {
    __auto_type number = *address;
    __auto_type const_number = *pointer;
    __auto_type deduced_pointer = pointer;
    if (!__builtin_types_compatible_p(typeof(number), int) ||
        !__builtin_types_compatible_p(typeof(const_number), int) ||
        !__builtin_types_compatible_p(typeof(deduced_pointer), const int *))
        __compiletime_assert_auto_type_qualifiers();
    return number + const_number + *deduced_pointer;
}

int mini_pidns_mismatched_signedness(int signed_value, unsigned long unsigned_value) {
    __auto_type x = signed_value;
    __auto_type y = unsigned_value;
    if (!(MINI_SIGN_USE(x) & MINI_SIGN_USE(y)))
        __compiletime_assert_minmax_dynamic();
    return x;
}

void mini_runtime_assert_survives(int runtime_value) {
    if ((int){runtime_value} != 0)
        __compiletime_assert_dynamic();
}

void mini_effectful_assert_survives(int *ptr) {
    if ((int){++*ptr} != 0)
        __compiletime_assert_effectful();
}
