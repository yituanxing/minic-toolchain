#include "frontend/ast.h"
#include "frontend/ast_verifier.h"
#include "frontend/cast_normalization.h"

#include <stdbool.h>
#include <string.h>

static MinicSourceSpan test_span(size_t offset) {
    MinicSourceSpan span;

    (void)memset(&span, 0, sizeof(span));
    span.begin.offset = offset;
    span.begin.line = 1U;
    span.begin.column = offset + 1U;
    span.end = span.begin;
    span.end.offset += 1U;
    span.end.column += 1U;
    return span;
}

static bool add_integer(MinicC0Program *program, int value, MinicExpressionId *expression_id) {
    MinicExpression expression;

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_INTEGER;
    expression.span = test_span(program->expression_count);
    expression.type = minic_type_int();
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.integer_value = value;
    return minic_c0_program_add_expression(program, &expression, expression_id);
}

static bool add_cast(MinicC0Program *program,
                     MinicExpressionId operand,
                     MinicType target_type,
                     MinicExpressionId *expression_id) {
    MinicExpression expression;

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_CAST;
    expression.span = test_span(program->expression_count);
    expression.type = target_type;
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.unary.operand = operand;
    return minic_c0_program_add_expression(program, &expression, expression_id);
}

static bool add_expression_statement(MinicC0Program *program,
                                     MinicExpressionId expression_id,
                                     MinicStatementId *statement_id) {
    MinicStatement statement;

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_EXPRESSION;
    statement.span = test_span(program->statement_count);
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = expression_id;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;
    return minic_c0_program_add_statement(program, &statement, statement_id);
}

static int test_integer_cast_topology(void) {
    MinicC0Program program;
    MinicExpressionId integer_id;
    MinicExpressionId cast_id;
    MinicStatementId statement_id;
    const MinicStatement *statement;
    const MinicExpression *normalized;

    minic_c0_program_initialize(&program);
    if (!add_integer(&program, 257, &integer_id) ||
        !add_cast(&program, integer_id, minic_type_unsigned_char(), &cast_id) ||
        !add_expression_statement(&program, cast_id, &statement_id) ||
        !minic_c0_program_verify(&program, MINIC_C0_AST_PARSED) ||
        minic_c0_program_verify(&program, MINIC_C0_AST_NORMALIZED) ||
        !minic_c0_program_normalize_casts(&program) ||
        !minic_c0_program_verify(&program, MINIC_C0_AST_NORMALIZED)) {
        minic_c0_program_destroy(&program);
        return 1;
    }

    statement = minic_c0_program_statement(&program, statement_id);
    normalized =
        statement == NULL ? NULL : minic_c0_program_expression(&program, statement->expression);
    if (program.expression_count != 2U || statement == NULL || normalized == NULL ||
        normalized->kind != MINIC_EXPRESSION_CONVERSION ||
        normalized->value.unary.operand >= statement->expression ||
        normalized->value.unary.operand != integer_id ||
        !minic_type_equal(normalized->type, minic_type_unsigned_char())) {
        minic_c0_program_destroy(&program);
        return 2;
    }

    minic_c0_program_destroy(&program);
    return 0;
}

