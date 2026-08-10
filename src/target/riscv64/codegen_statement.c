#include "target/riscv64/codegen_internal.h"

#include <inttypes.h>
#include <string.h>

typedef enum MinicBreakTargetKind {
    MINIC_BREAK_TARGET_NONE = 0,
    MINIC_BREAK_TARGET_WHILE,
    MINIC_BREAK_TARGET_SWITCH
} MinicBreakTargetKind;

typedef struct MinicBreakTarget {
    MinicBreakTargetKind kind;
    size_t label;
} MinicBreakTarget;

#define MINIC_RISCV64_MAX_SWITCH_CASES 128U

typedef struct MinicSwitchLabels {
    MinicStatementId cases[MINIC_RISCV64_MAX_SWITCH_CASES];
    size_t case_count;
    MinicStatementId default_statement;
} MinicSwitchLabels;

static bool minic_riscv64_emit_block_with_break_target(FILE *file,
                                                       const MinicC0Program *program,
                                                       const MinicFunction *function,
                                                       MinicBlockId block_id,
                                                       size_t *label_counter,
                                                       MinicBreakTarget break_target);

static bool
minic_riscv64_emit_normalize_word(FILE *file, MinicType type, const char *register_name) {
    return minic_riscv64_emit_integer_conversion(file, type, register_name);
}

static bool minic_riscv64_emit_assignment(FILE *file,
                                          const MinicC0Program *program,
                                          const MinicFunction *function,
                                          const MinicStatement *statement) {
    const MinicExpression *target;
    const MinicExpression *value;

    target = minic_c0_program_expression(program, statement->target_expression);
    value = minic_c0_program_expression(program, statement->expression);
    if (target == NULL || value == NULL || target->value_category != MINIC_VALUE_LVALUE ||
        !minic_c0_assignment_compatible(program, target->type, statement->expression)) {
        return false;
    }
    return minic_riscv64_emit_expression(file, program, function, statement->expression) &&
           fprintf(file, "  addi sp, sp, -16\n  sd a0, 0(sp)\n") >= 0 &&
           minic_riscv64_emit_lvalue_address(
               file, program, function, statement->target_expression) &&
           fprintf(file, "  ld t0, 0(sp)\n  addi sp, sp, 16\n") >= 0 &&
           (!minic_type_is_integer(target->type) ||
            minic_riscv64_emit_integer_conversion(file, target->type, "t0")) &&
           minic_riscv64_emit_scalar_store(file, target->type, "t0", "a0");
}

static bool minic_riscv64_emit_record_copy(FILE *file,
                                           const MinicC0Program *program,
                                           const MinicFunction *function,
                                           const MinicStatement *statement) {
    const MinicExpression *target;
    const MinicExpression *source;
    const MinicRecord *record;
    size_t storage_size;
    size_t temporary_size;
    size_t index;

    target = minic_c0_program_expression(program, statement->target_expression);
    source = minic_c0_program_expression(program, statement->expression);
    if (target == NULL || source == NULL || target->value_category != MINIC_VALUE_LVALUE ||
        source->value_category != MINIC_VALUE_LVALUE || minic_type_is_const(target->type) ||
        !minic_type_is_record(target->type) || !minic_type_is_record(source->type) ||
        target->type.record_id != source->type.record_id) {
        return false;
    }
    record = minic_c0_program_record(program, target->type.record_id);
    if (record == NULL || !record->is_complete || record->storage_size == 0U ||
        record->storage_size > SIZE_MAX - 15U) {
        return false;
    }
    storage_size = record->storage_size;
    temporary_size = (storage_size + 15U) & ~(size_t)15U;

    if (!minic_riscv64_emit_lvalue_address(file, program, function, statement->expression) ||
        !minic_riscv64_emit_stack_allocate(file, temporary_size) ||
        fprintf(file, "  mv t2, a0\n  mv t3, sp\n") < 0) {
        return false;
    }
    for (index = 0U; index < storage_size; ++index) {
        if (fprintf(file,
                    "  lbu t0, 0(t2)\n"
                    "  sb t0, 0(t3)\n"
                    "  addi t2, t2, 1\n"
                    "  addi t3, t3, 1\n") < 0) {
            return false;
        }
    }
    if (!minic_riscv64_emit_lvalue_address(file, program, function, statement->target_expression) ||
        fprintf(file, "  mv t2, sp\n  mv t3, a0\n") < 0) {
        return false;
    }
    for (index = 0U; index < storage_size; ++index) {
        if (fprintf(file,
                    "  lbu t0, 0(t2)\n"
                    "  sb t0, 0(t3)\n"
                    "  addi t2, t2, 1\n"
                    "  addi t3, t3, 1\n") < 0) {
            return false;
        }
    }
    return minic_riscv64_emit_stack_release(file, temporary_size);
}

