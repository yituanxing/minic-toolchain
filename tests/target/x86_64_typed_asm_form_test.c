#include "target/x86_64/asm_contract.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct FormCase {
    MinicX86MachineAsmForm id;
    uint32_t variant;
    const char *text;
} FormCase;

static bool admits(const FormCase *c) {
    return minic_x86_64_typed_machine_form_matches_template(
        (uint32_t)c->id, c->variant, c->text, strlen(c->text));
}

int main(void) {
    static const FormCase positive[] = {
        {MINIC_X86_MACHINE_ASM_ZSTD_CPUID, 0U, "cpuid"},
        {MINIC_X86_MACHINE_ASM_XSETBV, 0U, "xsetbv"},
        {MINIC_X86_MACHINE_ASM_MMIO_READ, 4U, "movl %1, %0"},
        {MINIC_X86_MACHINE_ASM_MMIO_WRITE, 4U, "movl %0, %1"},
        {MINIC_X86_MACHINE_ASM_ATOMIC_XCHG, 4U, "xchgl %0, %1"},
        {MINIC_X86_MACHINE_ASM_MONITOR_WAIT, 1U, ".byte 0x0f,0x01,0xc8;"},
        {MINIC_X86_MACHINE_ASM_SEGMENT_READ, 1U, "mov %%es, %0"},
        {MINIC_X86_MACHINE_ASM_FSGSBASE, 1U, "rdfsbase %0"},
        {MINIC_X86_MACHINE_ASM_X87_PROBE, 0U,
         "fninit ; fnstsw %0 ; fnstcw %1"}
    };
    size_t i;
    for (i = 0U; i < sizeof(positive)/sizeof(positive[0]); ++i)
        if (!admits(&positive[i])) {
            fprintf(stderr, "FAIL valid typed form %zu\n", i);
            return 1;
        }

    /* Standalone Core clients and future passes can forge opaque fields.
     * FormId, variant and source spelling must all match at the target gate;
     * even a valid source fingerprint cannot override an invalid triple. */
    for (i = 0U; i < sizeof(positive)/sizeof(positive[0]); ++i) {
        FormCase forged = positive[i];
        forged.variant ^= 0x100U;
        assert(!admits(&forged));
    }
    assert(!minic_x86_64_typed_machine_form_matches_template(
        MINIC_X86_MACHINE_ASM_ZSTD_CPUID, 0U, "xsetbv", 6U));
    assert(!minic_x86_64_typed_machine_form_matches_template(
        MINIC_X86_MACHINE_ASM_XSETBV, 0U, "cpuid", 5U));
    assert(!minic_x86_64_typed_machine_form_matches_template(
        MINIC_X86_MACHINE_ASM_MMIO_READ, 4U, "movl %0, %1", 11U));
    assert(!minic_x86_64_typed_machine_form_matches_template(
        MINIC_X86_MACHINE_ASM_MMIO_READ, 8U, "movl %1, %0", 11U));
    assert(!minic_x86_64_typed_machine_form_matches_template(
        MINIC_X86_MACHINE_ASM_ATOMIC_XCHG, 4U, "xchgb %b0, %1", 13U));
    assert(!minic_x86_64_typed_machine_form_matches_template(
        UINT32_MAX, 0U, "cpuid", 5U));
    assert(!minic_x86_64_typed_machine_form_matches_template(
        MINIC_X86_MACHINE_ASM_ZSTD_CPUID, 0U, NULL, 0U));
    puts("PASS x86 typed machine forms 9 admitted families, forged form/variant/template fail-closed");
    return 0;
}
