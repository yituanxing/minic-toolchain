#!/usr/bin/env python3
from pathlib import Path


def replace_between(path: str, start_marker: str, end_marker: str, replacement: str) -> None:
    target = Path(path)
    text = target.read_text()
    start = text.find(start_marker)
    end = text.find(end_marker, start + len(start_marker)) if start >= 0 else -1
    if start < 0 or end < 0 or text.find(start_marker, start + 1) >= 0:
        raise SystemExit(f"{path}: cannot uniquely replace region {start_marker!r}")
    target.write_text(text[:start] + replacement + text[end:])


replace_between(
    "src/frontend/parser_expression.c",
    "    if (minimum_precedence == 0U && (parser->current.kind == MINIC_TOKEN_PLUS_EQUAL ||\n",
    "    if (minimum_precedence == 0U && parser->current.kind == MINIC_TOKEN_EQUAL) {\n",
    r'''    if (minimum_precedence == 0U &&
        (parser->current.kind == MINIC_TOKEN_PLUS_EQUAL ||
         parser->current.kind == MINIC_TOKEN_MINUS_EQUAL ||
         parser->current.kind == MINIC_TOKEN_STAR_EQUAL ||
         parser->current.kind == MINIC_TOKEN_SLASH_EQUAL ||
         parser->current.kind == MINIC_TOKEN_AMPERSAND_EQUAL ||
         parser->current.kind == MINIC_TOKEN_PIPE_EQUAL ||
         parser->current.kind == MINIC_TOKEN_CARET_EQUAL ||
         parser->current.kind == MINIC_TOKEN_GREATER_GREATER_EQUAL)) {
        const MinicExpression *target_expression;
        const MinicExpression *value_expression;
        MinicExpression assignment;
        MinicExpressionId value_id;
        MinicSourceSpan target_span;
        MinicTokenKind assignment_token;
        MinicType target_type;
        MinicBinaryOperator compound_operator;

        assignment_token = parser->current.kind;
        switch (assignment_token) {
        case MINIC_TOKEN_PLUS_EQUAL:
            compound_operator = MINIC_BINARY_ADD;
            break;
        case MINIC_TOKEN_MINUS_EQUAL:
            compound_operator = MINIC_BINARY_SUBTRACT;
            break;
        case MINIC_TOKEN_STAR_EQUAL:
            compound_operator = MINIC_BINARY_MULTIPLY;
            break;
        case MINIC_TOKEN_SLASH_EQUAL:
            compound_operator = MINIC_BINARY_DIVIDE;
            break;
        case MINIC_TOKEN_AMPERSAND_EQUAL:
            compound_operator = MINIC_BINARY_BITWISE_AND;
            break;
        case MINIC_TOKEN_PIPE_EQUAL:
            compound_operator = MINIC_BINARY_BITWISE_OR;
            break;
        case MINIC_TOKEN_CARET_EQUAL:
            compound_operator = MINIC_BINARY_BITWISE_XOR;
            break;
        case MINIC_TOKEN_GREATER_GREATER_EQUAL:
            compound_operator = MINIC_BINARY_SHIFT_RIGHT;
            break;
        default:
            minic_parser_error(parser, "unsupported compound assignment expression");
            return false;
        }

        target_expression = minic_c0_program_expression(parser->program, left);
        if (target_expression == NULL || target_expression->value_category != MINIC_VALUE_LVALUE ||
            minic_type_is_const(target_expression->type) ||
            minic_type_is_array(target_expression->type) ||
            minic_type_is_function(target_expression->type) ||
            minic_type_is_record(target_expression->type)) {
            minic_parser_error(
                parser, "compound assignment expression requires a modifiable scalar lvalue");
            return false;
        }
        target_span = target_expression->span;
        target_type = target_expression->type;
        if (!minic_parser_advance(parser) ||
            !parse_expression_internal(parser, &value_id, 0U, true)) {
            return false;
        }
        value_expression = minic_c0_program_expression(parser->program, value_id);
        if (value_expression == NULL) {
            minic_parser_error(parser, "invalid compound assignment expression value");
            return false;
        }

        if (minic_type_is_pointer(target_type)) {
            MinicType pointee_type;

            if ((compound_operator != MINIC_BINARY_ADD &&
                 compound_operator != MINIC_BINARY_SUBTRACT) ||
                !minic_type_is_integer(value_expression->type) ||
                !minic_type_pointee(target_type, &pointee_type) ||
                !type_is_complete_object(parser->program, pointee_type)) {
                minic_parser_error(
                    parser,
                    "pointer compound assignment expression requires += or -= with an integer");
                return false;
            }
        } else {
            MinicType common_type;

            if (!minic_type_is_integer(target_type) ||
                !minic_type_is_integer(value_expression->type) ||
                !minic_type_integer_common(target_type, value_expression->type, &common_type)) {
                minic_parser_error(parser, "compound assignment expression requires integer operands");
                return false;
            }
        }

        (void)memset(&assignment, 0, sizeof(assignment));
        assignment.kind = MINIC_EXPRESSION_COMPOUND_ASSIGNMENT;
        assignment.span.begin = target_span.begin;
        assignment.span.end = value_expression->span.end;
        assignment.type = target_type;
        assignment.value_category = MINIC_VALUE_RVALUE;
        assignment.value.binary.operator_kind = compound_operator;
        assignment.value.binary.left = left;
        assignment.value.binary.right = value_id;
        if (!minic_parser_add_expression(parser, &assignment, &left)) {
            return false;
        }
    }
''',
)

