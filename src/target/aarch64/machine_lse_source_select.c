#include "target/aarch64/machine_lse_source_select.h"
#include "target/aarch64/asm_contract_internal.h"
#include "target/common/machine_source_asm_template.h"

#include <string.h>

/* Exact source and normalized Core grammar has a single ISA owner.  Keep
 * these pure recognizers with the typed family, never in a FILE* emitter. */
bool aarch64_lse_form(const char *text, size_t length,
    bool numeric, Aarch64LseForm *result) {
    static const struct { const char *store, *fetch; } operations[] = {
        {"stclr", "ldclr"}, {"stset", "ldset"},
        {"steor", "ldeor"}, {"stadd", "ldadd"}
    };
    static const char *const mode_suffix[] = {"", "a", "l", "al"};
    static const size_t cas_widths[] = {1U, 2U, 4U, 8U};
    static const char *const cas_suffix[] = {"b", "h", "", ""};
    char compact[160], expected[160];
    size_t i, n = 0U, operation, mode, width;
    if (!text || !length || length >= sizeof(compact) || !result)
        return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (width = 4U; width <= 8U; width += 4U) {
        const char *input = width == 8U ? "%" : "%w";
        for (operation = 0U;
             operation < sizeof(operations) / sizeof(operations[0]);
             ++operation) {
            const char *names[2] = {
                operations[operation].store, operations[operation].fetch
            };
            unsigned int kind;
            for (kind = 0U; kind < 2U; ++kind) {
                for (mode = 0U; mode < (kind ? 4U : 1U); ++mode) {
                    const char *mnemonic = names[kind];
                    const int written = snprintf(expected, sizeof(expected),
                        ".arch_extensionlse%s%s%s%s,%s%s",
                        mnemonic, kind ? mode_suffix[mode] : "",
                        input, numeric ? (kind ? "2" : "1") : "[i]",
                        kind ? input : "",
                        kind ? (numeric ? "1,%0" : "[old],%[v]") :
                               (numeric ? "%0" : "%[v]"));
                    if (written > 0 && (size_t)written < sizeof(expected) &&
                        strcmp(compact, expected) == 0) {
                        result->kind = kind ? AARCH64_LSE_FETCH :
                            AARCH64_LSE_STORE;
                        result->mnemonic = mnemonic;
                        result->width = width;
                        result->mode = (unsigned int)mode;
                        return true;
                    }
                }
            }
        }
    }
    for (width = 0U; width < 4U; ++width) {
        const char *reg = cas_widths[width] == 8U ? "%x" : "%w";
        for (mode = 0U; mode < 4U; ++mode) {
            const int written = snprintf(expected, sizeof(expected),
                ".arch_extensionlsecas%s%s%s%s,%s%s,%s",
                mode_suffix[mode], cas_suffix[width], reg,
                numeric ? "1" : "[old]", reg,
                numeric ? "2" : "[new]", numeric ? "%0" : "%[v]");
            if (written > 0 && (size_t)written < sizeof(expected) &&
                strcmp(compact, expected) == 0) {
                result->kind = AARCH64_LSE_CAS;
                result->mnemonic = "cas";
                result->width = cas_widths[width];
                result->mode = (unsigned int)mode;
                return true;
            }
        }
    }
    return false;
}

unsigned int aarch64_linux_lse_casp128_form(
    const char *text, size_t length) {
    static const char *const named[] = {
        ".arch_extensionlsecasp%[old1],%[old2],%[new1],%[new2],%[v]",
        ".arch_extensionlsecaspal%[old1],%[old2],%[new1],%[new2],%[v]"
    };
    static const char *const numbered[] = {
        ".arch_extensionlsecasp%0,%1,%3,%4,%2",
        ".arch_extensionlsecaspal%0,%1,%3,%4,%2"
    };
    char compact[192];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || c == ';' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < 2U; ++i)
        if (strcmp(compact, named[i]) == 0 ||
            strcmp(compact, numbered[i]) == 0) return (unsigned int)i + 1U;
    return 0U;
}

