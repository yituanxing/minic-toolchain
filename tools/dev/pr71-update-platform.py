#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file = Path(path)
    text = file.read_text()
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one replacement, found {count}: {old[:80]!r}")
    file.write_text(text.replace(old, new, 1))


# >>= token; bitwise | already has a token but was not part of expression precedence/AST semantics.
replace_once(
    "src/frontend/token.h",
    "    MINIC_TOKEN_GREATER_GREATER,\n    MINIC_TOKEN_GREATER_EQUAL,\n",
    "    MINIC_TOKEN_GREATER_GREATER,\n    MINIC_TOKEN_GREATER_GREATER_EQUAL,\n    MINIC_TOKEN_GREATER_EQUAL,\n",
)
replace_once(
    "src/frontend/token.c",
    '    case MINIC_TOKEN_GREATER_GREATER:\n        return ">>";\n    case MINIC_TOKEN_GREATER_EQUAL:\n',
    '    case MINIC_TOKEN_GREATER_GREATER:\n        return ">>";\n'
    '    case MINIC_TOKEN_GREATER_GREATER_EQUAL:\n        return ">>=";\n'
    '    case MINIC_TOKEN_GREATER_EQUAL:\n',
)
replace_once(
    "src/frontend/lexer.c",
    """    case '>':
        if (minic_lexer_peek_next(lexer) == '>') {
            token->kind = MINIC_TOKEN_GREATER_GREATER;
            minic_lexer_advance(lexer);
        } else if (minic_lexer_peek_next(lexer) == '=') {
            token->kind = MINIC_TOKEN_GREATER_EQUAL;
            minic_lexer_advance(lexer);
        } else {
            token->kind = MINIC_TOKEN_GREATER;
        }
        break;
""",
    """    case '>':
        if (minic_lexer_peek_next(lexer) == '>' && lexer->cursor + 2U < lexer->length &&
            lexer->source[lexer->cursor + 2U] == '=') {
            token->kind = MINIC_TOKEN_GREATER_GREATER_EQUAL;
            minic_lexer_advance(lexer);
            minic_lexer_advance(lexer);
        } else if (minic_lexer_peek_next(lexer) == '>') {
            token->kind = MINIC_TOKEN_GREATER_GREATER;
            minic_lexer_advance(lexer);
        } else if (minic_lexer_peek_next(lexer) == '=') {
            token->kind = MINIC_TOKEN_GREATER_EQUAL;
            minic_lexer_advance(lexer);
        } else {
            token->kind = MINIC_TOKEN_GREATER;
        }
        break;
""",
)

# Give bitwise OR its real AST operator and C precedence between ^ and &&.
replace_once(
    "src/frontend/ast.h",
    "    MINIC_BINARY_BITWISE_AND,\n    MINIC_BINARY_BITWISE_XOR,\n    MINIC_BINARY_EQUAL,\n",
    "    MINIC_BINARY_BITWISE_AND,\n    MINIC_BINARY_BITWISE_XOR,\n"
    "    MINIC_BINARY_BITWISE_OR,\n    MINIC_BINARY_EQUAL,\n",
)
replace_once(
    "src/frontend/parser_expression.c",
    """    case MINIC_TOKEN_CARET:
        return 10U;
    case MINIC_TOKEN_AMPERSAND_AMPERSAND:
        return 2U;
""",
    """    case MINIC_TOKEN_CARET:
        return 10U;
    case MINIC_TOKEN_PIPE:
        return 5U;
    case MINIC_TOKEN_AMPERSAND_AMPERSAND:
        return 2U;
""",
)
replace_once(
    "src/frontend/parser_expression.c",
    """    case MINIC_TOKEN_CARET:
        return MINIC_BINARY_BITWISE_XOR;
    case MINIC_TOKEN_EQUAL_EQUAL:
""",
    """    case MINIC_TOKEN_CARET:
        return MINIC_BINARY_BITWISE_XOR;
    case MINIC_TOKEN_PIPE:
        return MINIC_BINARY_BITWISE_OR;
    case MINIC_TOKEN_EQUAL_EQUAL:
""",
)

