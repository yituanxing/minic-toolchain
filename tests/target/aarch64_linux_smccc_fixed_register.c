/* Real GNU AArch64 SMCCC ABI: local rN registers are operand bindings,
 * not global register reservations. The generated instructions must use
 * x0/x1 for inputs and x0..x3 for the four result registers. */
struct smccc_four { unsigned long a0, a1, a2, a3; };

void minic_test_smccc_hvc(unsigned long call, unsigned int arg,
                          struct smccc_four *result) {
    register unsigned long r0 asm("r0");
    register unsigned long r1 asm("r1");
    register unsigned long r2 asm("r2");
    register unsigned long r3 asm("r3");
    register unsigned long arg0 asm("r0") = call;
    register unsigned int arg1 asm("r1") = arg;
    asm volatile("hvc\t#0\n" : "=r"(r0), "=r"(r1), "=r"(r2), "=r"(r3)
                 : "r"(arg0), "r"(arg1) : "memory");
    result->a0 = r0;
    result->a1 = r1;
    result->a2 = r2;
    result->a3 = r3;
}

void minic_test_smccc_smc(unsigned long call, struct smccc_four *result) {
    register unsigned long r0 asm("r0");
    register unsigned long r1 asm("r1");
    register unsigned long r2 asm("r2");
    register unsigned long r3 asm("r3");
    register unsigned long arg0 asm("r0") = call;
    asm volatile("smc\t#0\n" : "=r"(r0), "=r"(r1), "=r"(r2), "=r"(r3)
                 : "r"(arg0) : "memory");
    result->a0 = r0;
    result->a1 = r1;
    result->a2 = r2;
    result->a3 = r3;
}
