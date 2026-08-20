#!/usr/bin/env python3
"""Materialize generic RV64 two-GPR semantics for the int128 operations used by real C code."""
from pathlib import Path

MARKER = "RV64_INT128_PAIR_V1"


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    if new in text:
        return
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected 1 anchor, found {count}")
    p.write_text(text.replace(old, new, 1))


# Public backend helpers: an int128 rvalue is represented as a0=low64, a1=high64.
replace_once(
    "src/target/riscv64/codegen_internal.h",
    '''bool minic_riscv64_emit_scalar_store_for_program(FILE *file,\n                                                 const MinicC0Program *program,\n                                                 MinicType type,\n                                                 const char *source_register,\n                                                 const char *address_register);\n\n''',
    '''bool minic_riscv64_emit_scalar_store_for_program(FILE *file,\n                                                 const MinicC0Program *program,\n                                                 MinicType type,\n                                                 const char *source_register,\n                                                 const char *address_register);\n/* RV64_INT128_PAIR_V1: int128 rvalues use a0=low64 and a1=high64. */\nbool minic_riscv64_emit_int128_load_from_address(FILE *file, const char *address_register);\nbool minic_riscv64_emit_int128_store_to_address(FILE *file, const char *address_register);\n\n''')

# Scalar helpers must fail closed for 16-byte integers instead of silently selecting lw/sw.
replace_once(
    "src/target/riscv64/codegen_support.c",
    '''    if (!minic_type_is_integer(type)) {\n        return NULL;\n    }\n    if (minic_type_is_bool_integer(type)) {\n        return "lbu";\n''',
    '''    if (!minic_type_is_integer(type) || minic_type_is_int128_integer(type)) {\n        return NULL;\n    }\n    if (minic_type_is_bool_integer(type)) {\n        return "lbu";\n''')
replace_once(
    "src/target/riscv64/codegen_support.c",
    '''    if (!minic_type_is_integer(type)) {\n        return NULL;\n    }\n    return (minic_type_is_bool_integer(type) || minic_type_is_char_integer(type)) ? "sb"\n''',
    '''    if (!minic_type_is_integer(type) || minic_type_is_int128_integer(type)) {\n        return NULL;\n    }\n    return (minic_type_is_bool_integer(type) || minic_type_is_char_integer(type)) ? "sb"\n''')
replace_once(
    "src/target/riscv64/codegen_support.c",
    '''bool minic_riscv64_emit_integer_conversion(FILE *file, MinicType type, const char *register_name) {\n    if (register_name == NULL || !minic_type_is_integer(type)) {\n        return false;\n    }\n    if (minic_type_is_bool_integer(type)) {\n''',
    '''bool minic_riscv64_emit_integer_conversion(FILE *file, MinicType type, const char *register_name) {\n    if (register_name == NULL || !minic_type_is_integer(type) ||\n        minic_type_is_int128_integer(type)) {\n        return false;\n    }\n    if (minic_type_is_bool_integer(type)) {\n''')
replace_once(
    "src/target/riscv64/codegen_support.c",
    '''bool minic_riscv64_emit_object_address(FILE *file,\n''',
    '''/* RV64_INT128_PAIR_V1: keep the address stable while a0 becomes the low half. */\nbool minic_riscv64_emit_int128_load_from_address(FILE *file, const char *address_register) {\n    if (file == NULL || address_register == NULL) {\n        return false;\n    }\n    return fprintf(file,\n                   "  mv t0, %s\\n"\n                   "  ld a0, 0(t0)\\n"\n                   "  ld a1, 8(t0)\\n",\n                   address_register) >= 0;\n}\n\nbool minic_riscv64_emit_int128_store_to_address(FILE *file, const char *address_register) {\n    if (file == NULL || address_register == NULL) {\n        return false;\n    }\n    return fprintf(file, "  sd a0, 0(%s)\\n  sd a1, 8(%s)\\n", address_register, address_register) >=\n           0;\n}\n\nbool minic_riscv64_emit_object_address(FILE *file,\n''')