# Fast discovery lowering for += and >>= keeps existing assignment IR. Pointer += is accepted
# when the pointee is a complete object type; >>= uses the promoted left type.
replace_once(
    "src/frontend/parser_statement.c",
    """    if (assignment_token != MINIC_TOKEN_EQUAL && assignment_token != MINIC_TOKEN_CARET_EQUAL &&
        assignment_token != MINIC_TOKEN_PLUS_EQUAL) {
""",
    """    if (assignment_token != MINIC_TOKEN_EQUAL && assignment_token != MINIC_TOKEN_CARET_EQUAL &&
        assignment_token != MINIC_TOKEN_PLUS_EQUAL &&
        assignment_token != MINIC_TOKEN_GREATER_GREATER_EQUAL) {
""",
)
replace_once(
    "src/frontend/parser_statement.c",
    """        if (right_expression == NULL || !minic_type_is_integer(first_type) ||
            !minic_type_is_integer(right_expression->type) ||
            !minic_type_integer_common(first_type, right_expression->type, &common_type)) {
            minic_parser_error(parser, "compound addition assignment requires integer operands");
            return false;
        }
""",
    """        if (right_expression == NULL || !minic_type_is_integer(right_expression->type)) {
            minic_parser_error(parser, "compound addition assignment requires pointer/integer or integer operands");
            return false;
        }
        if (minic_type_is_pointer(first_type)) {
            MinicType pointee_type;

            if (!minic_type_pointee(first_type, &pointee_type) ||
                !minic_parser_require_complete_object_type(
                    parser, pointee_type, "pointer update requires a complete object type")) {
                return false;
            }
            common_type = first_type;
        } else if (!minic_type_is_integer(first_type) ||
                   !minic_type_integer_common(first_type, right_expression->type, &common_type)) {
            minic_parser_error(parser, "compound addition assignment requires pointer/integer or integer operands");
            return false;
        }
""",
)
replace_once(
    "src/frontend/parser_statement.c",
    """        if (!minic_parser_add_expression(parser, &addition, &statement.expression)) {
            return false;
        }
    }
    if (statement.kind == MINIC_STATEMENT_ASSIGN &&
""",
    """        if (!minic_parser_add_expression(parser, &addition, &statement.expression)) {
            return false;
        }
    } else if (assignment_token == MINIC_TOKEN_GREATER_GREATER_EQUAL) {
        const MinicExpression *right_expression;
        MinicExpression shift;
        MinicExpressionId right_id;

        right_id = statement.expression;
        right_expression = minic_c0_program_expression(parser->program, right_id);
        if (right_expression == NULL || !minic_type_is_integer(first_type) ||
            !minic_type_is_integer(right_expression->type)) {
            minic_parser_error(parser, "compound right shift assignment requires integer operands");
            return false;
        }
        (void)memset(&shift, 0, sizeof(shift));
        shift.kind = MINIC_EXPRESSION_BINARY;
        shift.span.begin = statement.span.begin;
        shift.span.end = right_expression->span.end;
        shift.value_category = MINIC_VALUE_RVALUE;
        shift.value.binary.operator_kind = MINIC_BINARY_SHIFT_RIGHT;
        shift.value.binary.left = statement.target_expression;
        shift.value.binary.right = right_id;
        if (!minic_type_integer_common(first_type, first_type, &shift.type) ||
            !minic_parser_add_expression(parser, &shift, &statement.expression)) {
            return false;
        }
    }
    if (statement.kind == MINIC_STATEMENT_ASSIGN &&
""",
)

# RV64 bitwise OR.
replace_once(
    "src/target/riscv64/codegen_expression.c",
    """        case MINIC_BINARY_BITWISE_XOR:
            return has_integer_common_type && fprintf(file, "  xor a0, t0, a0\\n") >= 0 &&
                   minic_riscv64_emit_integer_result_conversion(
                       file, common_integer_type, expression->type, "a0");
        case MINIC_BINARY_EQUAL:
""",
    """        case MINIC_BINARY_BITWISE_XOR:
            return has_integer_common_type && fprintf(file, "  xor a0, t0, a0\\n") >= 0 &&
                   minic_riscv64_emit_integer_result_conversion(
                       file, common_integer_type, expression->type, "a0");
        case MINIC_BINARY_BITWISE_OR:
            return has_integer_common_type && fprintf(file, "  or a0, t0, a0\\n") >= 0 &&
                   minic_riscv64_emit_integer_result_conversion(
                       file, common_integer_type, expression->type, "a0");
        case MINIC_BINARY_EQUAL:
""",
)

