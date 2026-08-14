#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/parser_expression.c",
    '''        if (minic_type_is_pointer(target_type)) {
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
''',
    '''        if (minic_type_is_pointer(target_type)) {
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
        } else if (minic_type_is_double(target_type)) {
            if ((compound_operator != MINIC_BINARY_ADD &&
                 compound_operator != MINIC_BINARY_SUBTRACT &&
                 compound_operator != MINIC_BINARY_MULTIPLY &&
                 compound_operator != MINIC_BINARY_DIVIDE) ||
                (!minic_type_is_double(value_expression->type) &&
                 !minic_type_is_integer(value_expression->type))) {
                minic_parser_error(
                    parser, "double compound assignment requires arithmetic operands");
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
''',
    "floating compound parser",
)

replace_once(
    "src/frontend/ast_verifier.c",
    '''        if (minic_type_is_pointer(left->type)) {
            return (operator_kind == MINIC_BINARY_ADD || operator_kind == MINIC_BINARY_SUBTRACT) &&
                   minic_type_is_integer(right->type) &&
                   minic_type_pointee(left->type, &pointee_type) &&
                   type_is_complete_object(program, pointee_type);
        }
        if (operator_kind != MINIC_BINARY_ADD && operator_kind != MINIC_BINARY_SUBTRACT &&
            operator_kind != MINIC_BINARY_MULTIPLY && operator_kind != MINIC_BINARY_DIVIDE &&
            operator_kind != MINIC_BINARY_REMAINDER &&
            operator_kind != MINIC_BINARY_BITWISE_AND && operator_kind != MINIC_BINARY_BITWISE_OR &&
            operator_kind != MINIC_BINARY_BITWISE_XOR && operator_kind != MINIC_BINARY_SHIFT_RIGHT) {
            return false;
        }
        return minic_type_is_integer(left->type) && minic_type_is_integer(right->type) &&
               minic_type_integer_common(left->type, right->type, &common_type);
''',
    '''        if (minic_type_is_pointer(left->type)) {
            return (operator_kind == MINIC_BINARY_ADD || operator_kind == MINIC_BINARY_SUBTRACT) &&
                   minic_type_is_integer(right->type) &&
                   minic_type_pointee(left->type, &pointee_type) &&
                   type_is_complete_object(program, pointee_type);
        }
        if (minic_type_is_double(left->type)) {
            return (operator_kind == MINIC_BINARY_ADD || operator_kind == MINIC_BINARY_SUBTRACT ||
                    operator_kind == MINIC_BINARY_MULTIPLY || operator_kind == MINIC_BINARY_DIVIDE) &&
                   (minic_type_is_double(right->type) || minic_type_is_integer(right->type));
        }
        if (operator_kind != MINIC_BINARY_ADD && operator_kind != MINIC_BINARY_SUBTRACT &&
            operator_kind != MINIC_BINARY_MULTIPLY && operator_kind != MINIC_BINARY_DIVIDE &&
            operator_kind != MINIC_BINARY_REMAINDER &&
            operator_kind != MINIC_BINARY_BITWISE_AND && operator_kind != MINIC_BINARY_BITWISE_OR &&
            operator_kind != MINIC_BINARY_BITWISE_XOR && operator_kind != MINIC_BINARY_SHIFT_RIGHT) {
            return false;
        }
        return minic_type_is_integer(left->type) && minic_type_is_integer(right->type) &&
               minic_type_integer_common(left->type, right->type, &common_type);
''',
    "floating compound verifier",
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    '''        if (minic_type_is_pointer(target->type)) {
            size_t element_size;

            if ((operator_kind != MINIC_BINARY_ADD && operator_kind != MINIC_BINARY_SUBTRACT) ||
                !minic_type_is_integer(value->type) ||
                !minic_riscv64_pointer_element_size(program, target->type, &element_size) ||
                fprintf(file, "  sd a0, 8(sp)\\n") < 0 ||
                !minic_riscv64_emit_expression(
                    file, program, function, expression->value.binary.right) ||
                !minic_riscv64_emit_scale_register(file, "a0", "t0", element_size) ||
                fprintf(file,
                        "  ld t0, 8(sp)\\n"
                        "  %s a0, t0, a0\\n",
                        operator_kind == MINIC_BINARY_ADD ? "add" : "sub") < 0) {
                return false;
            }
        } else {
            MinicType common_type;
            const char *opcode;
''',
    '''        if (minic_type_is_pointer(target->type)) {
            size_t element_size;

            if ((operator_kind != MINIC_BINARY_ADD && operator_kind != MINIC_BINARY_SUBTRACT) ||
                !minic_type_is_integer(value->type) ||
                !minic_riscv64_pointer_element_size(program, target->type, &element_size) ||
                fprintf(file, "  sd a0, 8(sp)\\n") < 0 ||
                !minic_riscv64_emit_expression(
                    file, program, function, expression->value.binary.right) ||
                !minic_riscv64_emit_scale_register(file, "a0", "t0", element_size) ||
                fprintf(file,
                        "  ld t0, 8(sp)\\n"
                        "  %s a0, t0, a0\\n",
                        operator_kind == MINIC_BINARY_ADD ? "add" : "sub") < 0) {
                return false;
            }
        } else if (minic_type_is_double(target->type)) {
            if ((operator_kind != MINIC_BINARY_ADD && operator_kind != MINIC_BINARY_SUBTRACT &&
                 operator_kind != MINIC_BINARY_MULTIPLY && operator_kind != MINIC_BINARY_DIVIDE) ||
                (!minic_type_is_double(value->type) && !minic_type_is_integer(value->type)) ||
                fprintf(file, "  sd a0, 8(sp)\\n") < 0 ||
                !minic_riscv64_emit_expression(
                    file, program, function, expression->value.binary.right) ||
                fprintf(file, "  ld t0, 8(sp)\\n") < 0 ||
                !minic_riscv64_emit_double_binary(
                    file, operator_kind, target->type, value->type)) {
                return false;
            }
        } else {
            MinicType common_type;
            const char *opcode;
''',
    "floating compound RV64 lowering",
)

print("staged double compound assignments with integer/double RHS conversion")
