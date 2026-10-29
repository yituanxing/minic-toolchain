#if defined(NEG_DEF_VARIADIC)
__attribute__((ms_abi)) long rejected(long value, ...) { return value; }
#elif defined(NEG_DEF_AGGREGATE)
struct Pair { long first; long second; };
__attribute__((ms_abi)) struct Pair rejected(struct Pair value) { return value; }
#elif defined(NEG_DEF_INT128)
__attribute__((ms_abi)) __int128 rejected(__int128 value) { return value; }
#elif defined(NEG_DEF_LONG_DOUBLE)
__attribute__((ms_abi)) long double rejected(long double value) { return value; }
#endif