# Declaration/statement assignments need to preserve both halves while computing the target address.
replace_once(
    "src/target/riscv64/codegen_statement.c",
    '''    if (!minic_riscv64_emit_expression(\n            file, program, function, function_layout, statement->expression)) {\n        return false;\n    }\n    if (fprintf(file, "  addi sp, sp, -16\\n  sd a0, 0(sp)\\n") < 0) {\n        return false;\n    }\n''',
    '''    if (!minic_riscv64_emit_expression(\n            file, program, function, function_layout, statement->expression)) {\n        return false;\n    }\n    if (minic_type_is_int128_integer(target->type)) {\n        if (!minic_type_is_int128_integer(value->type) ||\n            fprintf(file, "  addi sp, sp, -16\\n  sd a0, 0(sp)\\n  sd a1, 8(sp)\\n") < 0 ||\n            !minic_riscv64_emit_lvalue_address(\n                file, program, function, function_layout, statement->target_expression) ||\n            fprintf(file,\n                    "  mv t0, a0\\n"\n                    "  ld a0, 0(sp)\\n"\n                    "  ld a1, 8(sp)\\n"\n                    "  addi sp, sp, 16\\n") < 0 ||\n            !minic_riscv64_emit_int128_store_to_address(file, "t0")) {\n            return false;\n        }\n        return true;\n    }\n    if (fprintf(file, "  addi sp, sp, -16\\n  sd a0, 0(sp)\\n") < 0) {\n        return false;\n    }\n''')

# Pair-aware rvalue loads for all ordinary lvalue forms used by int128 objects.
replace_once(
    "src/target/riscv64/codegen_expression.c",
    '''    case MINIC_EXPRESSION_LOCAL:\n        return minic_riscv64_emit_object_load(\n            file, program, function, function_layout, expression->value.local_id);\n''',
    '''    case MINIC_EXPRESSION_LOCAL:\n        if (minic_type_is_int128_integer(expression->type)) {\n            return minic_riscv64_emit_object_address(\n                       file, program, function, function_layout, expression->value.local_id) &&\n                   minic_riscv64_emit_int128_load_from_address(file, "a0");\n        }\n        return minic_riscv64_emit_object_load(\n            file, program, function, function_layout, expression->value.local_id);\n''')
replace_once(
    "src/target/riscv64/codegen_expression.c",
    '''        if (minic_type_is_record(expression->type)) {\n            return true;\n        }\n        return minic_riscv64_emit_lvalue_load_from_address(\n''',
    '''        if (minic_type_is_record(expression->type)) {\n            return true;\n        }\n        if (minic_type_is_int128_integer(expression->type)) {\n            return minic_riscv64_emit_int128_load_from_address(file, "a0");\n        }\n        return minic_riscv64_emit_lvalue_load_from_address(\n''')
replace_once(
    "src/target/riscv64/codegen_expression.c",
    '''        return minic_riscv64_emit_scalar_load_for_program(file, program, object->type, "a0", "a0");\n''',
    '''        return minic_type_is_int128_integer(object->type)\n                   ? minic_riscv64_emit_int128_load_from_address(file, "a0")\n                   : minic_riscv64_emit_scalar_load_for_program(\n                         file, program, object->type, "a0", "a0");\n''')
replace_once(
    "src/target/riscv64/codegen_expression.c",
    '''    case MINIC_EXPRESSION_DEREFERENCE:\n        if (minic_type_is_function(expression->type)) {\n            return minic_riscv64_emit_expression(\n                file, program, function, function_layout, expression->value.unary.operand);\n        }\n        return minic_riscv64_emit_expression(\n                   file, program, function, function_layout, expression->value.unary.operand) &&\n               minic_riscv64_emit_scalar_load_for_program(\n                   file, program, expression->type, "a0", "a0");\n    case MINIC_EXPRESSION_SUBSCRIPT:\n        return minic_riscv64_emit_subscript_address(\n                   file, program, function, function_layout, expression) &&\n               minic_riscv64_emit_scalar_load_for_program(\n                   file, program, expression->type, "a0", "a0");\n''',
    '''    case MINIC_EXPRESSION_DEREFERENCE:\n        if (minic_type_is_function(expression->type)) {\n            return minic_riscv64_emit_expression(\n                file, program, function, function_layout, expression->value.unary.operand);\n        }\n        if (!minic_riscv64_emit_expression(\n                file, program, function, function_layout, expression->value.unary.operand)) {\n            return false;\n        }\n        return minic_type_is_int128_integer(expression->type)\n                   ? minic_riscv64_emit_int128_load_from_address(file, "a0")\n                   : minic_riscv64_emit_scalar_load_for_program(\n                         file, program, expression->type, "a0", "a0");\n    case MINIC_EXPRESSION_SUBSCRIPT:\n        if (!minic_riscv64_emit_subscript_address(\n                file, program, function, function_layout, expression)) {\n            return false;\n        }\n        return minic_type_is_int128_integer(expression->type)\n                   ? minic_riscv64_emit_int128_load_from_address(file, "a0")\n                   : minic_riscv64_emit_scalar_load_for_program(\n                         file, program, expression->type, "a0", "a0");\n''')