static bool minic_riscv64_emit_xor_assignment(FILE *file,
                                              const MinicC0Program *program,
                                              const MinicFunction *function,
                                              const MinicStatement *statement) {
    const MinicExpression *target;
    const MinicExpression *value;
    MinicType common_type;

    target = minic_c0_program_expression(program, statement->target_expression);
    value = minic_c0_program_expression(program, statement->expression);
    if (target == NULL || value == NULL || target->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_integer(target->type) || !minic_type_is_integer(value->type) ||
        !minic_type_integer_common(target->type, value->type, &common_type) ||
        !minic_riscv64_emit_lvalue_address(file, program, function, statement->target_expression) ||
        fprintf(file, "  addi sp, sp, -16\n  sd a0, 0(sp)\n") < 0 ||
        !minic_riscv64_emit_scalar_load(file, target->type, "t0", "a0") ||
        !minic_riscv64_emit_normalize_word(file, common_type, "t0") ||
        fprintf(file, "  sd t0, 8(sp)\n") < 0 ||
        !minic_riscv64_emit_expression(file, program, function, statement->expression) ||
        !minic_riscv64_emit_normalize_word(file, common_type, "a0") ||
        fprintf(file,
                "  ld t0, 8(sp)\n"
                "  xor a0, t0, a0\n") < 0 ||
        !minic_riscv64_emit_normalize_word(file, target->type, "a0") ||
        fprintf(file,
                "  ld t0, 0(sp)\n"
                "  addi sp, sp, 16\n") < 0 ||
        !minic_riscv64_emit_scalar_store(file, target->type, "a0", "t0")) {
        return false;
    }
    return true;
}

static bool minic_riscv64_emit_return(FILE *file,
                                      const MinicC0Program *program,
                                      const MinicFunction *function,
                                      const MinicStatement *statement) {
    if (statement->expression == MINIC_EXPRESSION_INVALID) {
        if (!minic_type_is_void(function->return_type)) {
            return false;
        }
    } else {
        const MinicExpression *value;

        value = minic_c0_program_expression(program, statement->expression);
        if (minic_type_is_void(function->return_type) || value == NULL ||
            !minic_c0_assignment_compatible(
                program, function->return_type, statement->expression)) {
            return false;
        }
        if (minic_type_is_record(function->return_type)) {
            size_t aggregate_size;
            size_t aggregate_chunks;

            if (!minic_type_is_record(value->type) ||
                value->type.record_id != function->return_type.record_id ||
                !minic_riscv64_integer_aggregate_abi(
                    program, function->return_type, &aggregate_size, &aggregate_chunks)) {
                return false;
            }
            (void)aggregate_size;
            if (value->value_category == MINIC_VALUE_LVALUE) {
                if (!minic_riscv64_emit_lvalue_address(
                        file, program, function, statement->expression) ||
                    fprintf(file, "  mv t0, a0\n  ld a0, 0(t0)\n") < 0 ||
                    (aggregate_chunks == 2U && fprintf(file, "  ld a1, 8(t0)\n") < 0)) {
                    return false;
                }
            } else if (value->kind != MINIC_EXPRESSION_CALL ||
                       !minic_riscv64_emit_expression(
                           file, program, function, statement->expression)) {
                return false;
            }
        } else if (!minic_riscv64_emit_expression(file, program, function, statement->expression)) {
            return false;
        }
        if (minic_type_is_integer(function->return_type) &&
            !minic_riscv64_emit_integer_conversion(file, function->return_type, "a0")) {
            return false;
        }
        if (minic_type_is_double(function->return_type) &&
            fprintf(file, "  fmv.d.x fa0, a0\n") < 0) {
            return false;
        }
    }
    return fprintf(file, "  j .L%s_return\n", function->name) >= 0;
}

