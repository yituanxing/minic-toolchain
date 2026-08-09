#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:180]!r}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/parser_expression.c",
    '''        target_expression = minic_c0_program_expression(parser->program, left);
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
            minic_parser_error(parser, "assignment expression requires a modifiable scalar lvalue");
            return false;
        }
''',
    '''        target_expression = minic_c0_program_expression(parser->program, left);
        if (target_expression == NULL || target_expression->value_category != MINIC_VALUE_LVALUE ||
            minic_type_is_const(target_expression->type) ||
            minic_type_is_array(target_expression->type) ||
            minic_type_is_function(target_expression->type)) {
            minic_parser_error(parser, "assignment expression requires a modifiable object lvalue");
            return false;
        }
''',
)

replace_once(
    "src/frontend/ast_verifier.c",
    '''        return left != NULL && right != NULL && left->value_category == MINIC_VALUE_LVALUE &&
               !minic_type_is_const(left->type) && !minic_type_is_array(left->type) &&
               !minic_type_is_function(left->type) && !minic_type_is_record(left->type) &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_equal(expression->type, left->type) &&
               minic_c0_assignment_compatible(program, left->type, expression->value.binary.right);
''',
    '''        return left != NULL && right != NULL && left->value_category == MINIC_VALUE_LVALUE &&
               !minic_type_is_const(left->type) && !minic_type_is_array(left->type) &&
               !minic_type_is_function(left->type) &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_equal(expression->type, left->type) &&
               minic_c0_assignment_compatible(program, left->type, expression->value.binary.right) &&
               (!minic_type_is_record(left->type) || right->value_category == MINIC_VALUE_LVALUE ||
                (right->kind == MINIC_EXPRESSION_ASSIGNMENT && minic_type_is_record(right->type)));
''',
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    '''        target = minic_c0_program_expression(program, expression->value.binary.left);
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
''',
    '''        target = minic_c0_program_expression(program, expression->value.binary.left);
        value = minic_c0_program_expression(program, expression->value.binary.right);
        if (target == NULL || value == NULL || target->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_equal(expression->type, target->type) ||
            !minic_c0_assignment_compatible(
                program, target->type, expression->value.binary.right) ||
            !minic_riscv64_emit_lvalue_address(
                file, program, function, expression->value.binary.left) ||
            fprintf(file, "  addi sp, sp, -16\\n  sd a0, 0(sp)\\n") < 0) {
            return false;
        }
        if (minic_type_is_record(target->type)) {
            size_t alignment;
            size_t size;

            if (!minic_riscv64_type_layout(program, target->type, &size, &alignment)) {
                return false;
            }
            if (value->value_category == MINIC_VALUE_LVALUE) {
                if (!minic_riscv64_emit_lvalue_address(
                        file, program, function, expression->value.binary.right)) {
                    return false;
                }
            } else if (value->kind == MINIC_EXPRESSION_ASSIGNMENT &&
                       minic_type_is_record(value->type)) {
                if (!minic_riscv64_emit_expression(
                        file, program, function, expression->value.binary.right)) {
                    return false;
                }
            } else {
                return false;
            }
            if (fprintf(file,
                        "  mv t1, a0\\n"
                        "  ld t0, 0(sp)\\n"
                        "  li t2, %zu\\n"
                        "  beqz t2, 2f\\n"
                        "1:\\n"
                        "  lbu t3, 0(t1)\\n"
                        "  sb t3, 0(t0)\\n"
                        "  addi t1, t1, 1\\n"
                        "  addi t0, t0, 1\\n"
                        "  addi t2, t2, -1\\n"
                        "  bnez t2, 1b\\n"
                        "2:\\n"
                        "  ld a0, 0(sp)\\n"
                        "  addi sp, sp, 16\\n",
                        size) < 0) {
                return false;
            }
            return true;
        }
        if (!minic_riscv64_emit_expression(
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
''',
)

print("staged record/union assignment expressions with RV64 memory copy")
