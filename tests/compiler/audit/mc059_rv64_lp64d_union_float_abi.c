/* MC-059: RISC-V LP64D unions are not flattened into FP registers.
 * Run independent cases with -DPROBE_*.
 * GNU C/psABI classifies union float/double members using integer ABI. */
union UF64 { double d; unsigned long long u; };
union UF32 { float f; unsigned u; };
union UI64 { unsigned long long u; long s; };
union UOnlyFP { double d; };
struct NestedUnion { unsigned long long tag; union UF64 data; };
struct UnionArray { union UF64 items[2]; };
struct TrueFPStruct { double d; };
#if defined(PROBE_RETURN)
union UF64 probe_return(union UF64 x) { return x; }
#elif defined(PROBE_PARAM)
unsigned long long probe_param(union UF64 x) { return x.u; }
#elif defined(PROBE_FLOAT32)
union UF32 probe_float32(union UF32 x) { return x; }
#elif defined(PROBE_CALL)
extern union UF64 probe_return(union UF64);
union UF64 probe_call(union UF64 x) { return probe_return(x); }
#elif defined(PROBE_NESTED)
struct NestedUnion probe_nested(struct NestedUnion x) { return x; }
#elif defined(PROBE_ARRAY)
struct UnionArray probe_array(struct UnionArray x) { return x; }
#elif defined(PROBE_FP_ONLY_UNION)
union UOnlyFP probe_only_fp(union UOnlyFP x) { return x; }
#elif defined(PROBE_VARIADIC)
extern unsigned long long probe_variadic(int, ...);
unsigned long long probe_variadic_caller(union UF64 x) {
    return probe_variadic(1, x);
}
#elif defined(PROBE_CONTROL)
union UI64 probe_control(union UI64 x) { return x; }
#elif defined(PROBE_SCALAR)
double probe_scalar(double x) { return x + 2.0; }
#elif defined(PROBE_TRUE_FP_STRUCT)
struct TrueFPStruct probe_true_fp_struct(struct TrueFPStruct x) { return x; }
#else
#error "Define exactly one PROBE_* mode"
#endif