static bool minic_riscv64_collect_switch_labels(const MinicC0Program *program,
                                                MinicBlockId block_id,
                                                MinicSwitchLabels *labels) {
    const MinicBlock *block;
    size_t index;

    block = minic_c0_program_block(program, block_id);
    if (block == NULL || labels == NULL) {
        return false;
    }
    for (index = 0U; index < block->statement_count; ++index) {
        MinicStatementId statement_id;
        const MinicStatement *statement;

        statement_id = block->statements[index];
        statement = minic_c0_program_statement(program, statement_id);
        if (statement == NULL) {
            return false;
        }
        switch (statement->kind) {
        case MINIC_STATEMENT_CASE:
            if (labels->case_count >= MINIC_RISCV64_MAX_SWITCH_CASES) {
                return false;
            }
            labels->cases[labels->case_count] = statement_id;
            labels->case_count += 1U;
            break;
        case MINIC_STATEMENT_DEFAULT:
            if (labels->default_statement != MINIC_STATEMENT_INVALID) {
                return false;
            }
            labels->default_statement = statement_id;
            break;
        case MINIC_STATEMENT_IF:
            if (!minic_riscv64_collect_switch_labels(program, statement->then_block, labels) ||
                (statement->else_block != MINIC_BLOCK_INVALID &&
                 !minic_riscv64_collect_switch_labels(program, statement->else_block, labels))) {
                return false;
            }
            break;
        case MINIC_STATEMENT_WHILE:
            if (!minic_riscv64_collect_switch_labels(program, statement->then_block, labels)) {
                return false;
            }
            break;
        case MINIC_STATEMENT_SWITCH:
            break;
        case MINIC_STATEMENT_ASSIGN:
        case MINIC_STATEMENT_RECORD_COPY:
        case MINIC_STATEMENT_XOR_ASSIGN:
        case MINIC_STATEMENT_EXPRESSION:
        case MINIC_STATEMENT_INLINE_ASM:
        case MINIC_STATEMENT_RETURN:
        case MINIC_STATEMENT_BREAK:
        case MINIC_STATEMENT_GOTO:
        case MINIC_STATEMENT_LABEL:
            break;
        }
    }
    return true;
}

static bool minic_riscv64_emit_break(FILE *file, MinicBreakTarget target) {
    switch (target.kind) {
    case MINIC_BREAK_TARGET_WHILE:
        return fprintf(file, "  j .Lwhile_end_%zu\n", target.label) >= 0;
    case MINIC_BREAK_TARGET_SWITCH:
        return fprintf(file, "  j .Lswitch_end_%zu\n", target.label) >= 0;
    case MINIC_BREAK_TARGET_NONE:
        return false;
    }
    return false;
}

