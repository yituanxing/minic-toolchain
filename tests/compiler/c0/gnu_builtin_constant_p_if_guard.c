/* Linux 6.6 minmax.h-style signedness guards.  __builtin_constant_p must
 * remain a pure (unevaluated) query when nested in an if predicate; its
 * operand may read a known __auto_type local or contain runtime operations. */
typedef unsigned int u32;

#define statically_true(x) (__builtin_constant_p(x) && (x))
#define is_signed_type(type) (((type)(-1)) < (type)1)
#define __is_nonneg(x) statically_true((long long)(x) >= 0)
#define __sign_use(x) (is_signed_type(typeof(x)) ? (2 + __is_nonneg(x)) : \
                       (1 + 2 * (sizeof(x) < 4)))
#define __types_ok(x, y) (__sign_use(x) & __sign_use(y))
#define __types_ok3(x, y, z) (__sign_use(x) & __sign_use(y) & __sign_use(z))

extern void __min_compiletime_assert(void)
    __attribute__((error("min signedness mismatch")));
extern void __clamp_compiletime_assert(void)
    __attribute__((error("clamp signedness mismatch")));

u32 min_guard(u32 value) {
    __auto_type x = value;
    __auto_type y = 16384;
    if (!__types_ok(x, y))
        __min_compiletime_assert();
    return x < y ? x : y;
}

unsigned long clamp_guard(unsigned long value) {
    __auto_type x = value;
    __auto_type lo = 0;
    __auto_type hi = 0xffff / 177;
    if (!__types_ok3(x, lo, hi))
        __clamp_compiletime_assert();
    return x >= hi ? hi : x <= lo ? lo : x;
}

int known_local_guard(void) {
    __auto_type lo = 1;
    if (!__builtin_constant_p((long long)lo >= 0))
        return -1;
    if (!(__builtin_constant_p((long long)lo >= 0) &&
          ((long long)lo >= 0)))
        return -2;
    return 1;
}

/* The outer condition is not constant, but facts about the auto local
 * dominate both of its runtime paths.  Lowering the then body first must
 * not destroy those facts while lowering the original predicate. */
int runtime_ternary_builtin_guard(int condition) {
    __auto_type lo = 1;
    if (condition ? !__builtin_constant_p((long long)lo >= 0) : 0)
        return -12;
    return 12;
}

int runtime_and_builtin_guard(int condition) {
    __auto_type lo = 1;
    if (condition && !__builtin_constant_p((long long)lo >= 0))
        return -11;
    return 11;
}

/* Both expressions are evaluated zero times.  In particular, the purity
 * decision cannot look through the builtin and classify ++*address as a
 * runtime side effect of the enclosing condition. */
int side_effect_guard(int *address) {
    if (__builtin_constant_p(++*address))
        return -1;
    return *address;
}

int volatile_guard(volatile int *address) {
    if (__builtin_constant_p(*address))
        return -1;
    return *address;
}

/* Keep an actually reachable runtime call, verifying that the conditional
 * elimination is restricted to proven constant conditions. */
extern void ordinary_runtime_call(void);
int runtime_guard(int value) {
    if (value == 73) {
        ordinary_runtime_call();
        return 17;
    }
    return 8;
}

int side_effect_runtime_guard(int *address, int condition) {
    if (condition && ++*address)
        return *address;
    return *address + 5;
}
