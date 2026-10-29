#include "target/x86_64/asm_contract.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool gate(MinicX86MachineAsmForm id, unsigned int variant,
                 const char *template_text) {
    return minic_x86_64_typed_machine_form_matches_template(
        (uint32_t)id, variant, template_text, strlen(template_text));
}

int main(void) {
    static const char unwind[] =
        "lea (%%rip), %0\n\tmov %%rsp, %1\n\tmov %%rbp, %2";
    const MinicX86MachineAsmForm mxcsr = MINIC_X86_MACHINE_ASM_MXCSR;
    const MinicX86MachineAsmForm orc = MINIC_X86_MACHINE_ASM_UNWIND_CONTEXT;
    assert(gate(mxcsr, 1U, "ldmxcsr %0"));
    assert(gate(mxcsr, 2U, "stmxcsr %[val]"));
    assert(gate(orc, 0U, unwind));
    assert(!gate(mxcsr, 2U, "ldmxcsr %0"));
    assert(!gate(mxcsr, 1U, "stmxcsr %0"));
    assert(!gate(mxcsr, 0U, "ldmxcsr %0"));
    assert(!gate(mxcsr, 3U, "ldmxcsr %0"));
    assert(!gate(mxcsr, 1U, "ldmxcsr %0; cli"));
    assert(!gate(orc, 1U, unwind));
    assert(!gate(orc, 0U, "lea (%%rip), %0; cli"));
    assert(!gate(orc, 0U, "ldmxcsr %0"));
    assert(!gate(mxcsr, 2U, unwind));
    puts("PASS x86 MXCSR/ORC typed FormId variant + immutable-template gate");
    return 0;
}