replace_between(
    "src/frontend/ast_verifier.c",
    "    case MINIC_EXPRESSION_COMPOUND_ASSIGNMENT: {\n",
    "    case MINIC_EXPRESSION_UNARY: {\n",
    r'''    case MINIC_EXPRESSION_COMPOUND_ASSIGNMENT: {
        MinicType common_type;
        MinicType pointee_type;
        MinicBinaryOperator operator_kind;

        left = expression_before(program, expression->value.binary.left, expression_index);
        right = expression_before(program, expression->value.binary.right, expression_index);
        operator_kind = expression->value.binary.operator_kind;
        if (left == NULL || right == NULL || left->value_category != MINIC_VALUE_LVALUE ||
            expression->value_category != MINIC_VALUE_RVALUE ||
            !minic_type_equal(expression->type, left->type) || minic_type_is_const(left->type)) {
            return false;
        }
        if (minic_type_is_pointer(left->type)) {
            return (operator_kind == MINIC_BINARY_ADD || operator_kind == MINIC_BINARY_SUBTRACT) &&
                   minic_type_is_integer(right->type) &&
                   minic_type_pointee(left->type, &pointee_type) &&
                   type_is_complete_object(program, pointee_type);
        }
        if (operator_kind != MINIC_BINARY_ADD && operator_kind != MINIC_BINARY_SUBTRACT &&
            operator_kind != MINIC_BINARY_MULTIPLY && operator_kind != MINIC_BINARY_DIVIDE &&
            operator_kind != MINIC_BINARY_BITWISE_AND && operator_kind != MINIC_BINARY_BITWISE_OR &&
            operator_kind != MINIC_BINARY_BITWISE_XOR && operator_kind != MINIC_BINARY_SHIFT_RIGHT) {
            return false;
        }
        return minic_type_is_integer(left->type) && minic_type_is_integer(right->type) &&
               minic_type_integer_common(left->type, right->type, &common_type);
    }
''',
)

