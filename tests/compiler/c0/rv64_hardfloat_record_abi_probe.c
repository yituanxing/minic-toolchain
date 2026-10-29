#if defined(PROBE_FLOAT) || (!defined(PROBE_DOUBLE) && !defined(PROBE_MIXED))
struct OneFloat { float f; };
float receive_one_float(struct OneFloat value) { return value.f; }
#if !defined(PROBE_CALLEE_ONLY)
float call_one_float(struct OneFloat value) {
    return receive_one_float(value);
}
#endif
#endif
#if defined(PROBE_DOUBLE) || (!defined(PROBE_FLOAT) && !defined(PROBE_MIXED))
struct OneDouble { double d; };
double receive_one_double(struct OneDouble value) { return value.d; }
#if !defined(PROBE_CALLEE_ONLY)
double call_one_double(struct OneDouble value) {
    return receive_one_double(value);
}
#endif
#endif
#if defined(PROBE_MIXED) || (!defined(PROBE_FLOAT) && !defined(PROBE_DOUBLE))
struct FloatInt { float f; int i; };
double receive_float_int(struct FloatInt value) {
    return (double)value.i + (double)value.f;
}
#endif
