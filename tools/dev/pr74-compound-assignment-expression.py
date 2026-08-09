#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:140]!r}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/ast.h",
    """    MINIC_EXPRESSION_LVALUE_READ,
    MINIC_EXPRESSION_ASSIGNMENT,
    MINIC_EXPRESSION_UNARY,
""",
    """    MINIC_EXPRESSION_LVALUE_READ,
    MINIC_EXPRESSION_ASSIGNMENT,
    MINIC_EXPRESSION_COMPOUND_ASSIGNMENT,
    MINIC_EXPRESSION_UNARY,
""",
)

# Parse += as a true expression. The target is retained as an lvalue so the backend can
# evaluate its address exactly once; do not lower it to x = x + y in the frontend.
replace_once(
    "src/frontend/parser_expression.c",
    """    if (minimum_precedence == 0U && parser->current.kind == MINIC_TOKEN_EQUAL) {
""",
    """    if (minimum_precedence == 0U && parser->current.kind == MINIC_TOKEN_PLUS_EQUAL) {
        const MinicExpression *target_expression;
        const MinicExpression *value_expression;
        MinicExpression assignment;
        MinicExpressionId value_id;
        MinicSourceSpan target_span;
        MinicType target_type;

        target_expression = minic_c0_program_expression(parser->program, left);
        if (target_expression == NULL || target_expression->value_category != MINIC_VALUE_LVALUE ||
            minic_type_is_const(target_expression->type) ||
            minic_type_is_array(target_expression->type) ||
            minic_type_is_function(target_expression->type) ||
            minic_type_is_record(target_expression->type)) {
            minic_parser_error(parser,
                               \"compound assignment expression requires a modifiable scalar lvalue\");
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
            minic_parser_error(parser, \"invalid compound assignment expression value\");
            return false;
        }
        if (minic_type_is_pointer(target_type)) {
            MinicType pointee_type;

            if (!minic_type_is_integer(value_expression->type) ||
                !minic_type_pointee(target_type, &pointee_type) ||
                !type_is_complete_object(parser->program, pointee_type)) {
                minic_parser_error(
                    parser,
                    \"compound addition assignment requires pointer/integer or integer operands\");
                return false;
            }
        } else {
            MinicType common_type;

            if (!minic_type_is_integer(target_type) ||
                !minic_type_is_integer(value_expression->type) ||
                !minic_type_integer_common(target_type, value_expression->type, &common_type)) {
                minic_parser_error(
                    parser,
                    \"compound addition assignment requires pointer/integer or integer operands\");
                return false;
            }
        }

        (void)memset(&assignment, 0, sizeof(assignment));
        assignment.kind = MINIC_EXPRESSION_COMPOUND_ASSIGNMENT;
        assignment.span.begin = target_span.begin;
        assignment.span.end = value_expression->span.end;
        assignment.type = target_type;
        assignment.value_category = MINIC_VALUE_RVALUE;
        assignment.value.binary.operator_kind = MINIC_BINARY_ADD;
        assignment.value.binary.left = left;
        assignment.value.binary.right = value_id;
        if (!minic_parser_add_expression(parser, &assignment, &left)) {
            return false;
        }
    }
    if (minimum_precedence == 0U && parser->current.kind == MINIC_TOKEN_EQUAL) {
""",
)

replace_once(
    "src/frontend/cast_normalization.c",
    """    case MINIC_EXPRESSION_ASSIGNMENT:
    case MINIC_EXPRESSION_BINARY:
""",
    """    case MINIC_EXPRESSION_ASSIGNMENT:
    case MINIC_EXPRESSION_COMPOUND_ASSIGNMENT:
    case MINIC_EXPRESSION_BINARY:
""",
)

