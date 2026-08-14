#!/usr/bin/env python3
from pathlib import Path


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    return text.replace(old, new, 1)


# Frontend: represent a static local record as an internal global object.  The
# existing initializer_values vector is used as one logical value per direct
# record field.  Aggregate fields are currently accepted only as all-zero
# initializers, while direct integer/enum fields may carry constant values.
path = Path("src/frontend/parser_statement.c")
text = path.read_text()
marker = "static bool parse_static_local_array_declarator(MinicParser *parser, MinicType base_type) {\n"
helper = r'''static bool static_record_integer_constant(const MinicC0Program *program,
                                           MinicExpressionId expression_id,
                                           int *value) {
    const MinicExpression *expression;
    int operand;

    if (program == NULL || value == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL || !minic_type_is_integer(expression->type)) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_INTEGER) {
        if (expression->value.integer_value < INT_MIN || expression->value.integer_value > INT_MAX) {
            return false;
        }
        *value = (int)expression->value.integer_value;
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_CAST) {
        return static_record_integer_constant(program, expression->value.unary.operand, value);
    }
    if (expression->kind != MINIC_EXPRESSION_UNARY ||
        !static_record_integer_constant(program, expression->value.unary.operand, &operand)) {
        return false;
    }
    switch (expression->value.unary.operator_kind) {
    case MINIC_UNARY_PLUS:
        *value = operand;
        return true;
    case MINIC_UNARY_NEGATE:
        if (operand == INT_MIN) {
            return false;
        }
        *value = -operand;
        return true;
    case MINIC_UNARY_LOGICAL_NOT:
        *value = operand == 0 ? 1 : 0;
        return true;
    case MINIC_UNARY_BITWISE_NOT:
        *value = ~operand;
        return true;
    default:
        return false;
    }
}

static bool parse_static_local_record_initializer(MinicParser *parser,
                                                  MinicType declared_type,
                                                  MinicSourceSpan name_span) {
    char symbol_name[96];
    const MinicRecord *record;
    MinicGlobalObjectId object_id;
    size_t field_index;
    int symbol_length;

    record = minic_c0_program_record(parser->program, declared_type.record_id);
    if (record == NULL || !record->is_complete || record->is_union) {
        minic_parser_error(parser,
                           "static local record initializer requires a complete struct type");
        return false;
    }
    symbol_length = snprintf(symbol_name,
                             sizeof(symbol_name),
                             "__minic_static_local_%zu_%zu",
                             (size_t)parser->current_function,
                             parser->program->global_object_count);
    if (symbol_length <= 0 || (size_t)symbol_length >= sizeof(symbol_name)) {
        minic_parser_error(parser, "cannot build static local record symbol name");
        return false;
    }
    if (!minic_c0_program_add_global_object(parser->program,
                                            symbol_name,
                                            (size_t)symbol_length,
                                            declared_type,
                                            true,
                                            minic_type_is_const(declared_type),
                                            &object_id) ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after static record") ||
        !minic_parser_expect(parser, MINIC_TOKEN_LBRACE, "expected '{' in static record initializer")) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot begin static local record initializer");
        }
        return false;
    }

    field_index = 0U;
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        const MinicRecordField *field;
        int value;

        if (field_index >= record->field_count) {
            minic_parser_error(parser, "too many static local record initializers");
            return false;
        }
        field = minic_c0_record_field(record, field_index);
        if (field == NULL || field->element_count != 1U || field->is_flexible_array) {
            minic_parser_error(parser, "unsupported static local record field initializer");
            return false;
        }

        value = 0;
        if (parser->current.kind == MINIC_TOKEN_LBRACE) {
            MinicSourceSpan initializer_span;

            if (!minic_type_is_record(field->type) ||
                !parse_zero_aggregate_initializer(parser, &initializer_span)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser,
                                       "nested static record initializer must be all zero");
                }
                return false;
            }
        } else {
            MinicExpressionId value_id;

            if (!minic_type_is_integer(field->type) ||
                !minic_parser_parse_expression(parser, &value_id, 1U) ||
                !static_record_integer_constant(parser->program, value_id, &value)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser,
                                       "static record field requires an integer constant expression");
                }
                return false;
            }
        }
        if (!minic_c0_global_object_add_initializer(parser->program, object_id, value)) {
            minic_parser_error(parser, "cannot record static local record initializer");
            return false;
        }
        field_index += 1U;

        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in static record initializer");
            return false;
        }
    }
    while (field_index < record->field_count) {
        if (!minic_c0_global_object_add_initializer(parser->program, object_id, 0)) {
            minic_parser_error(parser, "cannot zero-fill static local record initializer");
            return false;
        }
        field_index += 1U;
    }
    if (!minic_parser_expect(parser,
                             MINIC_TOKEN_RBRACE,
                             "expected '}' after static record initializer") ||
        !minic_parser_bind_static_local(parser, name_span, object_id)) {
        return false;
    }
    return true;
}

'''
text = replace_once(text, marker, helper + marker, "static local record helper anchor")