static int test_pointer_bitcast_identity(void) {
    MinicC0Program program;
    MinicLocal local;
    MinicLocalId local_id;
    MinicExpression local_expression;
    MinicExpressionId local_expression_id;
    MinicExpressionId cast_id;
    MinicStatementId statement_id;
    MinicType int_pointer;
    MinicType void_pointer;
    const MinicStatement *statement;
    const MinicExpression *bitcast;
    const MinicExpression *operand;

    minic_c0_program_initialize(&program);
    if (!minic_type_pointer_to(minic_type_int(), &int_pointer) ||
        !minic_type_pointer_to(minic_type_void(), &void_pointer)) {
        return 3;
    }

    minic_c0_local_initialize(&local);
    local.name_span = test_span(0U);
    local.type = int_pointer;
    local.element_count = 1U;
    if (!minic_c0_program_add_local(&program, &local, &local_id)) {
        minic_c0_program_destroy(&program);
        return 4;
    }

    (void)memset(&local_expression, 0, sizeof(local_expression));
    local_expression.kind = MINIC_EXPRESSION_LOCAL;
    local_expression.span = test_span(0U);
    local_expression.type = int_pointer;
    local_expression.value_category = MINIC_VALUE_LVALUE;
    local_expression.value.local_id = local_id;
    if (!minic_c0_program_add_expression(&program, &local_expression, &local_expression_id) ||
        !add_cast(&program, local_expression_id, void_pointer, &cast_id) ||
        !add_expression_statement(&program, cast_id, &statement_id) ||
        !minic_c0_program_verify(&program, MINIC_C0_AST_PARSED) ||
        !minic_c0_program_normalize_casts(&program) ||
        minic_c0_program_verify(&program, MINIC_C0_AST_PARSED) ||
        !minic_c0_program_verify(&program, MINIC_C0_AST_NORMALIZED)) {
        minic_c0_program_destroy(&program);
        return 5;
    }

    statement = minic_c0_program_statement(&program, statement_id);
    bitcast =
        statement == NULL ? NULL : minic_c0_program_expression(&program, statement->expression);
    operand = bitcast == NULL ? NULL
                              : minic_c0_program_expression(&program, bitcast->value.unary.operand);
    if (statement == NULL || bitcast == NULL || operand == NULL ||
        bitcast->kind != MINIC_EXPRESSION_BITCAST ||
        bitcast->value.unary.operand >= statement->expression ||
        !minic_type_equal(bitcast->type, void_pointer) || operand->kind != MINIC_EXPRESSION_LOCAL ||
        !minic_type_equal(operand->type, int_pointer) || operand->value.local_id != local_id) {
        minic_c0_program_destroy(&program);
        return 6;
    }

    minic_c0_program_destroy(&program);
    return 0;
}

