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

print("staged bitwise OR, >>=, pointer +=, and value-context postfix updates")
