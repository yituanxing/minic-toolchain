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
    "src/frontend/parser_expression.c",
    """        target_expression = minic_c0_program_expression(parser->program, left);
        if (target_expression != NULL && minic_type_is_record(target_expression->type)) {
            /* Record assignment already has statement-level recursive copy lowering.
               Leave '=' unconsumed so that path can handle standalone record copies. */
            *expression_id = left;
            return true;
        }
        if (target_expression == NULL || target_expression->value_category != MINIC_VALUE_LVALUE ||
            minic_type_is_const(target_expression->type) ||
            minic_type_is_array(target_expression->type) ||
            minic_type_is_function(target_expression->type) ||
            minic_type_is_record(target_expression->type)) {
            minic_parser_error(parser, \"assignment expression requires a modifiable scalar lvalue\");
            return false;
        }
""",
    """        target_expression = minic_c0_program_expression(parser->program, left);
        if (target_expression == NULL || target_expression->value_category != MINIC_VALUE_LVALUE ||
            minic_type_is_const(target_expression->type) ||
            minic_type_is_array(target_expression->type) ||
            minic_type_is_function(target_expression->type)) {
            minic_parser_error(parser, \"assignment expression requires a modifiable object lvalue\");
            return false;
        }
""",
)

replace_once(
    "src/frontend/parser_expression.c",
    """        if (!minic_c0_assignment_compatible(parser->program, target_type, value_id)) {
            MinicExpression cast_expression;

            if (minic_type_is_pointer(target_type) ||
                minic_type_is_pointer(value_expression->type) ||
                !minic_type_cast_compatible(target_type, value_expression->type)) {
                minic_parser_error(parser, \"assignment expression type does not match target type\");
                return false;
            }
            (void)memset(&cast_expression, 0, sizeof(cast_expression));
            cast_expression.kind = MINIC_EXPRESSION_CAST;
            cast_expression.span = value_expression->span;
            cast_expression.type = target_type;
            cast_expression.value_category = MINIC_VALUE_RVALUE;
            cast_expression.value.unary.operand = value_id;
            if (!minic_parser_add_expression(parser, &cast_expression, &value_id)) {
                return false;
            }
        }
        value_expression = minic_c0_program_expression(parser->program, value_id);
        if (value_expression == NULL ||
            !minic_c0_assignment_compatible(parser->program, target_type, value_id)) {
            minic_parser_error(parser, \"assignment expression conversion failed\");
            return false;
        }
""",
    """        if (minic_type_is_record(target_type)) {
            if (value_expression->value_category != MINIC_VALUE_LVALUE ||
                !minic_type_is_record(value_expression->type) ||
                target_type.record_id != value_expression->type.record_id) {
                minic_parser_error(parser, \"record assignment expression requires matching record lvalues\");
                return false;
            }
        } else {
            if (!minic_c0_assignment_compatible(parser->program, target_type, value_id)) {
                MinicExpression cast_expression;

                if (minic_type_is_pointer(target_type) ||
                    minic_type_is_pointer(value_expression->type) ||
                    !minic_type_cast_compatible(target_type, value_expression->type)) {
                    minic_parser_error(parser, \"assignment expression type does not match target type\");
                    return false;
                }
                (void)memset(&cast_expression, 0, sizeof(cast_expression));
                cast_expression.kind = MINIC_EXPRESSION_CAST;
                cast_expression.span = value_expression->span;
                cast_expression.type = target_type;
                cast_expression.value_category = MINIC_VALUE_RVALUE;
                cast_expression.value.unary.operand = value_id;
                if (!minic_parser_add_expression(parser, &cast_expression, &value_id)) {
                    return false;
                }
            }
            value_expression = minic_c0_program_expression(parser->program, value_id);
            if (value_expression == NULL ||
                !minic_c0_assignment_compatible(parser->program, target_type, value_id)) {
                minic_parser_error(parser, \"assignment expression conversion failed\");
                return false;
            }
        }
""",
)