start = text.index("    if (bound_count == 0U) {\n", text.index(marker))
end = text.index("    if (parser->current.kind == MINIC_TOKEN_EQUAL) {\n", start)
old_block = text[start:end]
if "static const scalar discovery local" not in old_block:
    raise SystemExit("unexpected static-local scalar block")
new_block = '''    if (bound_count == 0U) {
        MinicLocal local;
        MinicLocalId local_id;
        MinicStatement statement;
        const MinicExpression *initializer;

        if (parser->current.kind != MINIC_TOKEN_EQUAL) {
            minic_parser_error(parser,
                               "static local object currently requires an initializer or fixed array declarator");
            return false;
        }
        if (minic_type_is_record(declared_type)) {
            return parse_static_local_record_initializer(parser, declared_type, name_span);
        }
        if (!minic_type_is_const(declared_type)) {
            minic_parser_error(parser,
                               "static local scalar discovery currently requires const qualification");
            return false;
        }
        (void)memset(&local, 0, sizeof(local));
        local.name_span = name_span;
        local.type = declared_type;
        local.element_count = 1U;
        local.storage_offset = 0U;
        if (!minic_c0_program_add_local(parser->program, &local, &local_id) ||
            !minic_parser_bind_local(parser, name_span, local_id)) {
            minic_parser_error(parser, "cannot add static const scalar discovery local");
            return false;
        }

        (void)memset(&statement, 0, sizeof(statement));
        statement.kind = MINIC_STATEMENT_ASSIGN;
        statement.span.begin = name_span.begin;
        statement.target_expression = MINIC_EXPRESSION_INVALID;
        statement.expression = MINIC_EXPRESSION_INVALID;
        statement.target_statement = MINIC_STATEMENT_INVALID;
        statement.then_block = MINIC_BLOCK_INVALID;
        statement.else_block = MINIC_BLOCK_INVALID;
        if (!add_local_lvalue_expression(
                parser, local_id, name_span, &statement.target_expression) ||
            !minic_parser_advance(parser) ||
            !minic_parser_parse_expression(parser, &statement.expression, 0U) ||
            !apply_assignment_conversion(parser, local.type, &statement.expression)) {
            return false;
        }
        initializer = minic_c0_program_expression(parser->program, statement.expression);
        if (initializer == NULL ||
            !minic_c0_assignment_compatible(parser->program, local.type, statement.expression)) {
            minic_parser_error(parser, "static const scalar initializer type mismatch");
            return false;
        }
        statement.span.end = initializer->span.end;
        return minic_parser_add_statement(parser, &statement);
    }
'''
text = text[:start] + new_block + text[end:]
path.write_text(text)