bool aarch64_linux_lse_dec_positive_form(
    const char *text, size_t length) {
    static const char *const patterns[] = {
        ".arch_extensionlse"
        "1:ldr%x[tmp],%[v]"
        "subs%[ret],%x[tmp],#1"
        "b.lt2f"
        "casal%x[tmp],%[ret],%[v]"
        "sub%x[tmp],%x[tmp],#1"
        "sub%x[tmp],%x[tmp],%[ret]"
        "cbnz%x[tmp],1b2:",
        ".arch_extensionlse"
        "1:ldr%x2,%1"
        "subs%0,%x2,#1"
        "b.lt2f"
        "casal%x2,%0,%1"
        "sub%x2,%x2,#1"
        "sub%x2,%x2,%0"
        "cbnz%x2,1b2:"
    };
    char compact[360];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || c == ';' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(patterns) / sizeof(patterns[0]); ++i)
        if (strcmp(compact, patterns[i]) == 0) return true;
    return false;
}

/* GNU's plus-earlyclobber pair must be x0/x1, replacements x2/x3, and
 * +Q memory must materialize as the address in x10.  Inputs 5..7 are
 * deliberately checked although the original asm does not mention them:
 * their GNU operand bindings are part of the sealed Core contract. */
static const struct A64CaspRole {
    const char *name;
    const char *constraint;
    unsigned int reg;
    unsigned int access;
} a64_casp_roles[] = {
    {"old1", "+&r", 0U, MINIC_MACHINE_REGISTER_READ |
                           MINIC_MACHINE_REGISTER_WRITE},
    {"old2", "+&r", 1U, MINIC_MACHINE_REGISTER_READ |
                           MINIC_MACHINE_REGISTER_WRITE},
    {"v", "+Q", 10U, MINIC_MACHINE_REGISTER_READ},
    {"new1", "r", 2U, MINIC_MACHINE_REGISTER_READ},
    {"new2", "r", 3U, MINIC_MACHINE_REGISTER_READ},
    {"ptr", "r", 4U, MINIC_MACHINE_REGISTER_READ},
    {"oldval1", "r", 11U, MINIC_MACHINE_REGISTER_READ},
    {"oldval2", "r", 12U, MINIC_MACHINE_REGISTER_READ}
};

static bool a64_casp_form(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, void *context) {
    unsigned int form;
    const unsigned int memory = MINIC_MACHINE_EFFECT_MEMORY_READ |
        MINIC_MACHINE_EFFECT_MEMORY_WRITE;
    const unsigned int required = MINIC_MACHINE_EFFECT_SOURCE_ASM |
        MINIC_MACHINE_EFFECT_VOLATILE |
        MINIC_MACHINE_EFFECT_COMPILER_BARRIER | memory;
    (void)context;
    if (fn == NULL || fn->target == NULL ||
        fn->target->architecture != MINIC_TARGET_ARCH_AARCH64 ||
        ins == NULL || ins->kind != MINIC_MACHINE_INSTRUCTION_SOURCE_ASM ||
        ins->source_asm_machine_form_id != AARCH64_MACHINE_ASM_LSE_CASP128 ||
        ins->source_asm_template == NULL ||
        ins->operand_count != sizeof(a64_casp_roles) /
            sizeof(a64_casp_roles[0]) ||
        ins->implicit_count != 0U || ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough)
        return false;
    form = aarch64_linux_lse_casp128_form(ins->source_asm_template,
        ins->source_asm_length);
    return form != 0U && ins->source_asm_machine_form_variant == form &&
        ins->effects == required;
}

static bool a64_casp_binding(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, size_t index, void *context) {
    const MinicMachineOperand *op;
    const struct A64CaspRole *role;
    const bool named = ins != NULL && ins->source_asm_template != NULL &&
        memchr(ins->source_asm_template, '[', ins->source_asm_length) != NULL;
    (void)context;
    if (!a64_casp_form(fn, ins, NULL) ||
        index >= sizeof(a64_casp_roles) / sizeof(a64_casp_roles[0]) ||
        ins->operands == NULL) return false;
    op = &ins->operands[index];
    role = &a64_casp_roles[index];
    return op->kind == MINIC_MACHINE_OPERAND_REGISTER &&
        op->asm_constraint != NULL &&
        strcmp(op->asm_constraint, role->constraint) == 0 &&
        (!named || (op->asm_name != NULL &&
                    strcmp(op->asm_name, role->name) == 0)) &&
        (named || op->asm_name == NULL ||
         strcmp(op->asm_name, role->name) == 0) &&
        op->value.physical.reg.bank == MINIC_MACHINE_REGISTER_GENERAL &&
        op->value.physical.reg.number == role->reg &&
        op->value.physical.reg.width_bits == 64U &&
        op->value.physical.access == role->access;
}

