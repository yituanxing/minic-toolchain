#!/usr/bin/env python3
from pathlib import Path

path = Path('src/core/core_lower.c')
text = path.read_text()
marker = 'M51_SHIFT_COMPOUND_ASSIGNMENT'

if marker in text:
    print('M51 compound shift assignment already applied')
    raise SystemExit(0)

start_anchor = '    if (expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT &&\n'
end_anchor = '    if (minic_type_is_integer(expression->type) && context->target != NULL) {\n'
start = text.index(start_anchor)
end = text.index(end_anchor, start)
block = text[start:end]


def replace_once(source: str, old: str, new: str, label: str) -> str:
    count = source.count(old)
    if count != 1:
        raise SystemExit(f'M51 {label}: expected one anchor, found {count}')
    return source.replace(old, new, 1)

block = replace_once(
    block,
    '    if (expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT &&\n'
    '        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||\n'
    '         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT ||\n'
    '         expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_AND ||\n'
    '         expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_XOR ||\n'
    '         expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_OR)) {\n',
    '    /* M51_SHIFT_COMPOUND_ASSIGNMENT: shifts use integer promotions on each operand\n'
    '       independently; unlike arithmetic compound assignments they do not use the\n'
    '       usual arithmetic conversions to a shared operand type. */\n'
    '    if (expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT &&\n'
    '        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||\n'
    '         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT ||\n'
    '         expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_LEFT ||\n'
    '         expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_RIGHT ||\n'
    '         expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_AND ||\n'
    '         expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_XOR ||\n'
    '         expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_OR)) {\n',
    'operator set',
)

block = replace_once(
    block,
    '        MinicType address_type;\n'
    '        MinicType common_type;\n'
    '        MinicType stored_type;\n',
    '        MinicType address_type;\n'
    '        MinicType common_type;\n'
    '        MinicType right_type;\n'
    '        MinicType stored_type;\n'
    '        bool shift_assignment;\n',
    'locals',
)

block = replace_once(
    block,
    '        if (target == NULL || source == NULL || target->value_category != MINIC_VALUE_LVALUE ||\n'
    '            !minic_type_equal(expression->type, target->type) ||\n'
    '            minic_type_is_const(target->type) ||\n'
    '            !minic_type_unqualified(target->type, &stored_type) ||\n'
    '            !minic_type_is_integer(stored_type) || !minic_type_is_integer(source->type) ||\n'
    '            context->target == NULL ||\n'
    '            !minic_target_info_integer_common_for_program(\n'
    '                context->target, context->body->program, stored_type, source->type, &common_type)) {\n'
    '            return MINIC_CORE_LOWER_UNSUPPORTED;\n'
    '        }\n',
    '        if (target == NULL || source == NULL || target->value_category != MINIC_VALUE_LVALUE ||\n'
    '            !minic_type_equal(expression->type, target->type) ||\n'
    '            minic_type_is_const(target->type) ||\n'
    '            !minic_type_unqualified(target->type, &stored_type) ||\n'
    '            !minic_type_is_integer(stored_type) || !minic_type_is_integer(source->type) ||\n'
    '            context->target == NULL) {\n'
    '            return MINIC_CORE_LOWER_UNSUPPORTED;\n'
    '        }\n'
    '        shift_assignment =\n'
    '            expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_LEFT ||\n'
    '            expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_RIGHT;\n'
    '        if (shift_assignment) {\n'
    '            if (!minic_target_info_integer_promotion_for_program(\n'
    '                    context->target, context->body->program, stored_type, &common_type) ||\n'
    '                !minic_target_info_integer_promotion_for_program(\n'
    '                    context->target, context->body->program, source->type, &right_type)) {\n'
    '                return MINIC_CORE_LOWER_UNSUPPORTED;\n'
    '            }\n'
    '        } else {\n'
    '            if (!minic_target_info_integer_common_for_program(context->target,\n'
    '                                                              context->body->program,\n'
    '                                                              stored_type,\n'
    '                                                              source->type,\n'
    '                                                              &common_type)) {\n'
    '                return MINIC_CORE_LOWER_UNSUPPORTED;\n'
    '            }\n'
    '            right_type = common_type;\n'
    '        }\n',
    'integer semantics',
)

block = replace_once(
    block,
    '        status =\n'
    '            append_integer_conversion(context, source->span, common_type, right, &right_common);\n',
    '        status =\n'
    '            append_integer_conversion(context, source->span, right_type, right, &right_common);\n',
    'right promotion',
)

block = replace_once(
    block,
    '        case MINIC_BINARY_SUBTRACT:\n'
    '            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT;\n'
    '            break;\n'
    '        case MINIC_BINARY_BITWISE_AND:\n',
    '        case MINIC_BINARY_SUBTRACT:\n'
    '            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT;\n'
    '            break;\n'
    '        case MINIC_BINARY_SHIFT_LEFT:\n'
    '            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT;\n'
    '            break;\n'
    '        case MINIC_BINARY_SHIFT_RIGHT:\n'
    '            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;\n'
    '            break;\n'
    '        case MINIC_BINARY_BITWISE_AND:\n',
    'instruction mapping',
)

text = text[:start] + block + text[end:]
path.write_text(text)
print('M51 compound shift assignment applied')