replace_once(
    "src/target/riscv64/codegen_expression.c",
    '''        if (field->is_array) {\n            return expression->value_category == MINIC_VALUE_LVALUE;\n        }\n        return minic_riscv64_emit_lvalue_load_from_address(\n            file, program, expression_id, expression->type, "a0", "a0");\n    }\n    case MINIC_EXPRESSION_LVALUE_READ:\n        return minic_riscv64_emit_lvalue_address(\n                   file, program, function, function_layout, expression->value.unary.operand) &&\n               minic_riscv64_emit_lvalue_load_from_address(\n                   file, program, expression->value.unary.operand, expression->type, "a0", "a0");\n''',
    '''        if (field->is_array) {\n            return expression->value_category == MINIC_VALUE_LVALUE;\n        }\n        if (minic_type_is_int128_integer(expression->type)) {\n            return minic_riscv64_emit_int128_load_from_address(file, "a0");\n        }\n        return minic_riscv64_emit_lvalue_load_from_address(\n            file, program, expression_id, expression->type, "a0", "a0");\n    }\n    case MINIC_EXPRESSION_LVALUE_READ:\n        if (!minic_riscv64_emit_lvalue_address(\n                file, program, function, function_layout, expression->value.unary.operand)) {\n            return false;\n        }\n        return minic_type_is_int128_integer(expression->type)\n                   ? minic_riscv64_emit_int128_load_from_address(file, "a0")\n                   : minic_riscv64_emit_lvalue_load_from_address(\n                         file,\n                         program,\n                         expression->value.unary.operand,\n                         expression->type,\n                         "a0",\n                         "a0");\n''')

# Preserve pair identity conversions, but reject mixed-width int128 conversions for now.
replace_once(
    "src/target/riscv64/codegen_expression.c",
    '''        if (minic_type_is_integer(expression->type) && minic_type_is_integer(operand->type)) {\n            return minic_riscv64_emit_expression(\n                       file, program, function, function_layout, expression->value.unary.operand) &&\n                   minic_riscv64_emit_integer_conversion_for_program(\n                       file, program, expression->type, "a0");\n        }\n''',
    '''        if (minic_type_is_integer(expression->type) && minic_type_is_integer(operand->type)) {\n            if (minic_type_is_int128_integer(expression->type) ||\n                minic_type_is_int128_integer(operand->type)) {\n                return minic_type_is_int128_integer(expression->type) &&\n                       minic_type_is_int128_integer(operand->type) &&\n                       minic_riscv64_emit_expression(\n                           file, program, function, function_layout, expression->value.unary.operand);\n            }\n            return minic_riscv64_emit_expression(\n                       file, program, function, function_layout, expression->value.unary.operand) &&\n                   minic_riscv64_emit_integer_conversion_for_program(\n                       file, program, expression->type, "a0");\n        }\n''')

# Assignment expressions return the stored pair in a0/a1.
replace_once(
    "src/target/riscv64/codegen_expression.c",
    '''        if (target != NULL && minic_type_is_record(target->type)) {\n            return minic_riscv64_emit_record_assignment_expression(\n                file, program, function, function_layout, expression);\n        }\n        if (target == NULL || value == NULL || target->value_category != MINIC_VALUE_LVALUE ||\n''',
    '''        if (target != NULL && minic_type_is_record(target->type)) {\n            return minic_riscv64_emit_record_assignment_expression(\n                file, program, function, function_layout, expression);\n        }\n        if (target != NULL && minic_type_is_int128_integer(target->type)) {\n            if (value == NULL || !minic_type_is_int128_integer(value->type) ||\n                target->value_category != MINIC_VALUE_LVALUE ||\n                !minic_type_equal(expression->type, target->type) ||\n                !minic_c0_assignment_compatible(program, target->type, expression->value.binary.right) ||\n                !minic_riscv64_emit_lvalue_address(\n                    file, program, function, function_layout, expression->value.binary.left) ||\n                fprintf(file, "  addi sp, sp, -16\\n  sd a0, 0(sp)\\n") < 0 ||\n                !minic_riscv64_emit_expression(\n                    file, program, function, function_layout, expression->value.binary.right) ||\n                fprintf(file, "  ld t0, 0(sp)\\n  addi sp, sp, 16\\n") < 0 ||\n                !minic_riscv64_emit_int128_store_to_address(file, "t0")) {\n                return false;\n            }\n            return true;\n        }\n        if (target == NULL || value == NULL || target->value_category != MINIC_VALUE_LVALUE ||\n''')

