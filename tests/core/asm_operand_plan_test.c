#include "core/core_lower_asm_internal.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static MinicInlineAsmOperand make_operand(char *name, char *constraint,
                                          MinicInlineAsmOperandAccess access) {
    MinicInlineAsmOperand result;
    (void)memset(&result, 0, sizeof(result));
    result.name = name;
    result.name_length = name ? strlen(name) : 0U;
    result.constraint_text = constraint;
    result.constraint_length = strlen(constraint);
    result.access = access;
    return result;
}

static void check_source_plan(MinicAsmOperandPlanOwner owner) {
    MinicInlineAsmOperand outputs[3], inputs[3];
    MinicInlineAsm source;
    MinicAsmOperandPlan plan;
    MinicCoreInstruction ins;
    MinicCoreInlineAsm effect;
    uint64_t initial_id;
    size_t i;

    outputs[0] = make_operand("sum", "+&r", MINIC_INLINE_ASM_OPERAND_READ_WRITE);
    outputs[1] = make_operand("out", "=m", MINIC_INLINE_ASM_OPERAND_WRITE_ONLY);
    outputs[2] = make_operand("buffer", "+m", MINIC_INLINE_ASM_OPERAND_READ_WRITE);
    inputs[0] = make_operand(NULL, "0", MINIC_INLINE_ASM_OPERAND_READ_ONLY);
    inputs[1] = make_operand(NULL, "[sum]", MINIC_INLINE_ASM_OPERAND_READ_ONLY);
    inputs[2] = make_operand(NULL, "r", MINIC_INLINE_ASM_OPERAND_READ_ONLY);
    (void)memset(&source, 0, sizeof(source));
    source.template_text = "add %0, %0, %5";
    source.template_length = strlen(source.template_text);
    source.outputs = outputs;
    source.output_count = 3U;
    source.inputs = inputs;
    source.input_count = 3U;
    source.is_volatile = true;

    (void)memset(&plan, 0, sizeof(plan));
    plan.operand_count = 6U;
    plan.kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    plan.kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
    plan.kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    for (i = 3U; i < 6U; ++i)
        plan.kinds[i] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    assert(minic_backend_capture_asm_operand_source(&source, owner, &plan));
    assert(plan.owner == owner && plan.output_count == 3U);
    assert(plan.gnu_early_clobber[0] && !plan.gnu_early_clobber[1] &&
           !plan.gnu_early_clobber[2] && !plan.gnu_early_clobber[3]);
    assert(plan.gnu_readwrite[0] && plan.gnu_readwrite[2] &&
           !plan.gnu_readwrite[1] && !plan.gnu_readwrite[3]);
    assert(plan.has_tied_output[3] && plan.tied_output_index[3] == 0U &&
           plan.has_tied_output[4] && plan.tied_output_index[4] == 0U &&
           !plan.has_tied_output[5]);
    assert(!plan.explicit_memory_clobber && plan.implicit_memory_effect &&
           plan.effective_memory_effect);
    initial_id = plan.source_form_id;
    assert(initial_id == minic_backend_asm_source_form_id(&source));

    /* Core carries a target-owned ID/payload without treating the source
     * fingerprint as a machine opcode. This synthetic value tests transport,
     * not target admission (tested by x86's exact form gate separately). */
    plan.target_machine_form_id = owner == MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED
        ? 1234U : 0U;
    plan.target_machine_form_variant = plan.target_machine_form_id ? 8U : 0U;

    (void)memset(&ins, 0, sizeof(ins));
    (void)memset(&effect, 0, sizeof(effect));
    ins.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
    ins.value.structured_inline_asm.operand_count = plan.operand_count;
    effect.has_memory_clobber = true;
    for (i = 0U; i < plan.operand_count; ++i) {
        ins.value.structured_inline_asm.operands[i].kind = plan.kinds[i];
        ins.value.structured_inline_asm.operands[i].operand_index = i;
    }
    assert(core_asm_publish_operand_plan(&ins, &effect, &plan));
    assert(effect.operand_plan_owner == owner && effect.source_form_id == initial_id &&
           effect.source_output_count == 3U && !effect.explicit_memory_clobber &&
           effect.implicit_memory_effect);
    assert(effect.target_machine_form_id == plan.target_machine_form_id &&
           effect.target_machine_form_variant == plan.target_machine_form_variant);
    assert(ins.value.structured_inline_asm.operands[0].gnu_early_clobber &&
           ins.value.structured_inline_asm.operands[0].gnu_readwrite &&
           ins.value.structured_inline_asm.operands[3].has_tied_output &&
           ins.value.structured_inline_asm.operands[3].tied_output_index == 0U);

    outputs[0].constraint_text = "=r";
    outputs[0].constraint_length = 2U;
    assert(minic_backend_asm_source_form_id(&source) != initial_id);
    outputs[0].constraint_text = "+&r";
    outputs[0].constraint_length = 3U;
    source.has_memory_clobber = true;
    source.clobber_count = 1U;
    assert(minic_backend_capture_asm_operand_source(&source, owner, &plan));
    assert(plan.explicit_memory_clobber && plan.implicit_memory_effect &&
           plan.effective_memory_effect);

    inputs[0].constraint_text = NULL;
    assert(!minic_backend_capture_asm_operand_source(&source, owner, &plan));
}

int main(void) {
    check_source_plan(MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED);
    check_source_plan(MINIC_ASM_OPERAND_PLAN_GENERIC_STRUCTURED);
    puts("PASS core/asm_operand_plan source-ties=2 early=1 effects=explicit+implicit forms=2");
    return 0;
}
