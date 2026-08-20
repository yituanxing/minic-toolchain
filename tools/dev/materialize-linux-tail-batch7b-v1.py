#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one match, found {count}")
    p.write_text(text.replace(old, new, 1))


p = Path('src/frontend/parser_expression.c')
text = p.read_text()
anchor = '''static bool parse_cast(MinicParser *parser, MinicExpressionId *expression_id) {\n'''
if text.count(anchor) != 1:
    raise SystemExit('scalar compound literal insertion anchor mismatch')
helper = r'''static bool parse_scalar_compound_literal(MinicParser *parser,
                                          MinicSourcePosition begin,
                                          MinicType type,
                                          MinicExpressionId *expression_id) {
    MinicLocal local;
    MinicLocalId local_id;
    MinicExpression hidden_lvalue;
    MinicExpression compound_literal;
    MinicExpressionId hidden_lvalue_id;
    MinicExpressionId value_id;
    MinicBlockId initializer_block;
    MinicBlockId parent_block;
    MinicStatement assignment;
    const MinicExpression *value;
    MinicSourceSpan initializer_span;
    bool success;

    if (parser == NULL || expression_id == NULL || parser->current.kind != MINIC_TOKEN_LBRACE ||
        parser->current_function == MINIC_FUNCTION_INVALID ||
        (!minic_type_is_integer(type) && !minic_type_is_pointer(type) &&
         !minic_type_is_double(type))) {
        if (parser != NULL) {
            minic_parser_error(parser, "scalar compound literal requires a supported scalar type");
        }
        return false;
    }
    if (!minic_parser_require_complete_object_type(
            parser, type, "scalar compound literal requires a complete object type")) {
        return false;
    }

    (void)memset(&local, 0, sizeof(local));
    local.name_span.begin = begin;
    local.name_span.end = begin;
    local.type = type;
    local.element_count = 1U;
    local.is_array = false;
    local.is_register_storage = false;
    if (!minic_c0_program_add_local(parser->program, &local, &local_id)) {
        minic_parser_error(parser, "cannot allocate scalar compound literal backing object");
        return false;
    }

    (void)memset(&hidden_lvalue, 0, sizeof(hidden_lvalue));
    hidden_lvalue.kind = MINIC_EXPRESSION_LOCAL;
    hidden_lvalue.span.begin = begin;
    hidden_lvalue.span.end = parser->current.span.begin;
    hidden_lvalue.type = type;
    hidden_lvalue.value_category = MINIC_VALUE_LVALUE;
    hidden_lvalue.value.local_id = local_id;
    if (!minic_parser_add_expression(parser, &hidden_lvalue, &hidden_lvalue_id) ||
        !minic_c0_program_add_block(parser->program, &initializer_block)) {
        minic_parser_error(parser, "cannot create scalar compound literal initializer block");
        return false;
    }

    initializer_span.begin = parser->current.span.begin;
    parent_block = parser->current_block;
    parser->current_block = initializer_block;
    success = minic_parser_advance(parser) &&
              minic_parser_parse_expression(parser, &value_id, 0U);
    if (success) {
        value = minic_c0_program_expression(parser->program, value_id);
        if (value == NULL) {
            success = false;
        } else if (!minic_c0_assignment_compatible(parser->program, type, value_id)) {
            MinicExpression conversion;

            if (minic_type_is_pointer(type) || minic_type_is_pointer(value->type) ||
                !minic_type_cast_compatible(type, value->type)) {
                minic_parser_error(parser, "scalar compound literal initializer type mismatch");
                success = false;
            } else {
                (void)memset(&conversion, 0, sizeof(conversion));
                conversion.kind = MINIC_EXPRESSION_CAST;
                conversion.span = value->span;
                conversion.type = type;
                conversion.value_category = MINIC_VALUE_RVALUE;
                conversion.value.unary.operand = value_id;
                success = minic_parser_add_expression(parser, &conversion, &value_id) &&
                          minic_c0_assignment_compatible(parser->program, type, value_id);
            }
        }
    }
    if (success && parser->current.kind == MINIC_TOKEN_COMMA) {
        success = minic_parser_advance(parser);
    }
    if (success && parser->current.kind != MINIC_TOKEN_RBRACE) {
        minic_parser_error(parser, "scalar compound literal requires exactly one initializer");
        success = false;
    }
    if (success) {
        value = minic_c0_program_expression(parser->program, value_id);
        if (value == NULL) {
            success = false;
        } else {
            initializer_span.end = parser->current.span.end;
            (void)memset(&assignment, 0, sizeof(assignment));
            assignment.kind = MINIC_STATEMENT_ASSIGN;
            assignment.span.begin = begin;
            assignment.span.end = value->span.end;
            assignment.target_expression = hidden_lvalue_id;
            assignment.expression = value_id;
            assignment.target_statement = MINIC_STATEMENT_INVALID;
            assignment.cleanup_context = parser->cleanup_context;
            assignment.cleanup_stop_context = MINIC_CLEANUP_CONTEXT_ROOT;
            assignment.then_block = MINIC_BLOCK_INVALID;
            assignment.else_block = MINIC_BLOCK_INVALID;
            success = minic_parser_add_statement(parser, &assignment) && minic_parser_advance(parser);
        }
    }
    parser->current_block = parent_block;
    if (!success) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot materialize scalar compound literal initializer");
        }
        return false;
    }

    (void)memset(&compound_literal, 0, sizeof(compound_literal));
    compound_literal.kind = MINIC_EXPRESSION_COMPOUND_LITERAL;
    compound_literal.span.begin = begin;
    compound_literal.span.end = initializer_span.end;
    compound_literal.type = type;
    compound_literal.value_category = MINIC_VALUE_LVALUE;
    compound_literal.value.compound_literal.local_id = local_id;
    compound_literal.value.compound_literal.initializer_block = initializer_block;
    if (!minic_parser_add_expression(parser, &compound_literal, &value_id)) {
        return false;
    }
    return minic_parser_parse_postfix(parser, value_id, expression_id);
}

'''
text = text.replace(anchor, helper + anchor, 1)
p.write_text(text)