# Equality/inequality operate on both 64-bit halves before the legacy one-GPR binary path.
replace_once(
    "src/target/riscv64/codegen_expression.c",
    '''        has_pointer_relational = left != NULL && right != NULL &&\n                                 minic_type_is_pointer(left->type) &&\n                                 minic_type_is_pointer(right->type) &&\n                                 minic_type_equal(expression->type, minic_type_int());\n        if (left == NULL || right == NULL ||\n''',
    '''        has_pointer_relational = left != NULL && right != NULL &&\n                                 minic_type_is_pointer(left->type) &&\n                                 minic_type_is_pointer(right->type) &&\n                                 minic_type_equal(expression->type, minic_type_int());\n        if (has_integer_common_type && minic_type_is_int128_integer(common_integer_type)) {\n            const char *finish;\n\n            if (left == NULL || right == NULL || !minic_type_is_int128_integer(left->type) ||\n                !minic_type_is_int128_integer(right->type) ||\n                (expression->value.binary.operator_kind != MINIC_BINARY_EQUAL &&\n                 expression->value.binary.operator_kind != MINIC_BINARY_NOT_EQUAL) ||\n                !minic_type_equal(expression->type, minic_type_int()) ||\n                !minic_riscv64_emit_expression(\n                    file, program, function, function_layout, expression->value.binary.left) ||\n                fprintf(file,\n                        "  addi sp, sp, -16\\n"\n                        "  sd a0, 0(sp)\\n"\n                        "  sd a1, 8(sp)\\n") < 0 ||\n                !minic_riscv64_emit_expression(\n                    file, program, function, function_layout, expression->value.binary.right) ||\n                fprintf(file,\n                        "  ld t0, 0(sp)\\n"\n                        "  ld t1, 8(sp)\\n"\n                        "  addi sp, sp, 16\\n"\n                        "  xor t0, t0, a0\\n"\n                        "  xor t1, t1, a1\\n"\n                        "  or a0, t0, t1\\n") < 0) {\n                return false;\n            }\n            finish = expression->value.binary.operator_kind == MINIC_BINARY_EQUAL ? "seqz" : "snez";\n            return fprintf(file, "  %s a0, a0\\n", finish) >= 0;\n        }\n        if (left == NULL || right == NULL ||\n''')

# Existing int128 type regression now also compiles load/store/local/equality paths. Linux replay
# supplies the unchanged real-world end-to-end pressure; this focused test keeps the backend contract.
replace_once(
    "tests/compiler/c0/gnu_int128_type.c",
    '''unsigned long int128_record_size(void) {\n    return sizeof(struct Int128Layout);\n}\n''',
    '''unsigned long int128_record_size(void) {\n    return sizeof(struct Int128Layout);\n}\n\ntypedef union Int128Words {\n    struct {\n        unsigned long low;\n        unsigned long high;\n    } words;\n    unsigned128_t full;\n} Int128Words;\n\nint int128_pair_equal(const Int128Words *left, const Int128Words *right) {\n    unsigned128_t left_value = left->full;\n    unsigned128_t right_value = right->full;\n    return left_value == right_value;\n}\n\nvoid int128_pair_copy(Int128Words *target, const Int128Words *source) {\n    unsigned128_t value = source->full;\n    target->full = value;\n}\n''')
replace_once(
    "tests/compiler/c0/run-gnu-int128-type.sh",
    '''for symbol in signed128_size unsigned128_size direct_unsigned128_size int128_record_size; do\n''',
    '''for symbol in signed128_size unsigned128_size direct_unsigned128_size int128_record_size \\\n    int128_pair_equal int128_pair_copy; do\n''')
replace_once(
    "tests/compiler/c0/run-gnu-int128-type.sh",
    '''test "$size32" -ge 1\n\nprintf '%s\\n' 'PASS compiler/c0/gnu_int128_type signed=1 unsigned=2 scalar-size=16 align=16 record-size=32'\n''',
    '''test "$size32" -ge 1\n# Two-half values must touch the high 64-bit lane and compare both halves.\ngrep -Eq 'ld a1, 8\\(t0\\)' "$assembly"\ngrep -Eq 'sd a1, 8\\(t0\\)' "$assembly"\ngrep -F 'or a0, t0, t1' "$assembly" >/dev/null\n\nprintf '%s\\n' 'PASS compiler/c0/gnu_int128_type signed=1 unsigned=2 pair-load-store-equality=1'\n''')

print(MARKER)