static bool a64_casp_operand(FILE *output, const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins,
    const MinicMachineGNUAsmLabels *labels, size_t index,
    unsigned char modifier, void *context) {
    const struct A64CaspRole *role;
    (void)labels;
    if (modifier != 0U ||
        !a64_casp_binding(fn, ins, index, context)) return false;
    role = &a64_casp_roles[index];
    if (output == NULL) return true;
    if (index == 2U) return fputs("[x10]", output) >= 0;
    return fprintf(output, "x%u", role->reg) >= 0;
}

static const MinicMachineSourceAsmTemplatePolicy a64_casp_policy = {
    a64_casp_form, a64_casp_binding, a64_casp_operand, NULL
};

static bool a64_lse_parse_form(const MinicMachineInstruction *ins,
    Aarch64LseForm *form) {
    return ins != NULL && form != NULL &&
        (aarch64_lse_form(ins->source_asm_template,
             ins->source_asm_length, true, form) ||
         aarch64_lse_form(ins->source_asm_template,
             ins->source_asm_length, false, form));
}

bool minic_aarch64_machine_lse_casp128_validate(
    const MinicMachineFunction *fn, const MinicMachineInstruction *ins) {
    return minic_machine_source_asm_template_preflight(fn, ins,
        &a64_casp_policy);
}

bool minic_aarch64_machine_lse_casp128_emit(FILE *output,
    const MinicMachineFunction *fn, const MinicMachineInstruction *ins,
    const MinicMachineGNUAsmLabels *labels) {
    if (output == NULL ||
        !minic_machine_source_asm_template_emit(output, fn, ins,
            labels, &a64_casp_policy)) return false;
    return (ins->source_asm_length > 0U &&
            ins->source_asm_template[ins->source_asm_length - 1U] == '\n') ||
        fputc('\n', output) != EOF;
}

/* In the scalar LSE family, the +Q carrier is an address in x10; the
 * operation's old-value result (if any) is x9 and the scalar input x11.
 * Source qualifiers distinguish plain stores, fetched RMW, and CAS. */
static bool a64_scalar_lse_form(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, void *context) {
    Aarch64LseForm form = {0};
    const unsigned int effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
        MINIC_MACHINE_EFFECT_VOLATILE |
        MINIC_MACHINE_EFFECT_COMPILER_BARRIER |
        MINIC_MACHINE_EFFECT_MEMORY_READ |
        MINIC_MACHINE_EFFECT_MEMORY_WRITE;
    (void)context;
    if (fn == NULL || fn->target == NULL ||
        fn->target->architecture != MINIC_TARGET_ARCH_AARCH64 ||
        ins == NULL || ins->kind != MINIC_MACHINE_INSTRUCTION_SOURCE_ASM ||
        ins->source_asm_machine_form_id != AARCH64_MACHINE_ASM_LSE ||
        ins->source_asm_template == NULL ||
        ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough || ins->implicit_count != 0U ||
        !a64_lse_parse_form(ins, &form) ||
        ins->operand_count != (form.kind == AARCH64_LSE_STORE ? 2U : 3U))
        return false;
    return ins->source_asm_machine_form_variant ==
        (((unsigned int)form.kind << 16) |
         ((unsigned int)form.width << 8) | form.mode) &&
        ins->effects == effects;
}