static bool minic_riscv64_emit_switch(FILE *file,
                                      const MinicC0Program *program,
                                      const MinicFunction *function,
                                      const MinicStatement *statement,
                                      size_t *label_counter) {
    MinicSwitchLabels labels;
    MinicBreakTarget break_target;
    const MinicExpression *selector;
    size_t label;
    size_t index;

    (void)memset(&labels, 0, sizeof(labels));
    labels.default_statement = MINIC_STATEMENT_INVALID;
    selector = minic_c0_program_expression(program, statement->expression);
    if (selector == NULL || !minic_type_is_integer(selector->type) ||
        !minic_riscv64_collect_switch_labels(program, statement->then_block, &labels)) {
        return false;
    }

    label = *label_counter;
    *label_counter += 1U;
    if (!minic_riscv64_emit_expression(file, program, function, statement->expression) ||
        !minic_riscv64_emit_integer_conversion(file, selector->type, "a0") ||
        fprintf(file, "  mv t0, a0\n") < 0) {
        return false;
    }
    for (index = 0U; index < labels.case_count; ++index) {
        MinicStatementId case_id;
        const MinicStatement *case_statement;
        const MinicExpression *case_expression;

        case_id = labels.cases[index];
        case_statement = minic_c0_program_statement(program, case_id);
        if (case_statement == NULL) {
            return false;
        }
        case_expression = minic_c0_program_expression(program, case_statement->expression);
        if (case_expression == NULL || case_expression->kind != MINIC_EXPRESSION_INTEGER ||
            fprintf(file,
                    "  li t1, %" PRId64 "\n"
                    "  beq t0, t1, .Lswitch_case_%zu\n",
                    case_expression->value.integer_value,
                    (size_t)case_id) < 0) {
            return false;
        }
    }
    if (labels.default_statement != MINIC_STATEMENT_INVALID) {
        if (fprintf(file, "  j .Lswitch_default_%zu\n", (size_t)labels.default_statement) < 0) {
            return false;
        }
    } else if (fprintf(file, "  j .Lswitch_end_%zu\n", label) < 0) {
        return false;
    }

    break_target.kind = MINIC_BREAK_TARGET_SWITCH;
    break_target.label = label;
    return minic_riscv64_emit_block_with_break_target(
               file, program, function, statement->then_block, label_counter, break_target) &&
           fprintf(file, ".Lswitch_end_%zu:\n", label) >= 0;
}

