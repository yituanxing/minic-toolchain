/* Keep the Function arena exactly at its initial 16-slot capacity when
 * va_start parses the nested LoongArch builtin. The builtin synthesizes the
 * seventeenth Function and can realloc the arena while the enclosing parser
 * is still checking its final named parameter. Run with an ASan MiniC build
 * to catch dangling MinicFunction* reads, not merely source acceptance. */
typedef __builtin_va_list minic_va_list;

extern int arena_f00(void);
extern int arena_f01(void);
extern int arena_f02(void);
extern int arena_f03(void);
extern int arena_f04(void);
extern int arena_f05(void);
extern int arena_f06(void);
extern int arena_f07(void);
extern int arena_f08(void);
extern int arena_f09(void);
extern int arena_f10(void);
extern int arena_f11(void);
extern int arena_f12(void);
extern int arena_f13(void);
extern int arena_f14(void);

unsigned long loong_va_start_nested_builtin(unsigned long tag, ...) {
    minic_va_list ap;
    __builtin_va_start(*(&ap + (0U * __builtin_loongarch_cpucfg(0U))), tag);
    unsigned long result = __builtin_va_arg(ap, unsigned long);
    __builtin_va_end(ap);
    return result;
}