replace_between(
    "src/target/riscv64/codegen_expression.c",
    "    case MINIC_EXPRESSION_COMPOUND_ASSIGNMENT: {\n",
    "    case MINIC_EXPRESSION_UNARY:\n",
    r'''    case MINIC_EXPRESSION_COMPOUND_ASSIGNMENT: {
        const MinicExpression *target;
        const MinicExpression *value;
        MinicBinaryOperator operator_kind;

        target = minic_c0_program_expression(program, expression->value.binary.left);
        value = minic_c0_program_expression(program, expression->value.binary.right);
        operator_kind = expression->value.binary.operator_kind;
        if (target == NULL || value == NULL || target->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_equal(expression->type, target->type) ||
            !minic_riscv64_emit_lvalue_address(
                file, program, function, expression->value.binary.left) ||
            fprintf(file, "  addi sp, sp, -32\n  sd a0, 0(sp)\n") < 0 ||
            !minic_riscv64_emit_scalar_load(file, target->type, "a0", "a0")) {
            return false;
        }
        if (minic_type_is_pointer(target->type)) {
            size_t element_size;

            if ((operator_kind != MINIC_BINARY_ADD && operator_kind != MINIC_BINARY_SUBTRACT) ||
                !minic_type_is_integer(value->type) ||
                !minic_riscv64_pointer_element_size(program, target->type, &element_size) ||
                fprintf(file, "  sd a0, 8(sp)\n") < 0 ||
                !minic_riscv64_emit_expression(
                    file, program, function, expression->value.binary.right) ||
                !minic_riscv64_emit_scale_register(file, "a0", "t0", element_size) ||
                fprintf(file,
                        "  ld t0, 8(sp)\n"
                        "  %s a0, t0, a0\n",
                        operator_kind == MINIC_BINARY_ADD ? "add" : "sub") < 0) {
                return false;
            }
        } else {
            MinicType common_type;
            const char *opcode;

            if (!minic_type_is_integer(target->type) || !minic_type_is_integer(value->type) ||
                !minic_type_integer_common(target->type, value->type, &common_type) ||
                !minic_riscv64_emit_normalize_integer(file, common_type, "a0") ||
                fprintf(file, "  sd a0, 8(sp)\n") < 0 ||
                !minic_riscv64_emit_expression(
                    file, program, function, expression->value.binary.right) ||
                !minic_riscv64_emit_normalize_integer(file, common_type, "a0")) {
                return false;
            }
            switch (operator_kind) {
            case MINIC_BINARY_ADD:
                opcode = minic_type_is_long_integer(common_type) ? "add" : "addw";
                break;
            case MINIC_BINARY_SUBTRACT:
                opcode = minic_type_is_long_integer(common_type) ? "sub" : "subw";
                break;
            case MINIC_BINARY_MULTIPLY:
                opcode = minic_type_is_long_integer(common_type) ? "mul" : "mulw";
                break;
            case MINIC_BINARY_DIVIDE:
                if (minic_type_is_unsigned_integer(common_type)) {
                    opcode = minic_type_is_long_integer(common_type) ? "divu" : "divuw";
                } else {
                    opcode = minic_type_is_long_integer(common_type) ? "div" : "divw";
                }
                break;
            case MINIC_BINARY_BITWISE_AND:
                opcode = "and";
                break;
            case MINIC_BINARY_BITWISE_OR:
                opcode = "or";
                break;
            case MINIC_BINARY_BITWISE_XOR:
                opcode = "xor";
                break;
            case MINIC_BINARY_SHIFT_RIGHT:
                if (minic_type_is_unsigned_integer(common_type)) {
                    opcode = minic_type_is_long_integer(common_type) ? "srl" : "srlw";
                } else {
                    opcode = minic_type_is_long_integer(common_type) ? "sra" : "sraw";
                }
                break;
            default:
                return false;
            }
            if (fprintf(file,
                        "  ld t0, 8(sp)\n"
                        "  %s a0, t0, a0\n",
                        opcode) < 0 ||
                !minic_riscv64_emit_integer_conversion(file, target->type, "a0")) {
                return false;
            }
        }
        return fprintf(file,
                       "  mv t0, a0\n"
                       "  ld t1, 0(sp)\n"
                       "  addi sp, sp, 32\n") >= 0 &&
               minic_riscv64_emit_scalar_store(file, target->type, "t0", "t1") &&
               fprintf(file, "  mv a0, t0\n") >= 0;
    }
''',
)

print("staged full compound assignment expressions")