static bool a64_scalar_lse_binding(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, size_t index, void *context) {
    Aarch64LseForm form = {0};
    const MinicMachineOperand *op;
    const bool named = ins != NULL && ins->source_asm_template != NULL &&
        memchr(ins->source_asm_template, '[', ins->source_asm_length) != NULL;
    unsigned int reg, access;
    const char *constraint, *name;
    (void)context;
    if (!a64_scalar_lse_form(fn, ins, NULL) ||
        !a64_lse_parse_form(ins, &form) ||
        index >= ins->operand_count || ins->operands == NULL) return false;
    op = &ins->operands[index];
    if (index == 0U) {
        reg = 10U; access = MINIC_MACHINE_REGISTER_READ;
        constraint = "+Q"; name = "v";
    } else if (index == 1U && form.kind != AARCH64_LSE_STORE) {
        reg = 9U;
        access = form.kind == AARCH64_LSE_CAS ?
            MINIC_MACHINE_REGISTER_READ | MINIC_MACHINE_REGISTER_WRITE :
            MINIC_MACHINE_REGISTER_WRITE;
        constraint = form.kind == AARCH64_LSE_CAS ? "+r" : "=r";
        name = "old";
    } else {
        reg = 11U; access = MINIC_MACHINE_REGISTER_READ;
        constraint = form.kind == AARCH64_LSE_CAS ? "rZ" : "r";
        name = form.kind == AARCH64_LSE_CAS ? "new" : "i";
    }
    return op->kind == MINIC_MACHINE_OPERAND_REGISTER &&
        op->asm_constraint != NULL &&
        strcmp(op->asm_constraint, constraint) == 0 &&
        (!named || (op->asm_name != NULL &&
                    strcmp(op->asm_name, name) == 0)) &&
        (named || op->asm_name == NULL ||
         strcmp(op->asm_name, name) == 0) &&
        op->value.physical.reg.bank == MINIC_MACHINE_REGISTER_GENERAL &&
        op->value.physical.reg.number == reg &&
        op->value.physical.reg.width_bits == 64U &&
        op->value.physical.access == access;
}

static bool a64_scalar_lse_operand(FILE *output,
    const MinicMachineFunction *fn, const MinicMachineInstruction *ins,
    const MinicMachineGNUAsmLabels *labels, size_t index,
    unsigned char modifier, void *context) {
    Aarch64LseForm form = {0};
    unsigned int reg;
    const char *prefix;
    (void)labels;
    if (!a64_scalar_lse_binding(fn, ins, index, context) ||
        !a64_lse_parse_form(ins, &form)) return false;
    if (index == 0U) return modifier == 0U &&
        (output == NULL || fputs("[x10]", output) >= 0);
    if (modifier != (form.width == 8U ? (unsigned char)'x' :
                    (unsigned char)'w') &&
        !(form.width == 8U && modifier == 0U)) return false;
    reg = index == 1U && form.kind != AARCH64_LSE_STORE ? 9U : 11U;
    prefix = form.width == 8U ? "x" : "w";
    return output == NULL || fprintf(output, "%s%u", prefix, reg) >= 0;
}

static const MinicMachineSourceAsmTemplatePolicy a64_scalar_lse_policy = {
    a64_scalar_lse_form, a64_scalar_lse_binding,
    a64_scalar_lse_operand, NULL
};

bool minic_aarch64_machine_lse_scalar_validate(
    const MinicMachineFunction *fn, const MinicMachineInstruction *ins) {
    return minic_machine_source_asm_template_preflight(fn, ins,
        &a64_scalar_lse_policy);
}

bool minic_aarch64_machine_lse_scalar_emit(FILE *output,
    const MinicMachineFunction *fn, const MinicMachineInstruction *ins,
    const MinicMachineGNUAsmLabels *labels) {
    if (output == NULL ||
        !minic_machine_source_asm_template_emit(output, fn, ins,
            labels, &a64_scalar_lse_policy)) return false;
    return (ins->source_asm_length > 0U &&
            ins->source_asm_template[ins->source_asm_length - 1U] == '\n') ||
        fputc('\n', output) != EOF;
}