# Postfix updates in true value context: evaluate the lvalue address once, store the updated
# scalar, and leave the old scalar value in a0. This covers both integers and pointers.
postfix_helper = r'''static bool minic_riscv64_emit_postfix_update(FILE *file,
                                                 const MinicC0Program *program,
                                                 const MinicFunction *function,
                                                 const MinicExpression *expression) {
    const MinicExpression *operand;
    size_t element_size;
    bool increment;

    if (expression == NULL || expression->kind != MINIC_EXPRESSION_UNARY ||
        (expression->value.unary.operator_kind != MINIC_UNARY_POST_INCREMENT &&
         expression->value.unary.operator_kind != MINIC_UNARY_POST_DECREMENT)) {
        return false;
    }
    operand = minic_c0_program_expression(program, expression->value.unary.operand);
    if (operand == NULL || operand->value_category != MINIC_VALUE_LVALUE ||
        (!minic_type_is_integer(operand->type) && !minic_type_is_pointer(operand->type))) {
        return false;
    }
    increment = expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT;
    element_size = 1U;
    if (minic_type_is_pointer(operand->type) &&
        !minic_riscv64_pointer_element_size(program, operand->type, &element_size)) {
        return false;
    }

    if (!minic_riscv64_emit_lvalue_address(
            file, program, function, expression->value.unary.operand) ||
        fprintf(file, "  addi sp, sp, -16\n  sd a0, 0(sp)\n") < 0 ||
        !minic_riscv64_emit_scalar_load(file, operand->type, "t0", "a0") ||
        fprintf(file, "  sd t0, 8(sp)\n") < 0) {
        return false;
    }
    if (minic_type_is_pointer(operand->type)) {
        if (element_size <= 2047U) {
            if (fprintf(file,
                        increment ? "  addi t0, t0, %zu\n" : "  addi t0, t0, -%zu\n",
                        element_size) < 0) {
                return false;
            }
        } else if (fprintf(file,
                           "  li t1, %zu\n"
                           "  %s t0, t0, t1\n",
                           element_size,
                           increment ? "add" : "sub") < 0) {
            return false;
        }
    } else if (fprintf(file, increment ? "  addi t0, t0, 1\n" : "  addi t0, t0, -1\n") < 0 ||
               !minic_riscv64_emit_integer_conversion(file, operand->type, "t0")) {
        return false;
    }
    return fprintf(file, "  ld t1, 0(sp)\n") >= 0 &&
           minic_riscv64_emit_scalar_store(file, operand->type, "t0", "t1") &&
           fprintf(file, "  ld a0, 8(sp)\n  addi sp, sp, 16\n") >= 0;
}

'''
replace_once(
    "src/target/riscv64/codegen_expression.c",
    "static bool minic_riscv64_emit_subscript_address(FILE *file,\n",
    postfix_helper + "static bool minic_riscv64_emit_subscript_address(FILE *file,\n",
)
replace_once(
    "src/target/riscv64/codegen_expression.c",
    """    case MINIC_EXPRESSION_UNARY:
        if (!minic_riscv64_emit_expression(
                file, program, function, expression->value.unary.operand)) {
            return false;
        }
""",
    """    case MINIC_EXPRESSION_UNARY:
        if (expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT ||
            expression->value.unary.operator_kind == MINIC_UNARY_POST_DECREMENT) {
            return minic_riscv64_emit_postfix_update(file, program, function, expression);
        }
        if (!minic_riscv64_emit_expression(
                file, program, function, expression->value.unary.operand)) {
            return false;
        }
""",
)

