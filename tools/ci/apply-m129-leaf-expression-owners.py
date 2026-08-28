#!/usr/bin/env python3
from pathlib import Path

core_path = Path('src/core/core_lower.c')
text = core_path.read_text()
marker = 'M129_LEAF_EXPRESSION_OWNERS'

if marker not in text:
    # Preserve the established M86B discarded-record assignment owner for the
    # shapes it already supports. M129 only owns the two proven missing leaves:
    # a record assignment whose RHS is a conditional aggregate producer, and a
    # discarded direct record-returning call. This avoids stealing ordinary
    # Linux record copies (for example WRITE_ONCE-style set_pud assignments)
    # from the already-qualified record-copy path.
    start_marker = '    /* M86B_RECORD_ASSIGNMENT_EXPRESSION_STATEMENT:'
    end_marker = '    /* M91_BUILTIN_UNREACHABLE_TERMINATOR:'
    start = text.find(start_marker)
    end = text.find(end_marker, start + 1)
    if start < 0 or end < 0 or end <= start:
        raise SystemExit('record expression-statement owner seam changed')
    old_segment = text[start:end]
    if 'lower_record_copy_statement(context, &record_copy)' not in old_segment:
        raise SystemExit('legacy discarded record-assignment route changed')
    new_owner = '''    /* M129_LEAF_EXPRESSION_OWNERS: keep M86B as the default discarded-record
       assignment owner. Only conditional aggregate RHS values need the newer
       unified materialization owner here; direct record-return calls need the
       same owner when their result is discarded after all call side effects. */
    if (expression->kind == MINIC_EXPRESSION_ASSIGNMENT &&
        minic_type_is_record(expression->type)) {
        const MinicExpression *record_source;

        record_source = minic_c0_program_expression(
            context->body->program, expression->value.binary.right);
        if (record_source != NULL &&
            record_source->kind == MINIC_EXPRESSION_CONDITIONAL) {
            MinicCoreValueId discarded_record_address;
            MinicCoreLowerStatus status;

            status = lower_record_materialized_address(
                context, statement->expression, &discarded_record_address);
            return core_trace_expression_statement_status(
                context,
                expression,
                "discarded-record-conditional-assignment",
                status);
        }
    }
    if (expression->kind == MINIC_EXPRESSION_CALL &&
        minic_type_is_record(expression->type) &&
        expression->value.call.function_id != MINIC_FUNCTION_INVALID) {
        MinicCoreValueId discarded_record_address;
        MinicCoreLowerStatus status;

        status = lower_record_materialized_address(
            context, statement->expression, &discarded_record_address);
        return core_trace_expression_statement_status(
            context, expression, "discarded-record-call", status);
    }

'''
    text = text[:start] + new_owner + old_segment + text[end:]

    # Core has target-neutral integer primitives that exactly express the
    # established RV64 __builtin_isdigit semantics: ((unsigned)x - '0') < 10.
    # Keep the frontend builtin node intact and lower it here rather than
    # teaching condition/loop owners about this leaf operation.
    search_from = text.find('    /* M81_FUNCTION_ADDRESS_VALUE:')
    if search_from < 0:
        raise SystemExit('lower_expression function-address marker missing')
    call_seam = '''    if (expression->kind == MINIC_EXPRESSION_CALL) {
        if (expression->value.call.function_id == MINIC_FUNCTION_INVALID) {
            return lower_indirect_call(context, expression, value_id);
        }
        return lower_direct_call(context, expression, value_id);
    }
'''
    call_pos = text.find(call_seam, search_from)
    if call_pos < 0:
        raise SystemExit('lower_expression call seam changed')
    builtin_block = '''    /* M129_LEAF_EXPRESSION_OWNERS: __builtin_isdigit is a pure integer leaf.
       Preserve the existing direct-backend contract without target-specific
       instructions: convert once to unsigned int, subtract '0' modulo the
       unsigned width, then compare against 10.  The unsigned range test is
       true exactly for the ten decimal digit codes, including for negative
       int inputs where the subtraction wraps above the range. */
    if (expression->kind == MINIC_EXPRESSION_BUILTIN_UNARY &&
        expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_ISDIGIT) {
        const MinicExpression *operand;
        MinicCoreInstruction builtin_instruction;
        MinicCoreLowerStatus status;
        MinicCoreValueId operand_value;
        MinicCoreValueId normalized_value;
        MinicCoreValueId zero_code;
        MinicCoreValueId offset_value;
        MinicCoreValueId digit_count;

        operand = minic_c0_program_expression(
            context->body->program, expression->value.builtin_unary.operand);
        if (operand == NULL || !minic_type_equal(operand->type, minic_type_int()) ||
            !minic_type_equal(expression->type, minic_type_int())) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(
            context, expression->value.builtin_unary.operand, &operand_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = append_integer_conversion(context,
                                           operand->span,
                                           minic_type_unsigned_int(),
                                           operand_value,
                                           &normalized_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&builtin_instruction, 0, sizeof(builtin_instruction));
        builtin_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        builtin_instruction.span = expression->span;
        builtin_instruction.type = minic_type_unsigned_int();
        builtin_instruction.result = MINIC_CORE_VALUE_INVALID;
        builtin_instruction.value.integer_value = 48;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &builtin_instruction, &zero_code)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&builtin_instruction, 0, sizeof(builtin_instruction));
        builtin_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT;
        builtin_instruction.span = expression->span;
        builtin_instruction.type = minic_type_unsigned_int();
        builtin_instruction.result = MINIC_CORE_VALUE_INVALID;
        builtin_instruction.value.binary.left = normalized_value;
        builtin_instruction.value.binary.right = zero_code;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &builtin_instruction, &offset_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&builtin_instruction, 0, sizeof(builtin_instruction));
        builtin_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        builtin_instruction.span = expression->span;
        builtin_instruction.type = minic_type_unsigned_int();
        builtin_instruction.result = MINIC_CORE_VALUE_INVALID;
        builtin_instruction.value.integer_value = 10;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &builtin_instruction, &digit_count)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&builtin_instruction, 0, sizeof(builtin_instruction));
        builtin_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_LESS;
        builtin_instruction.span = expression->span;
        builtin_instruction.type = minic_type_int();
        builtin_instruction.result = MINIC_CORE_VALUE_INVALID;
        builtin_instruction.value.binary.left = offset_value;
        builtin_instruction.value.binary.right = digit_count;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &builtin_instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }

'''
    text = text[:call_pos] + builtin_block + text[call_pos:]
    core_path.write_text(text)

# Two isolated strict-Core contracts. The Linux focused replay remains the
# acceptance oracle, but these keep the semantic owners from disappearing in a
# later refactor.
Path('tests/compiler/c0/m129_builtin_isdigit.c').write_text('''int probe_digit(int value) {
    return __builtin_isdigit(value);
}

int main(void) {
    return probe_digit('7') && !probe_digit('x') && !probe_digit(-1) ? 0 : 1;
}
''')

Path('tests/compiler/c0/m129_discarded_record_values.c').write_text('''typedef struct {
    unsigned long bits;
} record_word_t;

static record_word_t make_word(unsigned long value) {
    return (record_word_t){value + 1UL};
}

int main(void) {
    record_word_t left = {1UL};
    record_word_t right = {2UL};
    left = 1 ? right : make_word(3UL);
    make_word(4UL);
    return left.bits == 2UL ? 0 : 1;
}
''')

print('M129 leaf expression owners and strict regressions staged')
