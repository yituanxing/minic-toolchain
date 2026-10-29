#include "target/x86_64/asm_contract.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool gate(MinicX86MachineAsmForm id, unsigned int variant,
                 const char *literal) {
    return minic_x86_64_typed_machine_form_matches_template(
        (uint32_t)id, variant, literal, strlen(literal));
}

#define CHECK_FAMILY(id, parser, literal, invalid) do {                 \
    const char *const source = literal;                                \
    unsigned int variant = (unsigned int)parser(source, strlen(source));\
    assert(variant != 0U);                                             \
    assert(gate(id, variant, source));                                  \
    assert(!gate(id, 0U, source));                                      \
    assert(!gate(id, variant + 1U, source));                            \
    assert(!gate(id, variant, invalid));                               \
} while (0)

int main(void) {
    static const char unlocked_cc[] =
        "incl %0\n\t/* output condition code e */";
    static const char locked_cc[] =
        ".pushsection .smp_locks,\"a\"\n.balign 4\n.long 671f - .\n"
        ".popsection\n671:\n\tlock; incl %0\n\t/* output condition code e */";
    const unsigned int cc_form = (unsigned int)minic_x86_64_atomic_cc_form(
        unlocked_cc, strlen(unlocked_cc));
    CHECK_FAMILY(MINIC_X86_MACHINE_ASM_PORT_IO,
        minic_x86_64_port_io_form, "outb %b0, %w1", "outb %b0, %w1; cli");
    CHECK_FAMILY(MINIC_X86_MACHINE_ASM_ATOMIC_RMW,
        minic_x86_64_atomic_rmw_form, "addl %1,%0", "addl %1,%0; cli");
    CHECK_FAMILY(MINIC_X86_MACHINE_ASM_RAW_CMPXCHG,
        minic_x86_64_raw_cmpxchg_form, "cmpxchgl %2,%1",
        "cmpxchgl %2,%1; cli");
    CHECK_FAMILY(MINIC_X86_MACHINE_ASM_TRYCAS,
        minic_x86_64_trycas_form,
        "cmpxchgl %[new], %[ptr]\n\t/* output condition code z */",
        "cmpxchgl %[new], %[ptr]; cli\n\t/* output condition code z */");
    CHECK_FAMILY(MINIC_X86_MACHINE_ASM_BITOPS,
        minic_x86_64_linux_bitops_form,
        "btq %2,%1\n\t/* output condition code c */",
        "btq %2,%1; cli\n\t/* output condition code c */");

    assert(cc_form != 0U);
    assert((unsigned int)minic_x86_64_atomic_cc_form(
        locked_cc, strlen(locked_cc)) == cc_form);
    assert(gate(MINIC_X86_MACHINE_ASM_ATOMIC_CC, cc_form, unlocked_cc));
    assert(gate(MINIC_X86_MACHINE_ASM_ATOMIC_CC,
        cc_form | MINIC_X86_ATOMIC_CC_LOCK_VARIANT, locked_cc));
    assert(!gate(MINIC_X86_MACHINE_ASM_ATOMIC_CC,
        cc_form | MINIC_X86_ATOMIC_CC_LOCK_VARIANT, unlocked_cc));
    assert(!gate(MINIC_X86_MACHINE_ASM_ATOMIC_CC, cc_form, locked_cc));
    assert(!gate(MINIC_X86_MACHINE_ASM_ATOMIC_CC, 0U, unlocked_cc));
    assert(!gate(MINIC_X86_MACHINE_ASM_ATOMIC_CC, cc_form,
        "incl %0; cli\n\t/* output condition code e */"));
    assert(!gate(MINIC_X86_MACHINE_ASM_TRYCAS, cc_form, unlocked_cc));
    assert(!gate(MINIC_X86_MACHINE_ASM_ATOMIC_CC,
        (unsigned int)minic_x86_64_port_io_form("outb %b0, %w1", 13U),
        "outb %b0, %w1"));
    puts("PASS six x86 typed family exact ID+variant+immutable template gates");
    return 0;
}
