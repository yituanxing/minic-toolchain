#include "target/loongarch64/machine_source_asm.h"
#include "target/loongarch64/machine_opcode.h"
#include "target/loongarch64/machine_target_state.h"
#include "target/loongarch64/machine_asm_form.h"
#include "target/loongarch64/asm_contract_internal.h"
#include "target/common/core_machine_asm.h"
#include "target/common/direct_linux_bug_asm.h"
#include "target/common/machine_source_asm_template.h"
#include <ctype.h>
#include <inttypes.h>
#include <stdint.h>
#include <string.h>

/* A zero-ID source form is deliberately restricted to the already qualified
 * operand-free hardware spellings. GNU directives, local labels, arbitrary
 * source instructions and Linux CSR/LL-SC/AMO forms require a separately
 * sealed target machine form. */
static bool loong_source_opaque(const MinicMachineInstruction *ins) {
    static const char *const literals[] = {
        "nop", "nop\n\t", "idle 0", "idle 0\n\t",
        "tlbrd", "tlbclr", "tlbflush", "tlbsrch", "tlbwr", "tlbfill",
        "dbar 0", "dbar 0x700", "dbar 0\n\t", "ibar 0",
        "\tibar 0\n", "ibar 0 \t\ndbar 0 \t\n"
    };
    MinicCoreInlineAsm linux_bug = {0};
    size_t i;
    /* Static-key asm-goto is a single, exactly sealed GNU record with a
     * source-owned C branch target and one typed symbolic relocation. Core
     * CFG provenance and Machine successor equality are checked separately. */
    if (ins->source_asm_machine_form_id == 0U &&
        ins->source_asm_machine_form_variant == 0U &&
        loong_linux_jump_label_form(ins->source_asm_template,
            ins->source_asm_length) != 0U)
        return ins->operand_count == 2U && ins->operands != NULL &&
            ins->implicit_count == 0U && ins->source_asm_terminates_block &&
            ins->source_asm_permits_fallthrough &&
            (ins->effects & (MINIC_MACHINE_EFFECT_SOURCE_ASM |
                             MINIC_MACHINE_EFFECT_VOLATILE |
                             MINIC_MACHINE_EFFECT_BRANCH)) ==
                            (MINIC_MACHINE_EFFECT_SOURCE_ASM |
                             MINIC_MACHINE_EFFECT_VOLATILE |
                             MINIC_MACHINE_EFFECT_BRANCH) &&
            (ins->effects & (MINIC_MACHINE_EFFECT_MEMORY_READ |
                             MINIC_MACHINE_EFFECT_MEMORY_WRITE |
                             MINIC_MACHINE_EFFECT_RETURN)) == 0U &&
            ins->operands[0].kind == MINIC_MACHINE_OPERAND_SYMBOL &&
            ins->operands[0].asm_constraint != NULL &&
            strcmp(ins->operands[0].asm_constraint, "i") == 0 &&
            ins->operands[1].kind == MINIC_MACHINE_OPERAND_BLOCK &&
            ins->operands[1].asm_constraint == NULL &&
            ins->operands[1].asm_name != NULL &&
            strcmp(ins->operands[1].asm_name, "l_yes") == 0;
    if (ins->operand_count != 0U || ins->implicit_count != 0U ||
        ins->source_asm_machine_form_variant != 0U ||
        ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough ||
        (ins->effects & (MINIC_MACHINE_EFFECT_CALL |
                         MINIC_MACHINE_EFFECT_BRANCH |
                         MINIC_MACHINE_EFFECT_RETURN)) != 0U)
        return false;
    /* Core's original Linux BUG-table qualifier seals only an exact
     * GNU metadata shape ending in `break 1`. Recheck that same shape at
     * the physical emission boundary: accepting the whole template does
     * not grant an arbitrary source-program or directive escape hatch. */
    linux_bug.template_text = ins->source_asm_template;
    linux_bug.template_length = ins->source_asm_length;
    linux_bug.is_volatile =
        (ins->effects & MINIC_MACHINE_EFFECT_VOLATILE) != 0U;
    linux_bug.has_memory_clobber =
        (ins->effects & MINIC_MACHINE_EFFECT_COMPILER_BARRIER) != 0U;
    if (minic_multi_direct_linux_bug_asm(&linux_bug,
            "10000", "10001", "10002", "break1", false))
        return true;
    for (i = 0U; i < sizeof(literals) / sizeof(literals[0]); ++i)
        if (ins->source_asm_length == strlen(literals[i]) &&
            memcmp(ins->source_asm_template, literals[i],
                   ins->source_asm_length) == 0)
            return true;
    return false;
}

