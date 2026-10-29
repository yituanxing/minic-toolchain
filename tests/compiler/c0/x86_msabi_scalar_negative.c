#if defined(NEG_VARARGS)
typedef long callback_t(long, ...);
struct T { callback_t __attribute__((ms_abi)) *cb; };
long rejected(struct T *t) { return t->cb(1, 2); }
#elif defined(NEG_AGGREGATE)
struct Pair { long first; long second; };
typedef long callback_t(struct Pair);
struct T { callback_t __attribute__((ms_abi)) *cb; };
long rejected(struct T *t) {
    struct Pair p = {1, 2};
    return t->cb(p);
}
#elif defined(NEG_INT128)
typedef long callback_t(__int128);
struct T { callback_t __attribute__((ms_abi)) *cb; };
long rejected(struct T *t) { return t->cb(1); }
#elif defined(NEG_LONG_DOUBLE)
typedef long double callback_t(long);
struct T { callback_t __attribute__((ms_abi)) *cb; };
long double rejected(struct T *t) { return t->cb(7); }
#elif defined(NEG_DEFINITION)
__attribute__((ms_abi)) long rejected(long value) { return value + 3; }
#endif
