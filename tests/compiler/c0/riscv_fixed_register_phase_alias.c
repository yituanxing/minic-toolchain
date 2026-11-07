/* Real Linux vDSO ecall register family: output a0 may overlap an input
 * a0 only because =r is written after the asm reads its inputs. */
#if defined(MINIC_BAD_EARLYCLOBBER)
long fixed_alias_bad_earlyclobber(long first, long second) {
    register long in asm("a0") = first;
    register long other asm("a1") = second;
    register long result asm("a0");
    asm volatile("add %0, %1, %2"
                 : "=&r"(result) : "r"(in), "r"(other) : "memory");
    return result;
}
#elif defined(MINIC_BAD_TWO_INPUTS)
long fixed_alias_bad_two_inputs(long first, long second) {
    register long in0 asm("a0") = first;
    register long in1 asm("a0") = second;
    register long result asm("a1");
    asm volatile("add %0, %1, %2"
                 : "=r"(result) : "r"(in0), "r"(in1) : "memory");
    return result;
}
#else
long fixed_alias_vdso_like(long first, long second) {
    register long in0 asm("a0") = first;
    register long in1 asm("a1") = second;
    register long result asm("a0");
    register long nr asm("a7") = 169;
    asm volatile("add %0, %1, %2 # nr=%3"
                 : "=r"(result) : "r"(in0), "r"(in1), "r"(nr)
                 : "memory");
    return result;
}
#endif
