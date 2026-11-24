#include "frontend/function_body.h"

#include <stdio.h>

typedef struct TestEffects {
    size_t assignments[7];
    size_t clobbers[7];
    size_t targets[7];
    bool invalid_rhs;
} TestEffects;

static bool collect(void *opaque, MinicLocalId id,
                    MinicDirectLocalEffect effect, MinicExpressionId rhs) {
    TestEffects *facts = opaque;
    if (id >= 7U) return false;
    switch (effect) {
    case MINIC_DIRECT_LOCAL_ASSIGN:
        ++facts->assignments[id];
        if (rhs == MINIC_EXPRESSION_INVALID) facts->invalid_rhs = true;
        return true;
    case MINIC_DIRECT_LOCAL_CLOBBER:
        ++facts->clobbers[id];
        return true;
    case MINIC_DIRECT_LOCAL_TARGET_ONLY:
        ++facts->targets[id];
        return true;
    }
    return false;
}

int main(void) {
    MinicLocal locals[7] = {0};
    MinicFunction fn = {.is_defined = true, .local_begin = 0U,
                        .local_count = 7U, .parameter_count = 7U};
    MinicExpression expressions[12] = {0};
    MinicStatement statements[3] = {
        {.kind = MINIC_STATEMENT_ASSIGN, .target_expression = 0U, .expression = 7U},
        {.kind = MINIC_STATEMENT_XOR_ASSIGN, .target_expression = 1U},
        {.kind = MINIC_STATEMENT_EXPRESSION, .target_expression = 6U}
    };
    MinicInlineAsmOperand output = {.expression = 5U};
    MinicInlineAsm assembly = {.outputs = &output, .output_count = 1U};
    MinicC0Program program = {
        .locals = locals, .local_count = 7U,
        .functions = &fn, .function_count = 1U,
        .expressions = expressions, .expression_count = 12U,
        .statements = statements, .statement_count = 3U,
        .inline_asms = &assembly, .inline_asm_count = 1U
    };
    TestEffects facts = {0};
    bool stable[7], parameter[7];
    size_t i;
    for (i = 0U; i < 7U; ++i) {
        locals[i].type = minic_type_int();
        expressions[i].kind = MINIC_EXPRESSION_LOCAL;
        expressions[i].value.local_id = i;
    }
    expressions[7].kind = MINIC_EXPRESSION_INTEGER;
    expressions[7].value.integer_value = 1;
    expressions[8].kind = MINIC_EXPRESSION_ASSIGNMENT;
    expressions[8].value.binary.left = 2U;
    expressions[8].value.binary.right = 7U;
    expressions[9].kind = MINIC_EXPRESSION_ADDRESS_OF;
    expressions[9].value.unary.operand = 3U;
    expressions[10].kind = MINIC_EXPRESSION_UNARY;
    expressions[10].value.unary.operator_kind = MINIC_UNARY_PRE_INCREMENT;
    expressions[10].value.unary.operand = 4U;
    expressions[11].kind = MINIC_EXPRESSION_COMPOUND_ASSIGNMENT;
    expressions[11].value.binary.left = 2U;
    expressions[11].value.binary.right = 7U;

    if (!minic_c0_program_seed_parameter_locals(&program, parameter, stable, 7U) ||
        !minic_c0_program_visit_direct_local_effects(&program, collect, &facts) ||
        !minic_c0_program_stable_parameter_locals(&program, stable, 7U))
        return 1;
    for (i = 0U; i < 7U; ++i) {
        if (!parameter[i] || stable[i] != (i == 6U)) return 2;
    }
    if (facts.assignments[0] != 1U || facts.assignments[2] != 1U ||
        facts.clobbers[1] != 1U || facts.clobbers[2] != 1U ||
        facts.clobbers[3] != 1U || facts.clobbers[4] != 1U ||
        facts.clobbers[5] != 1U || facts.targets[6] != 1U || facts.invalid_rhs)
        return 3;
    assembly.outputs = NULL;
    if (minic_c0_program_stable_parameter_locals(&program, stable, 7U))
        return 4;
    assembly.outputs = &output;
    fn.parameter_count = 8U;
    if (minic_c0_program_seed_parameter_locals(&program, parameter, stable, 7U))
        return 5;
    puts("MINIC_DIRECT_LOCAL_EFFECTS=PASS assignments clobbers target-only malformed");
    return 0;
}