static bool loong_source_form(const MinicMachineInstruction *ins) {
    const uint32_t id = ins->source_asm_machine_form_id;
    if (id == 0U) return loong_source_opaque(ins);
    if (id <= 14U)
        return minic_loongarch64_typed_descriptor_matches_template(id,
                ins->source_asm_machine_form_variant,
                ins->source_asm_template, ins->source_asm_length);
    /* ID 20 is LSX/LASX and requires its own typed SIMD register grammar;
     * do not misinterpret vector operands as general-purpose registers. */
    if (id == 20U) return false;
    /* C ingress seals the GNU named rdtime form as variant 2, but the
     * source-to-Core normalizer rewrites %[val]/%[tid] into %0/%1. That is
     * exactly the numeric template shape: retain the sealed named identity
     * while requiring its two early-clobber physical outputs. Arbitrary
     * numeric ASM may not borrow variant 2 to bypass this check. */
    if (id == MINIC_LOONG_MACHINE_ASM_CLOCK &&
        ins->source_asm_machine_form_variant == LOONG_CLOCK_RDTIME_NAMED &&
        loong_linux_clock_form(ins->source_asm_template,
            ins->source_asm_length) == LOONG_CLOCK_RDTIME_NUMERIC) {
        size_t i;
        if (ins->operand_count != 2U || ins->operands == NULL ||
            ins->implicit_count != 0U ||
            (ins->effects & MINIC_MACHINE_EFFECT_VOLATILE) == 0U ||
            (ins->effects & MINIC_MACHINE_EFFECT_COMPILER_BARRIER) != 0U)
            return false;
        for (i = 0U; i < 2U; ++i)
            if (ins->operands[i].kind != MINIC_MACHINE_OPERAND_REGISTER ||
                ins->operands[i].asm_constraint == NULL ||
                strcmp(ins->operands[i].asm_constraint, "=&r") != 0 ||
                ins->operands[i].value.physical.access !=
                    MINIC_MACHINE_REGISTER_WRITE)
                return false;
        return true;
    }
    if (id <= 28U)
        return minic_loongarch64_typed_scalar_form_matches_template(id,
                ins->source_asm_machine_form_variant,
                ins->source_asm_template, ins->source_asm_length);
    return false;
}

static bool loong_source_gp(const MinicMachineRegister *reg) {
    return reg != NULL && reg->bank == MINIC_MACHINE_REGISTER_GENERAL &&
           reg->number < 32U && reg->width_bits == 64U;
}