static bool minic_riscv64_emit_statement(FILE *file,
                                         const MinicC0Program *program,
                                         const MinicFunction *function,
                                         MinicStatementId statement_id,
                                         const MinicStatement *statement,
                                         size_t *label_counter,
                                         MinicBreakTarget break_target) {
    if (statement == NULL) {
        return false;
    }

    switch (statement->kind) {
    case MINIC_STATEMENT_ASSIGN:
        return minic_riscv64_emit_assignment(file, program, function, statement);

    case MINIC_STATEMENT_RECORD_COPY:
        return minic_riscv64_emit_record_copy(file, program, function, statement);

    case MINIC_STATEMENT_XOR_ASSIGN:
        return minic_riscv64_emit_xor_assignment(file, program, function, statement);

    case MINIC_STATEMENT_EXPRESSION:
        return statement->expression != MINIC_EXPRESSION_INVALID &&
               minic_riscv64_emit_expression(file, program, function, statement->expression);

    case MINIC_STATEMENT_INLINE_ASM:
        return minic_riscv64_emit_inline_asm(file, program, function, statement);

    case MINIC_STATEMENT_RETURN:
        return minic_riscv64_emit_return(file, program, function, statement);

    case MINIC_STATEMENT_BREAK:
        return minic_riscv64_emit_break(file, break_target);

    case MINIC_STATEMENT_GOTO:
        return statement->target_statement != MINIC_STATEMENT_INVALID &&
               fprintf(file, "  j .Luser_%zu\n", (size_t)statement->target_statement) >= 0;

    case MINIC_STATEMENT_LABEL:
        return statement->target_statement == MINIC_STATEMENT_INVALID &&
               fprintf(file, ".Luser_%zu:\n", (size_t)statement_id) >= 0;

    case MINIC_STATEMENT_IF: {
        size_t label;

        label = *label_counter;
        *label_counter += 1U;
        if (!minic_riscv64_emit_expression(file, program, function, statement->expression) ||
            fprintf(file, "  beqz a0, .Lif_else_%zu\n", label) < 0 ||
            !minic_riscv64_emit_block_with_break_target(
                file, program, function, statement->then_block, label_counter, break_target) ||
            fprintf(file,
                    "  j .Lif_end_%zu\n"
                    ".Lif_else_%zu:\n",
                    label,
                    label) < 0) {
            return false;
        }
        if (statement->else_block != MINIC_BLOCK_INVALID &&
            !minic_riscv64_emit_block_with_break_target(
                file, program, function, statement->else_block, label_counter, break_target)) {
            return false;
        }
        return fprintf(file, ".Lif_end_%zu:\n", label) >= 0;
    }

    case MINIC_STATEMENT_WHILE: {
        MinicBreakTarget loop_break_target;
        size_t label;

        label = *label_counter;
        *label_counter += 1U;
        if (fprintf(file, ".Lwhile_condition_%zu:\n", label) < 0) {
            return false;
        }
        if (statement->expression != MINIC_EXPRESSION_INVALID &&
            (!minic_riscv64_emit_expression(file, program, function, statement->expression) ||
             fprintf(file, "  beqz a0, .Lwhile_end_%zu\n", label) < 0)) {
            return false;
        }
        loop_break_target.kind = MINIC_BREAK_TARGET_WHILE;
        loop_break_target.label = label;
        return minic_riscv64_emit_block_with_break_target(file,
                                                          program,
                                                          function,
                                                          statement->then_block,
                                                          label_counter,
                                                          loop_break_target) &&
               fprintf(file,
                       "  j .Lwhile_condition_%zu\n"
                       ".Lwhile_end_%zu:\n",
                       label,
                       label) >= 0;
    }

    case MINIC_STATEMENT_SWITCH:
        return minic_riscv64_emit_switch(file, program, function, statement, label_counter);

    case MINIC_STATEMENT_CASE:
        return fprintf(file, ".Lswitch_case_%zu:\n", (size_t)statement_id) >= 0;

    case MINIC_STATEMENT_DEFAULT:
        return fprintf(file, ".Lswitch_default_%zu:\n", (size_t)statement_id) >= 0;
    }

    return false;
}

static bool minic_riscv64_emit_block_with_break_target(FILE *file,
                                                       const MinicC0Program *program,
                                                       const MinicFunction *function,
                                                       MinicBlockId block_id,
                                                       size_t *label_counter,
                                                       MinicBreakTarget break_target) {
    const MinicBlock *block;
    size_t index;

    block = minic_c0_program_block(program, block_id);
    if (block == NULL) {
        return false;
    }
    for (index = 0U; index < block->statement_count; ++index) {
        MinicStatementId statement_id;
        const MinicStatement *statement;

        statement_id = block->statements[index];
        statement = minic_c0_program_statement(program, statement_id);
        if (!minic_riscv64_emit_statement(
                file, program, function, statement_id, statement, label_counter, break_target)) {
            fprintf(stderr,
                    "CODEGEN_FAIL statement function=%s block=%zu statement=%zu kind=%d\n",
                    function != NULL ? function->name : "<null>",
                    (size_t)block_id,
                    (size_t)statement_id,
                    statement != NULL ? (int)statement->kind : -1);
            return false;
        }
    }
    return true;
}

bool minic_riscv64_emit_block(FILE *file,
                              const MinicC0Program *program,
                              const MinicFunction *function,
                              MinicBlockId block_id,
                              size_t *label_counter) {
    MinicBreakTarget break_target;

    break_target.kind = MINIC_BREAK_TARGET_NONE;
    break_target.label = 0U;
    return minic_riscv64_emit_block_with_break_target(
        file, program, function, block_id, label_counter, break_target);
}