# Normalized AST contract: target is a modifiable scalar lvalue; += accepts integer/integer
# or pointer/integer and the expression result has the target type.
replace_once(
    "src/frontend/ast_verifier.c",
    """    case MINIC_EXPRESSION_ASSIGNMENT:
        left = expression_before(program, expression->value.binary.left, expression_index);
        right = expression_before(program, expression->value.binary.right, expression_index);
        return left != NULL && right != NULL && left->value_category == MINIC_VALUE_LVALUE &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_equal(expression->type, left->type) && !minic_type_is_const(left->type) &&
               !minic_type_is_array(left->type) && !minic_type_is_function(left->type) &&
               !minic_type_is_record(left->type) &&
               minic_c0_assignment_compatible(program, left->type, expression->value.binary.right);
""",
    """    case MINIC_EXPRESSION_ASSIGNMENT:
        left = expression_before(program, expression->value.binary.left, expression_index);
        right = expression_before(program, expression->value.binary.right, expression_index);
        return left != NULL && right != NULL && left->value_category == MINIC_VALUE_LVALUE &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_equal(expression->type, left->type) && !minic_type_is_const(left->type) &&
               !minic_type_is_array(left->type) && !minic_type_is_function(left->type) &&
               !minic_type_is_record(left->type) &&
               minic_c0_assignment_compatible(program, left->type, expression->value.binary.right);
    case MINIC_EXPRESSION_COMPOUND_ASSIGNMENT: {
        MinicType common_type;

        left = expression_before(program, expression->value.binary.left, expression_index);
        right = expression_before(program, expression->value.binary.right, expression_index);
        if (left == NULL || right == NULL || left->value_category != MINIC_VALUE_LVALUE ||
            expression->value_category != MINIC_VALUE_RVALUE ||
            !minic_type_equal(expression->type, left->type) || minic_type_is_const(left->type) ||
            expression->value.binary.operator_kind != MINIC_BINARY_ADD) {
            return false;
        }
        if (minic_type_is_pointer(left->type)) {
            return minic_type_is_integer(right->type);
        }
        return minic_type_is_integer(left->type) && minic_type_is_integer(right->type) &&
               minic_type_integer_common(left->type, right->type, &common_type);
    }
""",
)

# RV64 lowering preserves single evaluation of the lvalue address. Integer += performs the
# operation in the usual common type then narrows back to the target; pointer += scales RHS.
replace_once(
    "src/target/riscv64/codegen_expression.c",
    """    case MINIC_EXPRESSION_UNARY:
""",
    """    case MINIC_EXPRESSION_COMPOUND_ASSIGNMENT: {
        const MinicExpression *target;
        const MinicExpression *value;

        target = minic_c0_program_expression(program, expression->value.binary.left);
        value = minic_c0_program_expression(program, expression->value.binary.right);
        if (target == NULL || value == NULL || target->value_category != MINIC_VALUE_LVALUE ||
            expression->value.binary.operator_kind != MINIC_BINARY_ADD ||
            !minic_type_equal(expression->type, target->type) ||
            !minic_riscv64_emit_lvalue_address(
                file, program, function, expression->value.binary.left) ||
            fprintf(file, \"  addi sp, sp, -32\\n  sd a0, 0(sp)\\n\") < 0 ||
            !minic_riscv64_emit_scalar_load(file, target->type, \"a0\", \"a0\")) {
            return false;
        }
        if (minic_type_is_pointer(target->type)) {
            size_t element_size;

            if (!minic_type_is_integer(value->type) ||
                !minic_riscv64_pointer_element_size(program, target->type, &element_size) ||
                fprintf(file, \"  sd a0, 8(sp)\\n\") < 0 ||
                !minic_riscv64_emit_expression(
                    file, program, function, expression->value.binary.right) ||
                !minic_riscv64_emit_scale_register(file, \"a0\", \"t0\", element_size) ||
                fprintf(file,
                        \"  ld t0, 8(sp)\\n\"
                        \"  add a0, t0, a0\\n\") < 0) {
                return false;
            }
        } else {
            MinicType common_type;

            if (!minic_type_is_integer(target->type) || !minic_type_is_integer(value->type) ||
                !minic_type_integer_common(target->type, value->type, &common_type) ||
                !minic_riscv64_emit_normalize_integer(file, common_type, \"a0\") ||
                fprintf(file, \"  sd a0, 8(sp)\\n\") < 0 ||
                !minic_riscv64_emit_expression(
                    file, program, function, expression->value.binary.right) ||
                !minic_riscv64_emit_normalize_integer(file, common_type, \"a0\") ||
                fprintf(file,
                        \"  ld t0, 8(sp)\\n\"
                        \"  %s a0, t0, a0\\n\",
                        minic_type_is_long_integer(common_type) ? \"add\" : \"addw\") < 0 ||
                !minic_riscv64_emit_integer_conversion(file, target->type, \"a0\")) {
                return false;
            }
        }
        return fprintf(file,
                       \"  mv t0, a0\\n\"
                       \"  ld t1, 0(sp)\\n\"
                       \"  addi sp, sp, 32\\n\") >= 0 &&
               minic_riscv64_emit_scalar_store(file, target->type, \"t0\", \"t1\") &&
               fprintf(file, \"  mv a0, t0\\n\") >= 0;
    }
    case MINIC_EXPRESSION_UNARY:
""",
)

print("staged += compound assignment expressions with single-evaluation RV64 lowering")