static bool loong_source_constraint(const MinicTargetInfo *target,
    const MinicMachineInstruction *ins, const MinicMachineOperand *operand) {
    const char *c;
    size_t i = 0U;
    bool output = false, readwrite = false;
    if (target == NULL ||
        target->architecture != MINIC_TARGET_ARCH_LOONGARCH64 ||
        operand == NULL || (c = operand->asm_constraint) == NULL)
        return false;
    if (c[0] == '=' || c[0] == '+') {
        output = c[0] == '=';
        readwrite = c[0] == '+';
        ++i;
    }
    if (c[i] == '&') {
        if (!output) return false;
        ++i;
    }
    if (c[i] == '\0') return false;
    if (operand->kind == MINIC_MACHINE_OPERAND_REGISTER) {
        const MinicMachineRegister *reg = &operand->value.physical.reg;
        const unsigned access = operand->value.physical.access;
        if (!(((strcmp(c + i, "r") == 0 ||
                 strcmp(c + i, "Jr") == 0) && loong_source_gp(reg)) ||
              (strcmp(c + i, "f") == 0 &&
               reg->bank == MINIC_MACHINE_REGISTER_FLOAT &&
               reg->number < 32U &&
               (reg->width_bits == 32U || reg->width_bits == 64U))))
            return false;
        if (reg->bank == MINIC_MACHINE_REGISTER_FLOAT &&
            minic_loongarch64_target_soft_float_abi(target)) return false;
        if ((output || readwrite) &&
            ((access & MINIC_MACHINE_REGISTER_WRITE) == 0U ||
             (reg->bank == MINIC_MACHINE_REGISTER_GENERAL &&
              reg->number == 0U))) return false;
        return (output && !readwrite) ||
            ((access & MINIC_MACHINE_REGISTER_READ) != 0U);
    }
    if (operand->kind == MINIC_MACHINE_OPERAND_ADDRESS) {
        const MinicMachineAddress *address = &operand->value.address;
        const bool mem = strcmp(c + i, "m") == 0 ||
                         strcmp(c + i, "ZB") == 0 ||
                         strcmp(c + i, "ZC") == 0;
        if (!mem || !address->has_base || address->has_index ||
            address->has_segment || address->has_symbol ||
            !loong_source_gp(&address->base) ||
            address->target_mode != MINIC_LOONG_ADDRESS_IMM12 ||
            address->displacement < -2048 ||
            address->displacement > 2047 ||
            (output || readwrite ?
                 (ins->effects & MINIC_MACHINE_EFFECT_MEMORY_WRITE) == 0U :
                 false) ||
            (!output || readwrite ?
                 (ins->effects & MINIC_MACHINE_EFFECT_MEMORY_READ) == 0U :
                 false)) return false;
        return true;
    }
    if (output || readwrite) return false;
    if (operand->kind == MINIC_MACHINE_OPERAND_INTEGER) {
        if (strcmp(c + i, "Jr") == 0) return operand->value.integer == 0;
        if (strcmp(c + i, "I") == 0) return
            operand->value.integer >= 0 && operand->value.integer <= 2047;
        return strcmp(c + i, "i") == 0 ||
               strcmp(c + i, "n") == 0;
    }
    if (operand->kind == MINIC_MACHINE_OPERAND_SYMBOL) {
        const char *symbol = operand->value.symbol.name;
        const unsigned char *p = (const unsigned char *)symbol;
        if (strcmp(c + i, "i") != 0 || !p ||
            !(*p == '_' || *p == '.' || isalpha(*p)) ||
            operand->value.symbol.relocation != MINIC_LOONG_RELOC_DIRECT ||
            operand->value.symbol.addend != 0) return false;
        for (++p; *p; ++p)
            if (*p != '_' && *p != '.' && *p != '$' && !isalnum(*p))
                return false;
        return true;
    }
    return false;
}

static bool loong_source_print_gp(FILE *out, unsigned reg) {
    if (reg == 0U) return fputs("$zero", out) >= 0;
    if (reg == 1U) return fputs("$ra", out) >= 0;
    if (reg == 2U) return fputs("$tp", out) >= 0;
    if (reg == 3U) return fputs("$sp", out) >= 0;
    if (reg <= 11U) return fprintf(out, "$a%u", reg - 4U) >= 0;
    if (reg <= 20U) return fprintf(out, "$t%u", reg - 12U) >= 0;
    if (reg == 21U) return fputs("$r21", out) >= 0;
    if (reg == 22U) return fputs("$fp", out) >= 0;
    return fprintf(out, "$s%u", reg - 23U) >= 0;
}

static bool loong_source_print_reg(FILE *out,
    const MinicMachineRegister *reg) {
    if (reg->bank == MINIC_MACHINE_REGISTER_GENERAL)
        return loong_source_print_gp(out, reg->number);
    if (reg->bank == MINIC_MACHINE_REGISTER_FLOAT)
        return fprintf(out, "$f%u", reg->number) >= 0;
    return false;
}

