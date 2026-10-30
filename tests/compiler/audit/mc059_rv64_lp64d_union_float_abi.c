/* MC-059: RISC-V LP64D unions are not flattened into FP registers.
 * Run each independent case with -DPROBE_{RETURN,PARAM,FLOAT32,CALL,CONTROL}.
 * GNU C/psABI classifies union float/double members using integer ABI. */
union UF64 { double d; unsigned long long u; };
union UF32 { float f; unsigned u; };
union UI64 { unsigned long long u; long s; };
#if defined(PROBE_RETURN)
union UF64 probe_return(union UF64 x) { return x; }
#elif defined(PROBE_PARAM)
unsigned long long probe_param(union UF64 x) { return x.u; }
#elif defined(PROBE_FLOAT32)
union UF32 probe_float32(union UF32 x) { return x; }
#elif defined(PROBE_CALL)
extern union UF64 probe_return(union UF64);
union UF64 probe_call(union UF64 x) { return probe_return(x); }
#elif defined(PROBE_CONTROL)
union UI64 probe_control(union UI64 x) { return x; }
#else
#error "Define exactly one PROBE_* mode"
#endif
