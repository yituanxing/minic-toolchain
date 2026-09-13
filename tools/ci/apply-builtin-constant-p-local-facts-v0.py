#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}: {old[:160]!r}")
    p.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/ast.h",
    "    MINIC_BUILTIN_UNARY_FFSLL,\n    MINIC_BUILTIN_UNARY_ISDIGIT\n} MinicBuiltinUnaryOperator;\n",
    "    MINIC_BUILTIN_UNARY_FFSLL,\n    MINIC_BUILTIN_UNARY_ISDIGIT,\n    MINIC_BUILTIN_UNARY_CONSTANT_P\n} MinicBuiltinUnaryOperator;\n",
)

replace_once(
    "src/frontend/parser_expression.c",
    '''    /* __builtin_constant_p is a compile-time query. The operand is parsed for C\n     * semantics but its AST edge is intentionally not retained, so it is never\n     * evaluated at runtime. This first generic implementation recognizes the\n     * integer constant-expression subset already shared by choose_expr; unknown\n     * expressions conservatively produce 0, as required by GCC's contract. */\n    (void)memset(&result, 0, sizeof(result));\n    result.kind = MINIC_EXPRESSION_INTEGER;\n    result.span.begin = begin;\n    result.span.end = end;\n    result.type = minic_type_int();\n    result.value_category = MINIC_VALUE_RVALUE;\n    result.value.integer_value = is_constant ? 1 : 0;\n    return minic_parser_add_expression(parser, &result, expression_id);\n''',
    '''    /* Keep unresolved constant-p queries unevaluated in the AST.  Core lowering\n     * can then answer the query from its conservative straight-line local fact\n     * table without ever evaluating the operand at runtime.  Pure frontend ICEs\n     * remain folded immediately. */\n    (void)memset(&result, 0, sizeof(result));\n    result.span.begin = begin;\n    result.span.end = end;\n    result.type = minic_type_int();\n    result.value_category = MINIC_VALUE_RVALUE;\n    if (is_constant) {\n        result.kind = MINIC_EXPRESSION_INTEGER;\n        result.value.integer_value = 1;\n    } else {\n        result.kind = MINIC_EXPRESSION_BUILTIN_UNARY;\n        result.value.builtin_unary.operator_kind = MINIC_BUILTIN_UNARY_CONSTANT_P;\n        result.value.builtin_unary.operand = operand_id;\n    }\n    return minic_parser_add_expression(parser, &result, expression_id);\n''',
)

replace_once(
    "src/frontend/ast_verifier.c",
    '''        case MINIC_BUILTIN_UNARY_ISDIGIT:\n            expected_operand_type = minic_type_int();\n            break;\n        default:\n''',
    '''        case MINIC_BUILTIN_UNARY_ISDIGIT:\n            expected_operand_type = minic_type_int();\n            break;\n        case MINIC_BUILTIN_UNARY_CONSTANT_P:\n            /* GCC permits __builtin_constant_p on non-integer expressions too.\n             * The operand is unevaluated; its type does not constrain the int\n             * result.  Returning here avoids imposing the ordinary unary-builtin\n             * operand-type equality contract. */\n            return builtin_operand != NULL &&\n                   expression->value_category == MINIC_VALUE_RVALUE &&\n                   minic_type_equal(expression->type, minic_type_int());\n        default:\n''',
)

p = Path("src/core/core_lower.c")
text = p.read_text()
old = '''    if (expression->kind == MINIC_EXPRESSION_STATEMENT &&\n        expression->value.statement_expression.result != MINIC_EXPRESSION_INVALID) {\n'''
new = '''    if (expression->kind == MINIC_EXPRESSION_BUILTIN_UNARY &&\n        expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_CONSTANT_P) {\n        value->type = minic_type_int();\n        value->bits = core_const_eval_integer_with_locals(\n                          context, expression->value.builtin_unary.operand, &operand_value)\n                          ? 1U\n                          : 0U;\n        return true;\n    }\n    if (expression->kind == MINIC_EXPRESSION_STATEMENT &&\n        expression->value.statement_expression.result != MINIC_EXPRESSION_INVALID) {\n'''
count = text.count(old)
if count != 1:
    raise SystemExit(f"core evaluator: expected one statement-expression anchor, found {count}")
text = text.replace(old, new, 1)

old = '''    if (expression->kind == MINIC_EXPRESSION_BUILTIN_UNARY &&\n        expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_ISDIGIT) {\n'''
new = '''    if (expression->kind == MINIC_EXPRESSION_BUILTIN_UNARY &&\n        expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_CONSTANT_P) {\n        MinicConstValue ignored_constant;\n        bool is_constant;\n\n        is_constant = core_const_eval_integer_with_locals(\n            context, expression->value.builtin_unary.operand, &ignored_constant);\n        (void)memset(&instruction, 0, sizeof(instruction));\n        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;\n        instruction.span = expression->span;\n        instruction.type = minic_type_int();\n        instruction.result = MINIC_CORE_VALUE_INVALID;\n        instruction.value.integer_value = is_constant ? 1 : 0;\n        return minic_core_function_append_value_instruction(\n                   context->function, context->block_id, &instruction, value_id)\n                   ? MINIC_CORE_LOWER_OK\n                   : MINIC_CORE_LOWER_ERROR;\n    }\n\n    if (expression->kind == MINIC_EXPRESSION_BUILTIN_UNARY &&\n        expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_ISDIGIT) {\n'''
count = text.count(old)
if count != 1:
    raise SystemExit(f"core lowering: expected one isdigit anchor, found {count}")
text = text.replace(old, new, 1)
p.write_text(text)

print("MINIC_BUILTIN_CONSTANT_P_LOCAL_FACTS_V0=APPLIED")