static bool loong_source_operand(FILE *out,
    const MinicMachineFunction *fn, const MinicMachineInstruction *ins,
    const MinicMachineGNUAsmLabels *labels, size_t id,
    unsigned char modifier, void *context) {
    const MinicMachineOperand *operand;
    const bool label_operand = modifier == 'l';
    (void)context;
    if (id >= ins->operand_count || ins->operands == NULL) return false;
    operand = &ins->operands[id];
    if (label_operand) {
        if (operand->kind != MINIC_MACHINE_OPERAND_BLOCK ||
            (ins->effects & MINIC_MACHINE_EFFECT_BRANCH) == 0U ||
            !ins->source_asm_terminates_block) return false;
        if (out == NULL) return true;
        return labels != NULL && labels->names != NULL &&
            operand->value.block_id < labels->count &&
            labels->names[operand->value.block_id] != NULL &&
            fputs(labels->names[operand->value.block_id], out) >= 0;
    }
    if (!loong_source_constraint(fn->target, ins, operand) ||
        (modifier != 0U && modifier != 'c' && modifier != 'n' &&
         modifier != 'z') ||
        (modifier == 'c' && operand->kind != MINIC_MACHINE_OPERAND_INTEGER) ||
        (modifier == 'n' && operand->kind != MINIC_MACHINE_OPERAND_INTEGER) ||
        (modifier == 'z' && operand->kind != MINIC_MACHINE_OPERAND_REGISTER &&
         operand->kind != MINIC_MACHINE_OPERAND_INTEGER)) return false;
    if (out == NULL) return true;
    if (modifier == 'n') {
        const int64_t n = operand->value.integer;
        return n > 0 ? fprintf(out, "-%" PRId64, n) >= 0 :
            fprintf(out, "%" PRIu64, -(uint64_t)n) >= 0;
    }
    switch (operand->kind) {
    case MINIC_MACHINE_OPERAND_REGISTER:
        return loong_source_print_reg(out, &operand->value.physical.reg);
    case MINIC_MACHINE_OPERAND_ADDRESS:
        /* GNU LoongArch +ZB atomic operands expand to the base GPR alone:
         * AMO instructions take rd,rk,rj (not a base+offset addressing
         * form). LL/SC `m`/`ZC` operands instead need rj,imm12. The Core
         * source ID is sealed against the exact instruction grammar, so
         * this distinction remains target-owned and non-heuristic. */
        if ((ins->source_asm_machine_form_id == LOONG_DESC_XCHG ||
             ins->source_asm_machine_form_id ==
                 MINIC_LOONG_MACHINE_ASM_PERCPU_AMO ||
             ins->source_asm_machine_form_id ==
                 MINIC_LOONG_MACHINE_ASM_AMO_STORE ||
             ins->source_asm_machine_form_id ==
                 MINIC_LOONG_MACHINE_ASM_AMO_FETCH) &&
            operand->asm_constraint != NULL &&
            strcmp(operand->asm_constraint, "+ZB") == 0 &&
            operand->value.address.displacement == 0)
            return loong_source_print_gp(out,
                operand->value.address.base.number);
        return loong_source_print_gp(out, operand->value.address.base.number) &&
            fprintf(out, ", %" PRId64,
                    operand->value.address.displacement) >= 0;
    case MINIC_MACHINE_OPERAND_INTEGER:
        if (modifier == 'z' && operand->value.integer == 0)
            return fputs("$zero", out) >= 0;
        return fprintf(out, "%" PRId64, operand->value.integer) >= 0;
    case MINIC_MACHINE_OPERAND_SYMBOL:
        return fputs(operand->value.symbol.name, out) >= 0;
    default:
        return false;
    }
}