static bool a64_dec_form(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, void *context) {
    const MinicMachineRegisterEffect *effect;
    (void)context;
    if (fn == NULL || fn->target == NULL ||
        fn->target->architecture != MINIC_TARGET_ARCH_AARCH64 ||
        ins == NULL || ins->kind != MINIC_MACHINE_INSTRUCTION_SOURCE_ASM ||
        ins->source_asm_machine_form_id !=
            AARCH64_MACHINE_ASM_LSE_DEC_POSITIVE ||
        ins->source_asm_machine_form_variant != 0U ||
        ins->source_asm_template == NULL ||
        !aarch64_linux_lse_dec_positive_form(
            ins->source_asm_template, ins->source_asm_length) ||
        ins->operand_count != 3U || ins->operands == NULL ||
        ins->implicit_count != 1U || ins->implicit == NULL ||
        ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough ||
        ins->effects != (MINIC_MACHINE_EFFECT_SOURCE_ASM |
                         MINIC_MACHINE_EFFECT_VOLATILE |
                         MINIC_MACHINE_EFFECT_MEMORY_READ |
                         MINIC_MACHINE_EFFECT_MEMORY_WRITE |
                         MINIC_MACHINE_EFFECT_COMPILER_BARRIER)) return false;
    effect = &ins->implicit[0];
    return effect->reg.bank == MINIC_MACHINE_REGISTER_FLAGS &&
        effect->reg.number == 0U && effect->reg.width_bits == 4U &&
        effect->access == MINIC_MACHINE_REGISTER_CLOBBER;
}

static bool a64_dec_binding(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, size_t index, void *context) {
    static const struct {
        const char *name, *constraint;
        unsigned int reg, access;
    } roles[] = {
        {"ret", "+&r", 9U, MINIC_MACHINE_REGISTER_READ |
                            MINIC_MACHINE_REGISTER_WRITE},
        {"v", "+Q", 10U, MINIC_MACHINE_REGISTER_READ},
        {"tmp", "=&r", 12U, MINIC_MACHINE_REGISTER_WRITE}
    };
    const MinicMachineOperand *op;
    const bool named = ins != NULL && ins->source_asm_template != NULL &&
        memchr(ins->source_asm_template, '[', ins->source_asm_length) != NULL;
    (void)context;
    if (!a64_dec_form(fn, ins, NULL) || index >= 3U) return false;
    op = &ins->operands[index];
    return op->kind == MINIC_MACHINE_OPERAND_REGISTER &&
        op->asm_constraint != NULL &&
        strcmp(op->asm_constraint, roles[index].constraint) == 0 &&
        (!named || (op->asm_name != NULL &&
                    strcmp(op->asm_name, roles[index].name) == 0)) &&
        (named || op->asm_name == NULL ||
         strcmp(op->asm_name, roles[index].name) == 0) &&
        op->value.physical.reg.bank == MINIC_MACHINE_REGISTER_GENERAL &&
        op->value.physical.reg.number == roles[index].reg &&
        op->value.physical.reg.width_bits == 64U &&
        op->value.physical.access == roles[index].access;
}

static bool a64_dec_operand(FILE *output, const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins,
    const MinicMachineGNUAsmLabels *labels, size_t index,
    unsigned char modifier, void *context) {
    const unsigned int reg = index == 0U ? 9U : index == 1U ? 10U : 12U;
    (void)labels;
    if (!a64_dec_binding(fn, ins, index, context)) return false;
    if (index == 1U) return modifier == 0U &&
        (output == NULL || fputs("[x10]", output) >= 0);
    if (modifier != (index == 2U ? 'x' : 0U)) return false;
    return output == NULL || fprintf(output, "x%u", reg) >= 0;
}

static const MinicMachineSourceAsmTemplatePolicy a64_dec_policy = {
    a64_dec_form, a64_dec_binding, a64_dec_operand, NULL
};

bool minic_aarch64_machine_lse_dec_positive_validate(
    const MinicMachineFunction *fn, const MinicMachineInstruction *ins) {
    return minic_machine_source_asm_template_preflight(fn, ins,
        &a64_dec_policy);
}

bool minic_aarch64_machine_lse_dec_positive_emit(FILE *output,
    const MinicMachineFunction *fn, const MinicMachineInstruction *ins,
    const MinicMachineGNUAsmLabels *labels) {
    if (output == NULL ||
        !minic_machine_source_asm_template_emit(output, fn, ins,
            labels, &a64_dec_policy)) return false;
    return (ins->source_asm_length > 0U &&
            ins->source_asm_template[ins->source_asm_length - 1U] == '\n') ||
        fputc('\n', output) != EOF;
}
