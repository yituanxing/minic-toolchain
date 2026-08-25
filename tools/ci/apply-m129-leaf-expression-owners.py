#!/usr/bin/env python3
from pathlib import Path

core_path = Path('src/core/core_lower.c')
text = core_path.read_text()
marker = 'M129_LEAF_EXPRESSION_OWNERS'

if marker not in text:
    # Discarded record expressions still own all side effects. Reuse the
    # aggregate materialization owner for the two value-producing forms that
    # currently reach Linux: record assignment and direct record-return call.
    start_marker = '    /* M86B_RECORD_ASSIGNMENT_EXPRESSION_STATEMENT:'
    end_marker = '    /* M91_BUILTIN_UNREACHABLE_TERMINATOR:'
    start = text.find(start_marker)
    end = text.find(end_marker, start + 1)
    if start < 0 or end < 0 or end <= start:
        raise SystemExit('record expression-statement owner seam changed')
    old_segment = text[start:end]
    if 'lower_record_copy_statement(context, &record_copy)' not in old_segment:
        raise SystemExit('legacy discarded record-assignment route changed')
    new_segment = '''    /* M129_LEAF_EXPRESSION_OWNERS: a discarded aggregate value still has to
       perform the producer's semantic effects.  Record assignment and a direct
       record-returning call already have one shared producer owner:
       lower_record_materialized_address().  Route both through it instead of
       forcing assignment through the older copy-source gate or forcing a
       record-return call through the scalar discarded-value gate. */
    if (minic_type_is_record(expression->type) &&
        (expression->kind == MINIC_EXPRESSION_ASSIGNMENT ||
         (expression->kind == MINIC_EXPRESSION_CALL &&
          expression->value.call.function_id != MINIC_FUNCTION_INVALID))) {
        MinicCoreValueId discarded_record_address;
        MinicCoreLowerStatus status;

        status = lower_record_materialized_address(
            context, statement->expression, &discarded_record_address);
        return core_trace_expression_statement_status(
            context, expression, "discarded-record-materialization", status);
    }

'''
    text = text[:start] + new_segment + text[end:]

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
        if (operand == NULL || !minic_type_is_integer(operand->type) ||
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

# Two isolated strict-Core contracts.  The Linux focused replay remains the
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