static bool loong_source_validate_form(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, void *context) {
    unsigned int form, reg, access;
    (void)context;
    if (fn == NULL || fn->target == NULL || ins == NULL ||
        fn->target->architecture != MINIC_TARGET_ARCH_LOONGARCH64 ||
        !loong_source_form(ins)) return false;
    if (ins->source_asm_machine_form_id == LOONG_DESC_FPR_TRANSFER) {
        const MinicMachineRegisterEffect *effect = ins->implicit;
        form = loong_linux_fpr_transfer_form(ins->source_asm_template,
            ins->source_asm_length);
        if (!form || ins->operand_count != 1U ||
            ins->implicit_count != 1U || effect == NULL) return false;
        access = form <= 32U ? MINIC_MACHINE_REGISTER_READ :
                               MINIC_MACHINE_REGISTER_WRITE;
        reg = (form - 1U) % 32U;
        return effect[0].reg.bank == MINIC_MACHINE_REGISTER_FLOAT &&
            effect[0].reg.number == reg &&
            effect[0].reg.width_bits == 64U &&
            effect[0].access == access;
    }
    if (ins->source_asm_machine_form_id == LOONG_DESC_PERCPU_INDEXED) {
        const MinicMachineRegisterEffect *effect = ins->implicit;
        if (ins->operand_count != 2U || ins->implicit_count != 1U ||
            effect == NULL) return false;
        return effect[0].reg.bank == MINIC_MACHINE_REGISTER_GENERAL &&
            effect[0].reg.number == 21U &&
            effect[0].reg.width_bits == 64U &&
            effect[0].access == MINIC_MACHINE_REGISTER_READ;
    }
    return true;
}

static bool loong_source_validate_binding(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, size_t id, void *context) {
    const MinicMachineOperand *op;
    (void)context;
    if (fn == NULL || fn->target == NULL || id >= ins->operand_count ||
        ins->operands == NULL) return false;
    op = &ins->operands[id];
    if (ins->source_asm_machine_form_id ==
            MINIC_LOONG_MACHINE_ASM_PERCPU_AMO &&
        (ins->operand_count != 3U ||
         (ins->effects & (MINIC_MACHINE_EFFECT_MEMORY_READ |
                          MINIC_MACHINE_EFFECT_MEMORY_WRITE |
                          MINIC_MACHINE_EFFECT_VOLATILE)) !=
                          (MINIC_MACHINE_EFFECT_MEMORY_READ |
                           MINIC_MACHINE_EFFECT_MEMORY_WRITE |
                           MINIC_MACHINE_EFFECT_VOLATILE) ||
         (id == 0U && (op->kind != MINIC_MACHINE_OPERAND_REGISTER ||
                        op->asm_constraint == NULL ||
                        strcmp(op->asm_constraint, "=&r") != 0)) ||
         (id == 1U && (op->kind != MINIC_MACHINE_OPERAND_ADDRESS ||
                        op->asm_constraint == NULL ||
                        strcmp(op->asm_constraint, "+ZB") != 0 ||
                        op->value.address.displacement != 0)) ||
         (id == 2U && (op->kind != MINIC_MACHINE_OPERAND_REGISTER ||
                        op->asm_constraint == NULL ||
                        strcmp(op->asm_constraint, "r") != 0))))
        return false;
    if (op->kind == MINIC_MACHINE_OPERAND_BLOCK)
        return op->asm_constraint == NULL &&
            ins->source_asm_terminates_block &&
            (ins->effects & MINIC_MACHINE_EFFECT_BRANCH) != 0U;
    return loong_source_constraint(fn->target, ins, op);
}

static const MinicMachineSourceAsmTemplatePolicy loong_asm_policy = {
    loong_source_validate_form,
    loong_source_validate_binding,
    loong_source_operand,
    NULL
};

bool minic_loongarch64_machine_validate_source_asm(
    const MinicTargetInfo *target, const MinicMachineInstruction *ins,
    void *context) {
    MinicMachineFunction probe = {0};
    (void)context;
    if (target == NULL ||
        target->architecture != MINIC_TARGET_ARCH_LOONGARCH64)
        return false;
    probe.target = target;
    return minic_machine_source_asm_template_preflight(
        &probe, ins, &loong_asm_policy);
}

bool minic_loongarch64_machine_emit_source_asm(
    FILE *out, const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins,
    const MinicMachineGNUAsmLabels *labels, void *context) {
    (void)context;
    if (out == NULL || fn == NULL || labels == NULL ||
        !minic_machine_source_asm_template_preflight(
            fn, ins, &loong_asm_policy) ||
        fputs("# MINIC_SOURCE_INLINE_ASM_BEGIN\n  ", out) < 0 ||
        !minic_machine_source_asm_template_emit(
            out, fn, ins, labels, &loong_asm_policy)) return false;
    return fputs("\n# MINIC_SOURCE_INLINE_ASM_END\n", out) >= 0;
}