# Backend: emit direct-field record initializer values using target layout.  A
# zero-valued nested record field consumes its complete storage, so following
# fields stay at ABI-correct offsets without the parser knowing target sizes.
path = Path("src/target/riscv64/codegen_function.c")
text = path.read_text()
marker = "static bool minic_riscv64_emit_global_object(FILE *file,\n"
helper = r'''static bool minic_riscv64_emit_record_values(FILE *file,
                                               const MinicC0Program *program,
                                               const MinicGlobalObject *object) {
    const MinicRecord *record;
    size_t cursor;
    size_t field_index;

    if (file == NULL || program == NULL || object == NULL || !minic_type_is_record(object->type) ||
        object->is_zero_initialized || object->function_relocation_count != 0U ||
        object->object_relocation_count != 0U) {
        return false;
    }
    record = minic_c0_program_record(program, object->type.record_id);
    if (record == NULL || !record->is_complete || record->is_union ||
        object->initializer_count != record->field_count) {
        return false;
    }

    cursor = 0U;
    for (field_index = 0U; field_index < record->field_count; ++field_index) {
        const MinicRecordField *field;
        size_t field_size;
        size_t field_alignment;
        size_t field_offset;
        int value;

        field = minic_c0_record_field(record, field_index);
        if (field == NULL || field->element_count != 1U || field->is_flexible_array ||
            !minic_riscv64_type_layout(program, field->type, &field_size, &field_alignment)) {
            return false;
        }
        (void)field_alignment;
        field_offset = field->storage_offset;
        if (field_offset < cursor || field_offset > object->storage_size ||
            field_size > object->storage_size - field_offset ||
            !minic_riscv64_emit_zero_bytes(file, field_offset - cursor)) {
            return false;
        }
        value = object->initializer_values[field_index];
        if (minic_type_is_integer(field->type)) {
            const char *directive;

            directive = minic_type_is_char_integer(field->type)    ? ".byte"
                        : minic_type_is_short_integer(field->type) ? ".half"
                        : minic_type_is_long_integer(field->type)  ? ".dword"
                                                                   : ".word";
            if (minic_type_is_char_integer(field->type)) {
                unsigned int byte_value;

                byte_value = (unsigned int)value & 0xffU;
                if (fprintf(file, "  %s %u\n", directive, byte_value) < 0) {
                    return false;
                }
            } else if (fprintf(file, "  %s %d\n", directive, value) < 0) {
                return false;
            }
        } else {
            if (value != 0 ||
                (!minic_type_is_record(field->type) && !minic_type_is_pointer(field->type)) ||
                !minic_riscv64_emit_zero_bytes(file, field_size)) {
                return false;
            }
        }
        cursor = field_offset + field_size;
    }
    return cursor <= object->storage_size &&
           minic_riscv64_emit_zero_bytes(file, object->storage_size - cursor);
}

'''
text = replace_once(text, marker, helper + marker, "global emitter anchor")

old = '''    directive = NULL;
    scalar_width = 0U;
    if (object->is_zero_initialized) {
        if (object->initializer_count != 0U) {
            return false;
        }
    } else {
        if (object->function_relocation_count != 0U ||
            !minic_riscv64_global_scalar_type(program, object->type, &scalar_type, &scalar_width) ||
            scalar_width == 0U || object->initializer_count > object->storage_size / scalar_width) {
            return false;
        }
        directive = minic_type_is_char_integer(scalar_type)    ? ".byte"
                    : minic_type_is_short_integer(scalar_type) ? ".half"
                    : minic_type_is_long_integer(scalar_type)  ? ".dword"
                                                               : ".word";
    }
'''
new = '''    directive = NULL;
    scalar_width = 0U;
    if (object->is_zero_initialized) {
        if (object->initializer_count != 0U) {
            return false;
        }
    } else if (minic_type_is_record(object->type)) {
        const MinicRecord *record;

        record = minic_c0_program_record(program, object->type.record_id);
        if (record == NULL || !record->is_complete || record->is_union ||
            object->function_relocation_count != 0U || object->object_relocation_count != 0U ||
            object->initializer_count != record->field_count) {
            return false;
        }
    } else {
        if (object->function_relocation_count != 0U ||
            !minic_riscv64_global_scalar_type(program, object->type, &scalar_type, &scalar_width) ||
            scalar_width == 0U || object->initializer_count > object->storage_size / scalar_width) {
            return false;
        }
        directive = minic_type_is_char_integer(scalar_type)    ? ".byte"
                    : minic_type_is_short_integer(scalar_type) ? ".half"
                    : minic_type_is_long_integer(scalar_type)  ? ".dword"
                                                               : ".word";
    }
'''
text = replace_once(text, old, new, "record initializer validation")

old = '''    } else if (object->is_zero_initialized) {
        if (!minic_riscv64_emit_zero_bytes(file, object->storage_size)) {
            return false;
        }
    } else {
        for (initializer_index = 0U; initializer_index < object->initializer_count;
             ++initializer_index) {
'''
new = '''    } else if (object->is_zero_initialized) {
        if (!minic_riscv64_emit_zero_bytes(file, object->storage_size)) {
            return false;
        }
    } else if (minic_type_is_record(object->type)) {
        if (!minic_riscv64_emit_record_values(file, program, object)) {
            return false;
        }
    } else {
        for (initializer_index = 0U; initializer_index < object->initializer_count;
             ++initializer_index) {
'''
text = replace_once(text, old, new, "record initializer emission")
path.write_text(text)

print("staged static local record aggregate constant initializers")
