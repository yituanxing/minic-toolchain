#include "target/loongarch64/simd_asm.h"
#include "target/loongarch64/simd_source_policy.h"
#include "target/backend_source.h"
#include "frontend/type.h"

#include <stdio.h>
#include <string.h>

#define REQUIRE(expr) do { if (!(expr)) return __LINE__; } while (0)

/* Standalone source-expression fixture: this deliberately leaves the
 * compiler's Core machinery out of the source/normalized ISA proof. */
const MinicExpression *minic_c0_program_expression(
    const MinicC0Program *program, MinicExpressionId id) {
    return program != NULL && id < program->expression_count
        ? &program->expressions[id] : NULL;
}

static bool forged_effect_seal(
    const MinicC0Program *program, const MinicInlineAsm *source,
    const char *normalized, size_t length,
    MinicTargetInlineAsmContract *contract) {
    (void)program;
    (void)source;
    (void)normalized;
    (void)length;
    ++contract->operand_count;
    return true;
}

static int run(void) {
    static const char named_source[] =
        "vld $vr0, %[v1]\n"
        "vxor.v $vr1, $vr0, $vr0\n"
        "vst $vr1, %[v2]";
    static const char named_numeric[] =
        "vld $vr0, %0\n"
        "vxor.v $vr1, $vr0, $vr0\n"
        "vst $vr1, %1";
    static const char forged_register[] =
        "vld $vr0, %0\n"
        "vxor.v $vr2, $vr0, $vr0\n"
        "vst $vr1, %1";
    static const char forged_instruction[] =
        "vld $vr0, %0\n"
        "vxor.v $vr1, $vr0, $vr0\n"
        "vst $vr1, %1\n"
        "vst $vr0, %1";
    static const char numeric_rw[] =
        "vld $vr0, %0\n"
        "vst $vr0, %0";
    MinicType pointer;
    MinicExpression expressions[2] = {{0}};
    MinicC0Program program = {0};
    MinicInlineAsmOperand inputs[2] = {{0}};
    MinicInlineAsmOperand output = {0};
    MinicInlineAsm source = {0};
    MinicTargetInlineAsmContract contract = {0};
    MinicSourceBackend backend = {0};
    backend.seal_normalized_inline_asm = minic_loongarch64_simd_seal_normalized;
    REQUIRE(minic_type_pointer_to(minic_type_unsigned_char(), &pointer));
    expressions[0].type = pointer;
    expressions[1].type = pointer;
    program.expressions = expressions;
    program.expression_count = 2U;
    inputs[0].name = "v1";
    inputs[0].name_length = 2U;
    inputs[0].constraint_text = "r";
    inputs[0].constraint_length = 1U;
    inputs[0].expression = 0U;
    inputs[1].name = "v2";
    inputs[1].name_length = 2U;
    inputs[1].constraint_text = "r";
    inputs[1].constraint_length = 1U;
    inputs[1].expression = 1U;
    source.template_text = (char *)named_source;
    source.template_length = sizeof(named_source) - 1U;
    source.inputs = inputs;
    source.input_count = 2U;
    source.is_volatile = true;
    source.has_memory_clobber = true;
    source.clobber_count = 1U;
    REQUIRE(minic_loongarch64_simd_qualify_source(&program, &source, &contract));
    REQUIRE(minic_source_backend_capture_asm_operand_source(
        &source, MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED, &contract));
    REQUIRE(minic_source_backend_seal_normalized_inline_asm(
        &backend, &program, &source, named_numeric,
        sizeof(named_numeric)-1U, &contract));
    REQUIRE(contract.target_machine_form_id == MINIC_LOONG_MACHINE_ASM_SIMD);
    REQUIRE(contract.target_machine_form_variant == MINIC_LOONG_MACHINE_SIMD_LSX);

    contract.target_machine_form_id = 0U;
    contract.target_machine_form_variant = 0U;
    REQUIRE(!minic_source_backend_seal_normalized_inline_asm(
        &backend, &program, &source, forged_register,
        sizeof(forged_register)-1U, &contract));
    REQUIRE(contract.target_machine_form_id == 0U);
    REQUIRE(!minic_source_backend_seal_normalized_inline_asm(
        &backend, &program, &source, forged_instruction,
        sizeof(forged_instruction)-1U, &contract));
    REQUIRE(contract.target_machine_form_id == 0U);
    backend.seal_normalized_inline_asm = forged_effect_seal;
    REQUIRE(!minic_source_backend_seal_normalized_inline_asm(
        &backend, &program, &source, named_numeric,
        sizeof(named_numeric)-1U, &contract));
    backend.seal_normalized_inline_asm = minic_loongarch64_simd_seal_normalized;

    /* Numeric +m form carries a true memory RMW effect even without a
     * literal GNU memory clobber. It is also sealed at the same boundary. */
    expressions[0].value_category = MINIC_VALUE_LVALUE;
    output.constraint_text = "+m";
    output.constraint_length = 2U;
    output.access = MINIC_INLINE_ASM_OPERAND_READ_WRITE;
    output.expression = 0U;
    source.template_text = (char *)numeric_rw;
    source.template_length = sizeof(numeric_rw) - 1U;
    source.outputs = &output;
    source.output_count = 1U;
    source.inputs = NULL;
    source.input_count = 0U;
    source.has_memory_clobber = false;
    source.clobber_count = 0U;
    contract = (MinicTargetInlineAsmContract){0};
    REQUIRE(minic_loongarch64_simd_qualify_source(&program, &source, &contract));
    REQUIRE(minic_source_backend_capture_asm_operand_source(
        &source, MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED, &contract));
    REQUIRE(contract.effective_memory_effect);
    REQUIRE(minic_source_backend_seal_normalized_inline_asm(
        &backend, &program, &source, numeric_rw,
        sizeof(numeric_rw)-1U, &contract));
    REQUIRE(contract.target_machine_form_id == MINIC_LOONG_MACHINE_ASM_SIMD);
    return 0;
}

int main(void) {
    const int failed = run();
    if (failed != 0) {
        (void)fprintf(stderr, "LOONG_SIMD_NORMALIZED_SEAL failed line=%d\n", failed);
        return 1;
    }
    (void)puts("LOONG_SIMD_NORMALIZED_SEAL=PASS named+numeric_rw tampered_reg+extra_instruction forged_effect fail_closed");
    return 0;
}
