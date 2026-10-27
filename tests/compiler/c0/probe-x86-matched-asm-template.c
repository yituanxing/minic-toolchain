#include "target/x86_64/asm_contract.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static int probe(const char *s, size_t outputs, bool expected) {
    bool got = minic_x86_64_asm_matched_template(s, strlen(s), outputs);
    if (got == expected) return 0;
    fprintf(stderr, "X86_MATCHED_TEMPLATE_CONTRACT_FAIL expected=%d actual=%d source=%s\n",
            (int)expected, (int)got, s);
    return 1;
}

int main(void) {
    int failed = 0;
    failed += probe("movq %0, %1\n\tadcq %2,%3", 4, true);
    failed += probe("movq (%0), %3\n\tadcq 1*8(%1), %3", 4, true);
    failed += probe("movq %3, 3*8(%2)", 4, true);
    failed += probe("addq $1, (%0)\n\tsbbq $0, 2*8(%0)", 1, true);
    failed += probe("subq $1, (%0)", 1, true);
    failed += probe("movq (%0), %3; syscall", 4, false);
    failed += probe("movq 4*8(%0), %3", 4, false);
    failed += probe("movq 1*16(%0), %3", 4, false);
    failed += probe("movq $2, (%0)", 1, false);
    failed += probe("movq $1, %gs:(%0)", 1, false);
    failed += probe("cmc\n\tsbbq %0,%1", 2, true);
    failed += probe("addq %0,%1;movq %1,%0", 2, true);
    failed += probe("cmc", 4, false);
    failed += probe("movq %0,%1;syscall", 2, false);
    failed += probe("movq %0,%1;call attacker", 2, false);
    failed += probe("movq %0,%1 # injected", 2, false);
    failed += probe("movq %0,%1\n\t.ret", 2, false);
    failed += probe("movq %0,%1, %0", 2, false);
    failed += probe("movq %%rax,%1", 2, false);
    failed += probe("movq %0,8(%1)", 2, false);
    failed += probe("movq %0,%4", 4, false);
    failed += probe("movqX %0,%1", 2, false);
    failed += probe("movq %0,%1\n\tud2", 2, false);
    failed += probe("movq %0,%1\n\taddq %1,%0", 1, false);
    if (failed != 0) return 1;
    puts("MINIC_X86_MATCHED_GNU_TEMPLATE_SECURITY=PASS cases=24");
    return 0;
}
