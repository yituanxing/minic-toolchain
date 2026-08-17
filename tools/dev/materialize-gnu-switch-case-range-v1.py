#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]

# Parser switch context tracks labels/ranges rather than expanding every value.
h = root / "src/frontend/parser_internal.h"
text = h.read_text()
old = '''typedef struct MinicParserSwitchContext {\n    int64_t case_values[MINIC_PARSER_MAX_SWITCH_CASES];\n    size_t case_count;\n    bool has_default;\n} MinicParserSwitchContext;\n'''
new = '''typedef struct MinicParserSwitchContext {\n    int64_t case_lower_values[MINIC_PARSER_MAX_SWITCH_CASES];\n    int64_t case_upper_values[MINIC_PARSER_MAX_SWITCH_CASES];\n    size_t case_count;\n    bool has_default;\n} MinicParserSwitchContext;\n'''
if text.count(old) != 1:
    raise SystemExit(f"switch context anchor count={text.count(old)}")
h.write_text(text.replace(old, new, 1))

# A CASE statement now owns one lower-bound expression and, for GNU ranges, one
# optional upper-bound expression in target_expression. This keeps one semantic
# statement per source label and makes the implementation limit about labels,
# not the cardinality of a range.
p = root / "src/frontend/parser_statement.c"
text = p.read_text()
old_decls = '''    int64_t lower_value;\n    int64_t upper_value;\n    int64_t candidate;\n    size_t range_count;\n    size_t index;\n    bool is_range;\n'''
new_decls = '''    int64_t lower_value;\n    int64_t upper_value;\n    size_t index;\n    bool is_range;\n'''
if text.count(old_decls) != 1:
    raise SystemExit(f"parse_case declarations anchor count={text.count(old_decls)}")
text = text.replace(old_decls, new_decls, 1)

old_validate = '''    range_count = 0U;\n    candidate = lower_value;\n    for (;;) {\n        if (context->case_count + range_count >= MINIC_PARSER_MAX_SWITCH_CASES) {\n            minic_parser_error(parser, "switch case count exceeds implementation limit");\n            return false;\n        }\n        for (index = 0U; index < context->case_count; ++index) {\n            if (context->case_values[index] == candidate) {\n                minic_parser_error(parser, "duplicate case value");\n                return false;\n            }\n        }\n        range_count += 1U;\n        if (candidate == upper_value) {\n            break;\n        }\n        candidate += 1;\n    }\n\n'''
new_validate = '''    if (context->case_count >= MINIC_PARSER_MAX_SWITCH_CASES) {\n        minic_parser_error(parser, "switch case label count exceeds implementation limit");\n        return false;\n    }\n    for (index = 0U; index < context->case_count; ++index) {\n        if (lower_value <= context->case_upper_values[index] &&\n            context->case_lower_values[index] <= upper_value) {\n            minic_parser_error(parser, "duplicate or overlapping case value range");\n            return false;\n        }\n    }\n\n'''
if text.count(old_validate) != 1:
    raise SystemExit(f"parse_case expansion validation anchor count={text.count(old_validate)}")
text = text.replace(old_validate, new_validate, 1)

old_emit = '''    candidate = lower_value;\n    for (;;) {\n        MinicStatement case_statement;\n\n        (void)memset(&folded_constant, 0, sizeof(folded_constant));\n        folded_constant.kind = MINIC_EXPRESSION_INTEGER;\n        folded_constant.span = constant_span;\n        folded_constant.type = constant_type;\n        folded_constant.value_category = MINIC_VALUE_RVALUE;\n        folded_constant.value.integer_value = candidate;\n\n        case_statement = statement;\n        if (!minic_parser_add_expression(parser, &folded_constant, &case_statement.expression) ||\n            !minic_parser_add_statement(parser, &case_statement)) {\n            return false;\n        }\n        context->case_values[context->case_count] = candidate;\n        context->case_count += 1U;\n        if (candidate == upper_value) {\n            break;\n        }\n        candidate += 1;\n    }\n    return true;\n'''
new_emit = '''    (void)memset(&folded_constant, 0, sizeof(folded_constant));\n    folded_constant.kind = MINIC_EXPRESSION_INTEGER;\n    folded_constant.span = lower_constant->span;\n    folded_constant.type = constant_type;\n    folded_constant.value_category = MINIC_VALUE_RVALUE;\n    folded_constant.value.integer_value = lower_value;\n    if (!minic_parser_add_expression(parser, &folded_constant, &statement.expression)) {\n        return false;\n    }\n    if (is_range) {\n        (void)memset(&folded_constant, 0, sizeof(folded_constant));\n        folded_constant.kind = MINIC_EXPRESSION_INTEGER;\n        folded_constant.span = upper_constant->span;\n        folded_constant.type = constant_type;\n        folded_constant.value_category = MINIC_VALUE_RVALUE;\n        folded_constant.value.integer_value = upper_value;\n        if (!minic_parser_add_expression(\n                parser, &folded_constant, &statement.target_expression)) {\n            return false;\n        }\n    }\n    if (!minic_parser_add_statement(parser, &statement)) {\n        return false;\n    }\n    context->case_lower_values[context->case_count] = lower_value;\n    context->case_upper_values[context->case_count] = upper_value;\n    context->case_count += 1U;\n    return true;\n'''
if text.count(old_emit) != 1:
    raise SystemExit(f"parse_case statement expansion anchor count={text.count(old_emit)}")
