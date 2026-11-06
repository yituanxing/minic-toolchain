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

void mini_runtime_assert_survives(int runtime_value) {
    if ((int){runtime_value} != 0)
        __compiletime_assert_dynamic();
}

void mini_effectful_assert_survives(int *ptr) {
    if ((int){++*ptr} != 0)
        __compiletime_assert_effectful();
}