replace_once(
    'src/frontend/parser_expression.c',
    '''    if (parser->current.kind == MINIC_TOKEN_LBRACE) {\n        return parse_record_compound_literal(parser, begin, target_type, expression_id);\n    }\n''',
    '''    if (parser->current.kind == MINIC_TOKEN_LBRACE) {\n        if (minic_type_is_record(target_type)) {\n            return parse_record_compound_literal(parser, begin, target_type, expression_id);\n        }\n        return parse_scalar_compound_literal(parser, begin, target_type, expression_id);\n    }\n''',
    'compound literal type dispatch',
)

replace_once(
    'src/frontend/ast_verifier.c',
    '''    case MINIC_EXPRESSION_COMPOUND_LITERAL: {\n        const MinicLocal *local;\n        const MinicBlock *initializer_block;\n        const MinicRecord *record;\n\n        local = minic_c0_program_local(program, expression->value.compound_literal.local_id);\n        initializer_block =\n            minic_c0_program_block(program, expression->value.compound_literal.initializer_block);\n        record = minic_type_is_record(expression->type)\n                     ? minic_c0_program_record(program, expression->type.record_id)\n                     : NULL;\n        return local != NULL && initializer_block != NULL && record != NULL &&\n               record->is_complete && expression->value_category == MINIC_VALUE_LVALUE &&\n               !local->is_array && !local->is_register_storage && local->element_count == 1U &&\n               minic_type_equal(local->type, expression->type);\n    }\n''',
    '''    case MINIC_EXPRESSION_COMPOUND_LITERAL: {\n        const MinicLocal *local;\n        const MinicBlock *initializer_block;\n        bool complete_object;\n\n        local = minic_c0_program_local(program, expression->value.compound_literal.local_id);\n        initializer_block =\n            minic_c0_program_block(program, expression->value.compound_literal.initializer_block);\n        complete_object = minic_c0_type_is_complete_object(program, expression->type);\n        return local != NULL && initializer_block != NULL && complete_object &&\n               !minic_type_is_array(expression->type) && !minic_type_is_function(expression->type) &&\n               !minic_type_is_void(expression->type) &&\n               expression->value_category == MINIC_VALUE_LVALUE && !local->is_array &&\n               !local->is_register_storage && local->element_count == 1U &&\n               minic_type_equal(local->type, expression->type);\n    }\n''',
    'compound literal verifier',
)

replace_once(
    'src/target/riscv64/codegen_expression.c',
    '''    case MINIC_EXPRESSION_COMPOUND_LITERAL:\n        return minic_riscv64_emit_lvalue_address(\n            file, program, function, function_layout, expression_id);\n''',
    '''    case MINIC_EXPRESSION_COMPOUND_LITERAL:\n        if (!minic_riscv64_emit_lvalue_address(\n                file, program, function, function_layout, expression_id)) {\n            return false;\n        }\n        if (minic_type_is_record(expression->type)) {\n            return true;\n        }\n        return minic_riscv64_emit_lvalue_load_from_address(\n            file, program, expression_id, expression->type, "a0", "a0");\n''',
    'scalar compound literal RV64 load',
)

run = Path('tests/compiler/c0/run.sh')
text = run.read_text()
if 'run-scalar-compound-literal.sh' not in text:
    text += '''\nMINIC="$minic" \\\nBUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\\nsh "$root/tests/compiler/c0/run-scalar-compound-literal.sh"\n'''
    run.write_text(text)

print('materialized scalar compound literal ownership')