text = text.replace(old_emit, new_emit, 1)
p.write_text(text)

# Verifier accepts the optional range upper bound and keeps the invariant local
# to CASE nodes: both bounds are folded integer expressions of the same type and
# the range is nondecreasing.
v = root / "src/frontend/ast_verifier.c"
text = v.read_text()
old = '''    case MINIC_STATEMENT_CASE:\n        return statement->target_expression == MINIC_EXPRESSION_INVALID && expression != NULL &&\n               expression->kind == MINIC_EXPRESSION_INTEGER &&\n               minic_type_is_integer(expression->type) &&\n               statement->then_block == MINIC_BLOCK_INVALID &&\n               statement->else_block == MINIC_BLOCK_INVALID;\n'''
new = '''    case MINIC_STATEMENT_CASE:\n        return expression != NULL && expression->kind == MINIC_EXPRESSION_INTEGER &&\n               minic_type_is_integer(expression->type) &&\n               (statement->target_expression == MINIC_EXPRESSION_INVALID ||\n                (target != NULL && target->kind == MINIC_EXPRESSION_INTEGER &&\n                 minic_type_equal(target->type, expression->type) &&\n                 target->value.integer_value >= expression->value.integer_value)) &&\n               statement->then_block == MINIC_BLOCK_INVALID &&\n               statement->else_block == MINIC_BLOCK_INVALID;\n'''
if text.count(old) != 1:
    raise SystemExit(f"CASE verifier anchor count={text.count(old)}")
v.write_text(text.replace(old, new, 1))

# RV64 dispatch uses interval comparisons for ranged labels. The fixed-size label
# table remains 128 entries because it now counts source case labels, not values.
r = root / "src/target/riscv64/codegen_statement.c"
text = r.read_text()
old = '''        case_expression = minic_c0_program_expression(program, case_statement->expression);\n        if (case_expression == NULL || case_expression->kind != MINIC_EXPRESSION_INTEGER ||\n            fprintf(file,\n                    "  li t1, %" PRId64 "\\n"\n                    "  beq t0, t1, .Lswitch_case_%zu\\n",\n                    case_expression->value.integer_value,\n                    (size_t)case_id) < 0) {\n            return false;\n        }\n'''
new = '''        case_expression = minic_c0_program_expression(program, case_statement->expression);\n        if (case_expression == NULL || case_expression->kind != MINIC_EXPRESSION_INTEGER) {\n            return false;\n        }\n        if (case_statement->target_expression == MINIC_EXPRESSION_INVALID) {\n            if (fprintf(file,\n                        "  li t1, %" PRId64 "\\n"\n                        "  beq t0, t1, .Lswitch_case_%zu\\n",\n                        case_expression->value.integer_value,\n                        (size_t)case_id) < 0) {\n                return false;\n            }\n        } else {\n            const MinicExpression *upper_expression;\n            const char *less_than;\n            const char *greater_equal;\n\n            upper_expression = minic_c0_program_expression(\n                program, case_statement->target_expression);\n            if (upper_expression == NULL ||\n                upper_expression->kind != MINIC_EXPRESSION_INTEGER ||\n                !minic_type_equal(upper_expression->type, case_expression->type)) {\n                return false;\n            }\n            less_than = minic_type_is_unsigned_integer(selector->type) ? "bltu" : "blt";\n            greater_equal =\n                minic_type_is_unsigned_integer(selector->type) ? "bgeu" : "bge";\n            if (fprintf(file,\n                        "  li t1, %" PRId64 "\\n"\n                        "  %s t0, t1, .Lswitch_range_next_%zu\\n"\n                        "  li t1, %" PRId64 "\\n"\n                        "  %s t1, t0, .Lswitch_case_%zu\\n"\n                        ".Lswitch_range_next_%zu:\\n",\n                        case_expression->value.integer_value,\n                        less_than,\n                        (size_t)case_id,\n                        upper_expression->value.integer_value,\n                        greater_equal,\n                        (size_t)case_id,\n                        (size_t)case_id) < 0) {\n                return false;\n            }\n        }\n'''
if text.count(old) != 1:
    raise SystemExit(f"RV64 switch dispatch anchor count={text.count(old)}")
r.write_text(text.replace(old, new, 1))