replace_once(
    "src/frontend/ast_verifier.c",
    """    case MINIC_EXPRESSION_ASSIGNMENT:
        left = expression_before(program, expression->value.binary.left, expression_index);
        right = expression_before(program, expression->value.binary.right, expression_index);
        return left != NULL && right != NULL && left->value_category == MINIC_VALUE_LVALUE &&
               !minic_type_is_const(left->type) && !minic_type_is_array(left->type) &&
               !minic_type_is_function(left->type) && !minic_type_is_record(left->type) &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_equal(expression->type, left->type) &&
               minic_c0_assignment_compatible(program, left->type, expression->value.binary.right);
""",
    """    case MINIC_EXPRESSION_ASSIGNMENT:
        left = expression_before(program, expression->value.binary.left, expression_index);
        right = expression_before(program, expression->value.binary.right, expression_index);
        if (left == NULL || right == NULL || left->value_category != MINIC_VALUE_LVALUE ||
            minic_type_is_const(left->type) || minic_type_is_array(left->type) ||
            minic_type_is_function(left->type) || expression->value_category != MINIC_VALUE_RVALUE ||
            !minic_type_equal(expression->type, left->type)) {
            return false;
        }
        if (minic_type_is_record(left->type)) {
            return right->value_category == MINIC_VALUE_LVALUE && minic_type_is_record(right->type) &&
                   left->type.record_id == right->type.record_id;
        }
        return minic_c0_assignment_compatible(program, left->type, expression->value.binary.right);
""",
)

codegen = Path("src/target/riscv64/codegen_expression.c")
text = codegen.read_text()
marker = "bool minic_riscv64_emit_expression(FILE *file,\n"
start = text.find(marker)
if start < 0 or text.find(marker, start + 1) >= 0:
    raise SystemExit("unexpected expression emitter marker")
helper = r'''static bool minic_riscv64_emit_record_assignment_expression(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    const MinicExpression *expression) {
    const MinicExpression *target;
    const MinicExpression *source;
    const MinicRecord *record;
    size_t storage_size;
    size_t temporary_size;
    size_t index;

    target = minic_c0_program_expression(program, expression->value.binary.left);
    source = minic_c0_program_expression(program, expression->value.binary.right);
    if (target == NULL || source == NULL || target->value_category != MINIC_VALUE_LVALUE ||
        source->value_category != MINIC_VALUE_LVALUE || minic_type_is_const(target->type) ||
        !minic_type_is_record(target->type) || !minic_type_is_record(source->type) ||
        target->type.record_id != source->type.record_id ||
        !minic_type_equal(expression->type, target->type)) {
        return false;
    }
    record = minic_c0_program_record(program, target->type.record_id);
    if (record == NULL || !record->is_complete || record->storage_size == 0U ||
        record->storage_size > SIZE_MAX - 15U) {
        return false;
    }
    storage_size = record->storage_size;
    temporary_size = (storage_size + 15U) & ~(size_t)15U;

    if (!minic_riscv64_emit_lvalue_address(
            file, program, function, expression->value.binary.right) ||
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
    if (!minic_riscv64_emit_lvalue_address(
            file, program, function, expression->value.binary.left) ||
        fprintf(file, "  mv t4, a0\n  mv t2, sp\n  mv t3, a0\n") < 0) {
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
    return minic_riscv64_emit_stack_release(file, temporary_size) &&
           fprintf(file, "  mv a0, t4\n") >= 0;
}

'''
codegen.write_text(text[:start] + helper + text[start:])

replace_once(
    "src/target/riscv64/codegen_expression.c",
    """    case MINIC_EXPRESSION_ASSIGNMENT: {
        const MinicExpression *target;
        const MinicExpression *value;

        target = minic_c0_program_expression(program, expression->value.binary.left);
        value = minic_c0_program_expression(program, expression->value.binary.right);
        if (target == NULL || value == NULL || target->value_category != MINIC_VALUE_LVALUE ||
""",
    """    case MINIC_EXPRESSION_ASSIGNMENT: {
        const MinicExpression *target;
        const MinicExpression *value;

        target = minic_c0_program_expression(program, expression->value.binary.left);
        value = minic_c0_program_expression(program, expression->value.binary.right);
        if (target != NULL && minic_type_is_record(target->type)) {
            return minic_riscv64_emit_record_assignment_expression(file, program, function, expression);
        }
        if (target == NULL || value == NULL || target->value_category != MINIC_VALUE_LVALUE ||
""",
)

print("staged alias-safe whole-record assignment expressions")