# cJSON next uses a nested all-zero record initializer. During discovery, accept only braces
# whose leaves are literal integer zero, then lower the record to ordinary scalar assignments.
record_zero_helpers = r'''static bool parse_zero_aggregate_initializer(MinicParser *parser,
                                             MinicSourceSpan *initializer_span) {
    MinicSourcePosition begin;
    bool saw_value;

    if (parser == NULL || initializer_span == NULL ||
        parser->current.kind != MINIC_TOKEN_LBRACE) {
        minic_parser_error(parser, "expected aggregate zero initializer");
        return false;
    }
    begin = parser->current.span.begin;
    saw_value = false;
    if (!minic_parser_advance(parser)) {
        return false;
    }
    for (;;) {
        if (parser->current.kind == MINIC_TOKEN_RBRACE) {
            if (!saw_value) {
                minic_parser_error(parser, "empty aggregate initializer is unsupported");
                return false;
            }
            initializer_span->begin = begin;
            initializer_span->end = parser->current.span.end;
            return minic_parser_advance(parser);
        }
        if (parser->current.kind == MINIC_TOKEN_LBRACE) {
            MinicSourceSpan nested_span;

            if (!parse_zero_aggregate_initializer(parser, &nested_span)) {
                return false;
            }
        } else if (parser->current.kind == MINIC_TOKEN_INTEGER_CONSTANT) {
            int value;

            if (!minic_parser_parse_integer_value(parser, &value) || value != 0) {
                minic_parser_error(parser, "only all-zero aggregate initializers are supported");
                return false;
            }
        } else {
            minic_parser_error(parser, "only all-zero aggregate initializers are supported");
            return false;
        }
        saw_value = true;
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            continue;
        }
        if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in aggregate initializer");
            return false;
        }
    }
}

static bool add_zero_assignment_to_lvalue(MinicParser *parser,
                                          MinicExpressionId target_id,
                                          MinicSourceSpan initializer_span) {
    const MinicExpression *target;
    MinicExpression zero;
    MinicExpressionId value_id;
    MinicStatement statement;

    target = minic_c0_program_expression(parser->program, target_id);
    if (target == NULL || target->value_category != MINIC_VALUE_LVALUE) {
        minic_parser_error(parser, "invalid aggregate zero target");
        return false;
    }
    (void)memset(&zero, 0, sizeof(zero));
    zero.kind = MINIC_EXPRESSION_INTEGER;
    zero.span = initializer_span;
    zero.type = minic_type_int();
    zero.value_category = MINIC_VALUE_RVALUE;
    zero.value.integer_value = 0;
    if (!minic_parser_add_expression(parser, &zero, &value_id) ||
        !apply_assignment_conversion(parser, target->type, &value_id) ||
        !minic_c0_assignment_compatible(parser->program, target->type, value_id)) {
        minic_parser_error(parser, "aggregate zero initializer does not match member type");
        return false;
    }

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_ASSIGN;
    statement.span = initializer_span;
    statement.target_expression = target_id;
    statement.expression = value_id;
    statement.target_statement = MINIC_STATEMENT_INVALID;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;
    return minic_parser_add_statement(parser, &statement);
}

static bool add_zero_initialized_record_lvalue(MinicParser *parser,
                                               MinicExpressionId base_id,
                                               MinicSourceSpan initializer_span) {
    const MinicExpression *base;
    const MinicRecord *record;
    MinicExpression address;
    MinicExpressionId address_id;
    size_t field_index;

    base = minic_c0_program_expression(parser->program, base_id);
    if (base == NULL || base->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_record(base->type)) {
        minic_parser_error(parser, "aggregate zero initializer requires a record lvalue");
        return false;
    }
    record = minic_c0_program_record(parser->program, base->type.record_id);
    if (record == NULL || !record->is_complete) {
        minic_parser_error(parser, "aggregate zero initializer requires a complete record");
        return false;
    }

    (void)memset(&address, 0, sizeof(address));
    address.kind = MINIC_EXPRESSION_ADDRESS_OF;
    address.span = base->span;
    if (!minic_type_pointer_to(base->type, &address.type)) {
        minic_parser_error(parser, "record initializer address depth is unsupported");
        return false;
    }
    address.value_category = MINIC_VALUE_RVALUE;
    address.value.unary.operand = base_id;
    if (!minic_parser_add_expression(parser, &address, &address_id)) {
        return false;
    }

    for (field_index = 0U; field_index < record->field_count; ++field_index) {
        const MinicRecordField *field;
        MinicExpression member;
        MinicExpressionId member_id;

        field = minic_c0_record_field(record, field_index);
        if (field == NULL || field->element_count != 1U) {
            minic_parser_error(parser, "record array members in aggregate initialization are unsupported");
            return false;
        }
        (void)memset(&member, 0, sizeof(member));
        member.kind = MINIC_EXPRESSION_MEMBER;
        member.span = initializer_span;
        member.type = field->type;
        member.value_category = MINIC_VALUE_LVALUE;
        member.value.member.base = address_id;
        member.value.member.record_id = base->type.record_id;
        member.value.member.field_index = field_index;
        if (!minic_parser_add_expression(parser, &member, &member_id)) {
            return false;
        }
        if (minic_type_is_record(field->type)) {
            if (!add_zero_initialized_record_lvalue(parser, member_id, initializer_span)) {
                return false;
            }
        } else if (!add_zero_assignment_to_lvalue(parser, member_id, initializer_span)) {
            return false;
        }
    }
    return true;
}

'''
replace_once(
    "src/frontend/parser_statement.c",
    "static bool parse_local_declarator(MinicParser *parser, MinicType base_type) {\n",
    record_zero_helpers + "static bool parse_local_declarator(MinicParser *parser, MinicType base_type) {\n",
)
replace_once(
    "src/frontend/parser_statement.c",
    """        if (local.element_count != 1U) {
            return parse_local_array_zero_initializer(parser, local_id, local.name_span);
        }
        (void)memset(&statement, 0, sizeof(statement));
""",
    """        if (local.element_count != 1U) {
            return parse_local_array_zero_initializer(parser, local_id, local.name_span);
        }
        if (minic_type_is_record(local.type)) {
            MinicExpressionId target_id;
            MinicSourceSpan initializer_span;

            if (!add_local_lvalue_expression(parser, local_id, local.name_span, &target_id) ||
                !minic_parser_advance(parser) ||
                !parse_zero_aggregate_initializer(parser, &initializer_span) ||
                !add_zero_initialized_record_lvalue(parser, target_id, initializer_span)) {
                return false;
            }
            return true;
        }
        (void)memset(&statement, 0, sizeof(statement));
""",
)

print("staged bitwise OR, >>=, pointer +=, value-context postfix, and record zero init")