static int test_malformed_contracts(void) {
    MinicC0Program program;
    MinicExpression expression;
    MinicExpressionId integer_id;
    MinicExpressionId malformed_id;
    MinicStatementId statement_id;
    MinicLocal local;
    MinicLocalId local_id;

    minic_c0_program_initialize(&program);
    if (!add_integer(&program, 1, &integer_id)) {
        return 7;
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_BINARY;
    expression.span = test_span(1U);
    expression.type = minic_type_int();
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.binary.operator_kind = MINIC_BINARY_ADD;
    expression.value.binary.left = integer_id;
    expression.value.binary.right = 1U;
    if (!minic_c0_program_add_expression(&program, &expression, &malformed_id) ||
        minic_c0_program_verify(&program, MINIC_C0_AST_PARSED)) {
        minic_c0_program_destroy(&program);
        return 8;
    }
    minic_c0_program_destroy(&program);

    minic_c0_program_initialize(&program);
    if (!add_integer(&program, 2, &integer_id)) {
        return 9;
    }
    program.expressions[integer_id].value_category = MINIC_VALUE_LVALUE;
    if (minic_c0_program_verify(&program, MINIC_C0_AST_PARSED)) {
        minic_c0_program_destroy(&program);
        return 10;
    }
    minic_c0_program_destroy(&program);

    minic_c0_program_initialize(&program);
    if (!add_integer(&program, 3, &integer_id) ||
        !add_expression_statement(&program, program.expression_count + 10U, &statement_id) ||
        minic_c0_program_verify(&program, MINIC_C0_AST_PARSED)) {
        minic_c0_program_destroy(&program);
        return 11;
    }
    minic_c0_program_destroy(&program);

    minic_c0_program_initialize(&program);
    minic_c0_local_initialize(&local);
    local.name_span = test_span(0U);
    local.type = minic_type_int();
    local.type.is_plain_char = true;
    local.element_count = 1U;
    if (!minic_c0_program_add_local(&program, &local, &local_id) ||
        minic_c0_program_verify(&program, MINIC_C0_AST_PARSED)) {
        minic_c0_program_destroy(&program);
        return 12;
    }
    minic_c0_program_destroy(&program);

    minic_c0_program_initialize(&program);
    minic_c0_local_initialize(&local);
    local.name_span = test_span(0U);
    local.type = minic_type_int();
    local.type.function_type_id = 0U;
    local.element_count = 1U;
    if (!minic_c0_program_add_local(&program, &local, &local_id) ||
        minic_c0_program_verify(&program, MINIC_C0_AST_PARSED)) {
        minic_c0_program_destroy(&program);
        return 13;
    }
    minic_c0_program_destroy(&program);

    minic_c0_program_initialize(&program);
    minic_c0_local_initialize(&local);
    local.name_span = test_span(0U);
    local.type = minic_type_int();
    local.type.pointer_qualifiers = 1U;
    local.element_count = 1U;
    if (!minic_c0_program_add_local(&program, &local, &local_id) ||
        minic_c0_program_verify(&program, MINIC_C0_AST_PARSED)) {
        minic_c0_program_destroy(&program);
        return 14;
    }
    minic_c0_program_destroy(&program);
    return 0;
}

static bool verify_builtin_local(const MinicTargetInfo *target, MinicType type) {
    MinicC0Program program;
    MinicLocal local;
    MinicLocalId local_id;
    bool valid;

    minic_c0_program_initialize(&program);
    minic_c0_local_initialize(&local);
    local.name_span = test_span(0U);
    local.type = type;
    local.element_count = 1U;
    if (!minic_c0_program_add_local(&program, &local, &local_id)) {
        minic_c0_program_destroy(&program);
        return false;
    }
    valid = minic_c0_program_verify_target(&program, MINIC_C0_AST_PARSED, target);
    minic_c0_program_destroy(&program);
    return valid;
}

/* The type-role adapter centralizes extended language declarations. The
 * builtin va_list source identity is now Program-owned and shape-checked by
 * TargetInfo, rather than a frontend-only scalar base-kind. */
static int test_builtin_type_target_admission(void) {
    static const MinicTargetArchitecture architectures[] = {
        MINIC_TARGET_ARCH_RISCV32,
        MINIC_TARGET_ARCH_RISCV64,
        MINIC_TARGET_ARCH_X86_64,
        MINIC_TARGET_ARCH_AARCH64,
        MINIC_TARGET_ARCH_LOONGARCH64
    };
    size_t index;

    for (index = 0U; index < sizeof(architectures) / sizeof(architectures[0]); ++index) {
        const MinicTargetInfo *target;
        MinicType malformed;
        bool is_aarch64, has_extended_float;

        target = minic_target_info_for_architecture(architectures[index]);
        is_aarch64 = architectures[index] == MINIC_TARGET_ARCH_AARCH64;
        has_extended_float = architectures[index] != MINIC_TARGET_ARCH_RISCV32;
        malformed = minic_type_float128();
        malformed.integer_rank = MINIC_INTEGER_RANK_INT;
        if (target == NULL || !verify_builtin_local(target, minic_type_float()) ||
            verify_builtin_local(target, minic_type_float128()) != has_extended_float ||
            verify_builtin_local(target, malformed) ||
            verify_builtin_local(target, minic_type_aarch64_header_builtin(
                                             MINIC_TYPE_BASE_AARCH64_SVE_BOOL)) != is_aarch64) {
            return 90;
        }
    }
    return 0;
}

static int test_verify_failure_provenance(void) {
    MinicC0AstVerifyFailure failure;
    MinicC0Program program;
    MinicLocal local;
    MinicLocalId local_id;

    minic_c0_program_initialize(&program);
    minic_c0_local_initialize(&local);
    local.name_span = test_span(0U);
    local.type = minic_type_int();
    local.element_count = 1U;
    local.explicit_alignment = 3U;
    if (!minic_c0_program_add_local(&program, &local, &local_id) ||
        minic_c0_program_verify_detailed(&program, MINIC_C0_AST_PARSED, &failure) ||
        failure.stage != MINIC_C0_AST_VERIFY_LOCAL || failure.index != local_id ||
        failure.subindex != MINIC_C0_AST_VERIFY_INDEX_NONE || failure.reason == NULL) {
        minic_c0_program_destroy(&program);
        return 30;
    }
    minic_c0_program_destroy(&program);
    return 0;
}

/* New semantic fields must acquire safe defaults in the owning constructor, not parser call sites. */
static int test_semantic_entity_defaults(void) {
    MinicC0Program program;
    MinicFunctionId function_id;
    MinicGlobalObjectId object_id;
    MinicRecordId record_id;
    const MinicFunction *function;
    const MinicGlobalObject *object;
    const MinicRecord *record;
    const MinicRecordField *field;

    minic_c0_program_initialize(&program);
    if (!minic_c0_program_add_function(&program,
                                       "f",
                                       1U,
                                       0U,
                                       0U,
                                       MINIC_BLOCK_INVALID,
                                       &function_id) ||
        !minic_c0_program_add_record(&program, "R", 1U, &record_id) ||
        !minic_c0_record_add_field(
            &program, record_id, "x", 1U, minic_type_int(), 1U) ||
        !minic_c0_program_add_global_object(
            &program, "g", 1U, minic_type_int(), false, false, &object_id)) {
        minic_c0_program_destroy(&program);
        return 31;
    }

    function = minic_c0_program_function(&program, function_id);
    record = minic_c0_program_record(&program, record_id);
    field = minic_c0_record_field(record, 0U);
    object = minic_c0_program_global_object(&program, object_id);
    if (function == NULL || record == NULL || field == NULL || object == NULL ||
        function->alias_target != MINIC_FUNCTION_INVALID ||
        function->assembler_name != NULL || function->assembler_name_length != 0U ||
        function->section_name != NULL || function->section_name_length != 0U ||
        function->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT ||
        !minic_type_equal(function->return_type, minic_type_int()) ||
        function->parameter_count != 0U || function->is_defined ||
        field->explicit_alignment != 0U || field->is_array || field->is_packed ||
        field->is_bit_field || field->is_flexible_array || field->is_zero_length_array ||
        field->is_anonymous_member ||
        object->alias_target != MINIC_GLOBAL_OBJECT_INVALID ||
        object->section_name != NULL || object->section_name_length != 0U ||
        object->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT ||
        object->explicit_alignment != 0U || object->is_internal || object->is_weak ||
        object->is_zero_initialized || object->is_extern || object->is_tentative ||
        object->is_block_scope_extern_only) {
        minic_c0_program_destroy(&program);
        return 32;
    }
    minic_c0_program_destroy(&program);
    return 0;
}

static int test_normalization_transaction(void) {
    MinicC0Program program;
    MinicExpressionId integer_id;
    MinicExpressionId cast_id;
    MinicStatementId statement_id;
    MinicExpression *original_expressions;
    size_t original_expression_count;
    MinicExpressionId original_statement_expression;

    minic_c0_program_initialize(&program);
    if (!add_integer(&program, 4, &integer_id) ||
        !add_cast(&program, integer_id, minic_type_unsigned_char(), &cast_id) ||
        !add_expression_statement(&program, program.expression_count + 20U, &statement_id)) {
        minic_c0_program_destroy(&program);
        return 15;
    }

    original_expressions = program.expressions;
    original_expression_count = program.expression_count;
    original_statement_expression = program.statements[statement_id].expression;
    if (minic_c0_program_normalize_casts(&program) || program.expressions != original_expressions ||
        program.expression_count != original_expression_count ||
        program.statements[statement_id].expression != original_statement_expression ||
        program.expressions[cast_id].kind != MINIC_EXPRESSION_CAST) {
        minic_c0_program_destroy(&program);
        return 16;
    }

    minic_c0_program_destroy(&program);
    return 0;
}

/* Function parameter types are a private, realloc-stable allocation.
 * Snapshots borrow the source; cloning a Function copies parameters through
 * the public setter, and repeated signature updates cannot alias freed data.
 */
/* Always available on the legacy inline64 layout as well as the
 * experimental inline8+private overflow layout. The latter must clone
 * overflow during AST normalization instead of aliasing freed old storage. */
static int test_call_argument_overflow_normalization(void) {
    MinicC0Program program;
    MinicExpression call;
    MinicExpressionId argument, call_id;
    const MinicExpression *stored;
    size_t i;
    int failure = 0;
#ifdef MINIC_CALL_INLINE_ARGUMENTS
    uint32_t scratch[MINIC_MAX_FUNCTION_PARAMETERS-MINIC_CALL_INLINE_ARGUMENTS];
#endif

    minic_c0_program_initialize(&program);
    (void)memset(&call, 0, sizeof(call));
    call.kind = MINIC_EXPRESSION_CALL;
    call.type = minic_type_int();
    call.value_category = MINIC_VALUE_RVALUE;
    call.value.call.function_id = 0U; /* direct, not indirect */
    call.value.call.callee = MINIC_EXPRESSION_INVALID;
    call.value.call.argument_count = MINIC_MAX_FUNCTION_PARAMETERS;
#ifdef MINIC_CALL_INLINE_ARGUMENTS
    call.value.call.overflow_arguments = scratch; /* stack-borrowed during build */
#endif
    for (i=0U; i<MINIC_MAX_FUNCTION_PARAMETERS; ++i) {
        if (!add_integer(&program, (int)i, &argument) ||
            !minic_c0_call_set_argument(&call, i, argument)) {
            failure = 80;
            goto finish;
        }
    }
    if (minic_c0_call_set_argument(&call, MINIC_MAX_FUNCTION_PARAMETERS, argument) ||
        !minic_c0_program_add_expression(&program, &call, &call_id)) {
        failure = 81;
        goto finish;
    }
    stored = minic_c0_program_expression(&program, call_id);
    if (stored == NULL || stored->value.call.argument_count != MINIC_MAX_FUNCTION_PARAMETERS) {
        failure = 82;
        goto finish;
    }
#ifdef MINIC_CALL_INLINE_ARGUMENTS
    if (stored->value.call.overflow_arguments == scratch ||
        stored->value.call.overflow_arguments == NULL) {
        failure = 83;
        goto finish;
    }
#endif
    for (i=0U; i<MINIC_MAX_FUNCTION_PARAMETERS; ++i) {
#ifdef MINIC_CALL_INLINE_ARGUMENTS
        argument = minic_c0_call_argument(stored, i);
#else
        argument = stored->value.call.arguments[i];
#endif
        if (argument != (MinicExpressionId)i) {
            failure = 84;
            goto finish;
        }
    }
    if (!minic_c0_program_normalize_casts(&program)) {
        failure = 85;
        goto finish;
    }
    stored = minic_c0_program_expression(&program, call_id);
    if (stored == NULL || stored->kind != MINIC_EXPRESSION_CALL) {
        failure = 86;
        goto finish;
    }
    for (i=0U; i<MINIC_MAX_FUNCTION_PARAMETERS; ++i) {
#ifdef MINIC_CALL_INLINE_ARGUMENTS
        argument = minic_c0_call_argument(stored, i);
#else
        argument = stored->value.call.arguments[i];
#endif
        if (argument != (MinicExpressionId)i) {
            failure = 87;
            goto finish;
        }
    }
#ifdef MINIC_CALL_INLINE_ARGUMENTS
    if (stored->value.call.overflow_arguments == scratch ||
        stored->value.call.overflow_arguments == NULL) {
        failure = 88;
    }
#endif
finish:
    minic_c0_program_destroy(&program);
    return failure;
}

static int test_function_signature_storage_ownership(void) {
    MinicC0Program program;
    MinicFunctionId source_id, clone_id, unused_id;
    MinicFunction source_snapshot;
    MinicType parameters[3], changed[2], max_parameters[MINIC_MAX_FUNCTION_PARAMETERS];
    MinicType invalid = minic_type_void();
    const MinicType *original_storage;
    size_t i;
    int failed = 0;

    minic_c0_program_initialize(&program);
    parameters[0] = minic_type_int();
    parameters[1] = minic_type_long();
    parameters[2] = minic_type_unsigned_int();
    changed[0] = minic_type_short();
    changed[1] = minic_type_long_long();

    if (!minic_c0_program_add_function(&program, "original", 8U, 0U, 0U,
                                       MINIC_BLOCK_INVALID, &source_id) ||
        !minic_c0_program_set_function_signature(&program, source_id, minic_type_int(),
                                                 parameters, 3U)) {
        failed = 70;
        goto finish;
    }

    original_storage = program.functions[source_id].parameter_types;
    source_snapshot = program.functions[source_id]; /* borrowed pointer */
    if (original_storage == NULL || original_storage == parameters) {
        failed = 71;
        goto finish;
    }

    /* Reallocate the Function header array repeatedly. Private heap arrays
       must remain stable even when an old Function snapshot is retained. */
    for (i = 0U; i < 100U; ++i) {
        if (!minic_c0_program_add_function(&program, "padding", 7U, 0U, 0U,
                                           MINIC_BLOCK_INVALID, &unused_id)) {
            failed = 72;
            goto finish;
        }
    }
    if (program.functions[source_id].parameter_types != original_storage ||
        source_snapshot.parameter_types != original_storage ||
        !minic_type_equal(source_snapshot.parameter_types[2], parameters[2])) {
        failed = 73;
        goto finish;
    }
    if (!minic_c0_program_add_function(&program, "clone", 5U, 0U, 0U,
                                       MINIC_BLOCK_INVALID, &clone_id) ||
        !minic_c0_program_set_function_signature(&program, clone_id,
                                                 source_snapshot.return_type,
                                                 source_snapshot.parameter_types,
                                                 source_snapshot.parameter_count)) {
        failed = 74;
        goto finish;
    }
    if (program.functions[clone_id].parameter_types == original_storage ||
        !minic_type_equal(program.functions[clone_id].parameter_types[2], parameters[2])) {
        failed = 75;
        goto finish;
    }
    if (!minic_c0_program_set_function_signature(&program, source_id,
                                                  minic_type_int(), changed, 2U) ||
        !minic_type_equal(program.functions[clone_id].parameter_types[2], parameters[2]) ||
        !minic_type_equal(program.functions[source_id].parameter_types[0], changed[0])) {
        failed = 76;
        goto finish;
    }
    /* In-place caller passes the owned storage back to the setter. */
    if (!minic_c0_program_set_function_signature(&program, clone_id,
                                                  program.functions[clone_id].return_type,
                                                  program.functions[clone_id].parameter_types,
                                                  program.functions[clone_id].parameter_count) ||
        minic_c0_program_set_function_signature(&program, clone_id,
                                                 minic_type_int(), &invalid, 1U) ||
        program.functions[clone_id].parameter_count != 3U ||
        !minic_type_equal(program.functions[clone_id].parameter_types[2], parameters[2])) {
        failed = 77;
        goto finish;
    }

    for (i = 0U; i < MINIC_MAX_FUNCTION_PARAMETERS; ++i) {
        max_parameters[i] = minic_type_int();
    }
    if (!minic_c0_program_set_function_signature(&program, clone_id,
                                                  minic_type_long(), max_parameters,
                                                  MINIC_MAX_FUNCTION_PARAMETERS) ||
        program.functions[clone_id].parameter_count != MINIC_MAX_FUNCTION_PARAMETERS ||
        !minic_type_equal(program.functions[clone_id].parameter_types[63],
                          minic_type_int()) ||
        !minic_c0_program_set_function_signature(&program, clone_id,
                                                  minic_type_int(), NULL, 0U) ||
        program.functions[clone_id].parameter_types != NULL ||
        program.functions[clone_id].parameter_count != 0U) {
        failed = 78;
    }
finish:
    minic_c0_program_destroy(&program);
    return failed;
}

int main(void) {
    int status;

    status = test_integer_cast_topology();
    if (status != 0) {
        return status;
    }
    status = test_pointer_bitcast_identity();
    if (status != 0) {
        return status;
    }
    status = test_malformed_contracts();
    if (status != 0) {
        return status;
    }
    status = test_builtin_type_target_admission();
    if (status != 0) {
        return status;
    }
    status = test_verify_failure_provenance();
    if (status != 0) {
        return status;
    }
    status = test_semantic_entity_defaults();
    if (status != 0) {
        return status;
    }
    status = test_function_signature_storage_ownership();
    if (status != 0) {
        return status;
    }
    status = test_call_argument_overflow_normalization();
    if (status != 0) {
        return status;
    }
    return test_normalization_transaction();
}