/* Core has already sealed the target form and occurrence identity. This
 * bridge intentionally does not reconstruct operands from C expressions or
 * guess register allocations: the same role must be physically realized by
 * the selector before MachineIR may claim a valid SOURCE_ASM instruction. */
bool minic_loongarch64_select_core_source_asm(
    MinicMachineBuilder *builder, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *instruction) {
    const MinicCoreFunction *fn;
    const MinicCoreInlineAsm *source;
    size_t source_id;
    if (builder == NULL || ctx == NULL || instruction == NULL ||
        (fn = ctx->fn) == NULL ||
        builder->function == NULL ||
        builder->function->target != ctx->target ||
        ctx->target == NULL ||
        ctx->target->architecture != MINIC_TARGET_ARCH_LOONGARCH64)
        return false;
    if (instruction->kind == MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM) {
        source_id = instruction->value.inline_asm_id;
    } else if (instruction->kind ==
               MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM) {
        /* One physical binding requires a selected register or memory
         * transport, not an assembler text placeholder or borrowed Core
         * value. That transport remains explicitly unsupported here. */
        if (instruction->value.structured_inline_asm.operand_count != 0U)
            return false;
        source_id = instruction->value.structured_inline_asm.inline_asm_id;
    } else return false;
    if (source_id >= fn->inline_asm_count || fn->inline_asms == NULL)
        return false;
    source = &fn->inline_asms[source_id];
    if (source->is_goto || source->register_clobber_count != 0U ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        source->template_text == NULL) return false;
    return minic_machine_builder_emit_core_source_asm(builder, source,
        0U, NULL, 0U, NULL, 0U, false, false);
}

bool minic_loongarch64_select_core_dbar_hint(
    MinicMachineBuilder *builder, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *instruction) {
    const MinicCoreFunction *fn;
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *role;
    const MinicCoreInstruction *definition;
    MinicCoreInstructionId definition_id;
    MinicMachineOperand binding = {0};
    int64_t hint;
    if (builder == NULL || builder->function == NULL || ctx == NULL ||
        (fn = ctx->fn) == NULL || ctx->target == NULL ||
        builder->function->target != ctx->target ||
        ctx->target->architecture != MINIC_TARGET_ARCH_LOONGARCH64 ||
        instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        instruction->result != MINIC_CORE_VALUE_INVALID ||
        !minic_core_function_type_is_kind(fn, instruction->core_type_id,
            MINIC_CORE_TYPE_VOID) ||
        instruction->value.structured_inline_asm.operand_count != 1U ||
        instruction->value.structured_inline_asm.inline_asm_id >=
            fn->inline_asm_count || fn->inline_asms == NULL)
        return false;
    source = &fn->inline_asms[
        instruction->value.structured_inline_asm.inline_asm_id];
    role = &instruction->value.structured_inline_asm.operands[0];
    if (source->target_machine_form_id !=
            MINIC_LOONG_MACHINE_ASM_HINT_DBAR ||
        source->target_machine_form_variant != 1U ||
        source->operand_plan_owner !=
            MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        !source->is_volatile || source->is_goto ||
        !source->has_memory_clobber ||
        source->register_clobber_count != 0U ||
        !loongarch64_dbar_template(source->template_text,
            source->template_length) ||
        role->operand_index != 0U ||
        role->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        role->has_fixed_register_binding || role->early_clobber ||
        role->value >= fn->value_count)
        return false;
    definition_id = fn->values[role->value].definition;
    if (definition_id >= fn->instruction_count) return false;
    definition = &fn->instructions[definition_id];
    if (definition->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT ||
        definition->result != role->value ||
        !loongarch64_dbar_hint(
            (hint = definition->value.integer_value)))
        return false;
    binding.kind = MINIC_MACHINE_OPERAND_INTEGER;
    binding.value.integer = hint;
    binding.asm_constraint = "I";
    return minic_machine_builder_emit_core_source_asm(builder, source,
        0U, &binding, 1U, NULL, 0U, false, false);
}
