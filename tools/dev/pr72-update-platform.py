#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    if old not in text:
        raise SystemExit(f"missing expected text in {path}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/target/riscv64/codegen_expression.c",
    """    case MINIC_EXPRESSION_GLOBAL_OBJECT:
        return false;
""",
    """    case MINIC_EXPRESSION_GLOBAL_OBJECT: {
        const MinicGlobalObject *object;

        object = minic_c0_program_global_object(program, expression->value.global_object_id);
        if (object == NULL || object->name_length == 0U || minic_type_is_array(object->type) ||
            minic_type_is_record(object->type) ||
            fprintf(file, "  la a0, %s\\n", object->name) < 0) {
            return false;
        }
        return minic_riscv64_emit_scalar_load(file, object->type, "a0", "a0");
    }
""",
)

replace_once(
    "src/frontend/parser_expression.c",
    """    *expression_id = left;
    return true;
}

bool minic_parser_parse_expression(MinicParser *parser,
""",
    """    if (minimum_precedence == 0U && parser->current.kind == MINIC_TOKEN_EQUAL) {
        const MinicExpression *target_expression;
        const MinicExpression *value_expression;
        MinicExpression assignment;
        MinicExpressionId value_id;
        MinicSourceSpan target_span;
        MinicType target_type;

        target_expression = minic_c0_program_expression(parser->program, left);
        if (target_expression == NULL ||
            target_expression->value_category != MINIC_VALUE_LVALUE ||
            minic_type_is_const(target_expression->type) ||
            minic_type_is_array(target_expression->type) ||
            minic_type_is_function(target_expression->type) ||
            minic_type_is_record(target_expression->type)) {
            minic_parser_error(parser, "assignment expression requires a modifiable scalar lvalue");
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
            minic_parser_error(parser, "invalid assignment expression value");
            return false;
        }
        if (!minic_c0_assignment_compatible(parser->program, target_type, value_id)) {
            if (!minic_type_cast_compatible(target_type, value_expression->type) ||
                !parser_add_cast(parser, value_id, target_type, &value_id)) {
                minic_parser_error(parser, "assignment expression type does not match target type");
                return false;
            }
        }
        value_expression = minic_c0_program_expression(parser->program, value_id);
        if (value_expression == NULL ||
            !minic_c0_assignment_compatible(parser->program, target_type, value_id)) {
            minic_parser_error(parser, "assignment expression conversion failed");
            return false;
        }

        (void)memset(&assignment, 0, sizeof(assignment));
        assignment.kind = MINIC_EXPRESSION_ASSIGNMENT;
        assignment.span.begin = target_span.begin;
        assignment.span.end = value_expression->span.end;
        assignment.type = target_type;
        assignment.value_category = MINIC_VALUE_RVALUE;
        assignment.value.binary.left = left;
        assignment.value.binary.right = value_id;
        if (!minic_parser_add_expression(parser, &assignment, &left)) {
            return false;
        }
    }
    *expression_id = left;
    return true;
}

bool minic_parser_parse_expression(MinicParser *parser,
""",
)

replace_once(
    "src/frontend/parser_statement.c",
    """        if (!allow_expression_statement) {
            minic_parser_error(parser, "for initializer requires an assignment");
            return false;
        }
""",
    """        if (!allow_expression_statement &&
            first_expression->kind != MINIC_EXPRESSION_ASSIGNMENT) {
            minic_parser_error(parser, "for initializer requires an assignment");
            return false;
        }
""",
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    """    case MINIC_EXPRESSION_LVALUE_READ:
        return minic_riscv64_emit_lvalue_address(
                   file, program, function, expression->value.unary.operand) &&
               minic_riscv64_emit_scalar_load(file, expression->type, "a0", "a0");
    case MINIC_EXPRESSION_UNARY:
""",
    """    case MINIC_EXPRESSION_LVALUE_READ:
        return minic_riscv64_emit_lvalue_address(
                   file, program, function, expression->value.unary.operand) &&
               minic_riscv64_emit_scalar_load(file, expression->type, "a0", "a0");
    case MINIC_EXPRESSION_ASSIGNMENT: {
        const MinicExpression *target;
        const MinicExpression *value;

        target = minic_c0_program_expression(program, expression->value.binary.left);
        value = minic_c0_program_expression(program, expression->value.binary.right);
        if (target == NULL || value == NULL || target->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_equal(expression->type, target->type) ||
            !minic_c0_assignment_compatible(
                program, target->type, expression->value.binary.right) ||
            !minic_riscv64_emit_lvalue_address(
                file, program, function, expression->value.binary.left) ||
            fprintf(file, "  addi sp, sp, -16\\n  sd a0, 0(sp)\\n") < 0 ||
            !minic_riscv64_emit_expression(
                file, program, function, expression->value.binary.right)) {
            return false;
        }
        if (minic_type_is_integer(target->type) &&
            !minic_riscv64_emit_integer_conversion(file, target->type, "a0")) {
            return false;
        }
        return fprintf(file,
                       "  mv t0, a0\\n"
                       "  ld t1, 0(sp)\\n"
                       "  addi sp, sp, 16\\n") >= 0 &&
               minic_riscv64_emit_scalar_store(file, target->type, "t0", "t1") &&
               fprintf(file, "  mv a0, t0\\n") >= 0;
    }
    case MINIC_EXPRESSION_UNARY:
""",
)

print("staged global scalar RV64 loads and true assignment expressions")
