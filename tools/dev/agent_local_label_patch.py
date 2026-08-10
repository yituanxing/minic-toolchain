#!/usr/bin/env python3
from pathlib import Path


def replace_once(path, old, new, label):
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    p.write_text(text.replace(old, new, 1))


# Parser-scoped GNU local-label bindings remain separate from ordinary identifiers.
replace_once(
    "src/frontend/parser_internal.h",
    "typedef struct MinicParserEnumConstant {\n",
    "typedef struct MinicParserLocalLabel {\n"
    "    MinicSourceSpan name_span;\n"
    "    MinicStatementId statement_id;\n"
    "    size_t scope_depth;\n"
    "    bool is_active;\n"
    "    bool is_defined;\n"
    "} MinicParserLocalLabel;\n\n"
    "typedef struct MinicParserEnumConstant {\n",
    "local-label-binding-struct",
)
replace_once(
    "src/frontend/parser_internal.h",
    "    size_t function_statement_begin;\n\n"
    "    MinicParserLocalBinding *local_bindings;\n",
    "    size_t function_statement_begin;\n\n"
    "    MinicParserLocalLabel *local_labels;\n"
    "    size_t local_label_count;\n"
    "    size_t local_label_capacity;\n\n"
    "    MinicParserLocalBinding *local_bindings;\n",
    "local-label-parser-storage",
)
replace_once(
    "src/frontend/parser_internal.h",
    "bool minic_parser_bind_local(MinicParser *parser, MinicSourceSpan name_span, MinicLocalId local_id);\n",
    "bool minic_parser_declare_local_label(MinicParser *parser,\n"
    "                                      MinicSourceSpan name_span,\n"
    "                                      MinicStatementId statement_id);\n"
    "MinicStatementId minic_parser_find_local_label(const MinicParser *parser,\n"
    "                                                MinicSourceSpan name_span);\n"
    "bool minic_parser_define_local_label(MinicParser *parser,\n"
    "                                     MinicSourceSpan name_span,\n"
    "                                     MinicStatementId *statement_id);\n"
    "bool minic_parser_statement_is_local_label(const MinicParser *parser,\n"
    "                                           MinicStatementId statement_id);\n"
    "MinicStatementId minic_parser_find_label_statement(MinicParser *parser,\n"
    "                                                   MinicSourceSpan name_span);\n"
    "bool minic_parser_bind_local(MinicParser *parser, MinicSourceSpan name_span, MinicLocalId local_id);\n",
    "local-label-parser-api",
)

# Scope lifecycle activates/deactivates GNU local labels without collapsing their
# function-unique statement identity after a block exits.
replace_once(
    "src/frontend/parser_core.c",
    "void minic_parser_end_scope(MinicParser *parser) {\n"
    "    if (parser->scope_count == 0U) {\n"
    "        return;\n"
    "    }\n"
    "    parser->scope_count -= 1U;\n"
    "    parser->local_binding_count = parser->scope_binding_begins[parser->scope_count];\n"
    "}\n",
    "void minic_parser_end_scope(MinicParser *parser) {\n"
    "    size_t label_index;\n\n"
    "    if (parser->scope_count == 0U) {\n"
    "        return;\n"
    "    }\n"
    "    for (label_index = parser->local_label_count; label_index > 0U; --label_index) {\n"
    "        MinicParserLocalLabel *label;\n\n"
    "        label = &parser->local_labels[label_index - 1U];\n"
    "        if (label->is_active && label->scope_depth == parser->scope_count) {\n"
    "            label->is_active = false;\n"
    "        }\n"
    "    }\n"
    "    parser->scope_count -= 1U;\n"
    "    parser->local_binding_count = parser->scope_binding_begins[parser->scope_count];\n"
    "}\n",
    "local-label-scope-exit",
)
replace_once(
    "src/frontend/parser_core.c",
    "static bool minic_parser_bind_scoped_object(MinicParser *parser,\n",
    r'''bool minic_parser_declare_local_label(MinicParser *parser,
                                      MinicSourceSpan name_span,
                                      MinicStatementId statement_id) {
    MinicParserLocalLabel *label;
    size_t index;

    if (parser == NULL || parser->scope_count == 0U ||
        parser->current_function == MINIC_FUNCTION_INVALID ||
        statement_id == MINIC_STATEMENT_INVALID) {
        if (parser != NULL) {
            minic_parser_error(parser, "GNU local label requires an active function scope");
        }
        return false;
    }
    for (index = parser->local_label_count; index > 0U; --index) {
        const MinicParserLocalLabel *existing;

        existing = &parser->local_labels[index - 1U];
        if (existing->is_active && existing->scope_depth == parser->scope_count &&
            minic_parser_span_equals(parser, existing->name_span, name_span)) {
            minic_parser_error(parser, "duplicate GNU local label declaration");
            return false;
        }
    }
    if (parser->local_label_count == parser->local_label_capacity &&
        !minic_parser_grow_array((void **)&parser->local_labels,
                                 &parser->local_label_capacity,
                                 sizeof(*parser->local_labels))) {
        minic_parser_error(parser, "out of memory while declaring GNU local label");
        return false;
    }
    label = &parser->local_labels[parser->local_label_count];
    label->name_span = name_span;
    label->statement_id = statement_id;
    label->scope_depth = parser->scope_count;
    label->is_active = true;
    label->is_defined = false;
    parser->local_label_count += 1U;
    return true;
}

MinicStatementId minic_parser_find_local_label(const MinicParser *parser,
                                                MinicSourceSpan name_span) {
    size_t index;

    if (parser == NULL) {
        return MINIC_STATEMENT_INVALID;
    }
    for (index = parser->local_label_count; index > 0U; --index) {
        const MinicParserLocalLabel *label;

        label = &parser->local_labels[index - 1U];
        if (label->is_active && minic_parser_span_equals(parser, label->name_span, name_span)) {
            return label->statement_id;
        }
    }
    return MINIC_STATEMENT_INVALID;
}

bool minic_parser_define_local_label(MinicParser *parser,
                                     MinicSourceSpan name_span,
                                     MinicStatementId *statement_id) {
    size_t index;

    if (parser == NULL || statement_id == NULL) {
        return false;
    }
    for (index = parser->local_label_count; index > 0U; --index) {
        MinicParserLocalLabel *label;

        label = &parser->local_labels[index - 1U];
        if (!label->is_active || !minic_parser_span_equals(parser, label->name_span, name_span)) {
            continue;
        }
        if (label->is_defined) {
            minic_parser_error(parser, "duplicate GNU local label definition");
            return false;
        }
        label->is_defined = true;
        *statement_id = label->statement_id;
        return true;
    }
    return false;
}

bool minic_parser_statement_is_local_label(const MinicParser *parser,
                                           MinicStatementId statement_id) {
    size_t index;

    if (parser == NULL || statement_id == MINIC_STATEMENT_INVALID) {
        return false;
    }
    for (index = 0U; index < parser->local_label_count; ++index) {
        if (parser->local_labels[index].statement_id == statement_id) {
            return true;
        }
    }
    return false;
}

static bool minic_parser_bind_scoped_object(MinicParser *parser,
''',
    "local-label-scope-api",
)
replace_once(
    "src/frontend/parser_core.c",
    "void minic_parser_destroy_scopes(MinicParser *parser) {\n"
    "    free(parser->local_bindings);\n",
    "void minic_parser_destroy_scopes(MinicParser *parser) {\n"
    "    free(parser->local_labels);\n"
    "    parser->local_labels = NULL;\n"
    "    parser->local_label_count = 0U;\n"
    "    parser->local_label_capacity = 0U;\n"
    "    free(parser->local_bindings);\n",
    "local-label-destroy",
)

# Statement IDs remain the semantic label identity. Local declarations preallocate
# that identity; definitions only materialize it into the current block.
replace_once(
    "src/frontend/parser_statement.c",
    "        if (statement != NULL && statement->kind == MINIC_STATEMENT_LABEL &&\n"
    "            minic_parser_span_equals(parser, statement->span, name_span)) {\n"
    "            return statement_index;\n"
    "        }\n",
    "        if (statement != NULL && statement->kind == MINIC_STATEMENT_LABEL &&\n"
    "            !minic_parser_statement_is_local_label(parser, statement_index) &&\n"
    "            minic_parser_span_equals(parser, statement->span, name_span)) {\n"
    "            return statement_index;\n"
    "        }\n",
    "function-label-skip-local",
)
replace_once(
    "src/frontend/parser_statement.c",
    "static bool parse_goto(MinicParser *parser) {\n",
    r'''MinicStatementId minic_parser_find_label_statement(MinicParser *parser,
                                                   MinicSourceSpan name_span) {
    MinicStatementId local_label;

    local_label = minic_parser_find_local_label(parser, name_span);
    return local_label != MINIC_STATEMENT_INVALID ? local_label
                                                   : find_function_label(parser, name_span);
}

static bool current_identifier_is_local_label_keyword(const MinicParser *parser) {
    return parser->current.kind == MINIC_TOKEN_IDENTIFIER &&
           identifier_equals(parser, parser->current.span, "__label__", 9U);
}

static bool parse_gnu_local_label_declaration(MinicParser *parser) {
    if (!current_identifier_is_local_label_keyword(parser) || !minic_parser_advance(parser)) {
        return false;
    }
    for (;;) {
        MinicStatement label;
        MinicStatementId statement_id;
        MinicSourceSpan name_span;

        if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
            minic_parser_error(parser, "expected label name after __label__");
            return false;
        }
        name_span = parser->current.span;
        (void)memset(&label, 0, sizeof(label));
        label.kind = MINIC_STATEMENT_LABEL;
        label.span = name_span;
        label.target_expression = MINIC_EXPRESSION_INVALID;
        label.expression = MINIC_EXPRESSION_INVALID;
        label.target_statement = MINIC_STATEMENT_INVALID;
        label.then_block = MINIC_BLOCK_INVALID;
        label.else_block = MINIC_BLOCK_INVALID;
        if (!minic_c0_program_add_statement(parser->program, &label, &statement_id) ||
            !minic_parser_declare_local_label(parser, name_span, statement_id) ||
            !minic_parser_advance(parser)) {
            return false;
        }
        if (parser->current.kind != MINIC_TOKEN_COMMA) {
            break;
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after GNU local label declaration");
}

static bool parse_goto(MinicParser *parser) {
''',
    "local-label-declaration-parser",
)
replace_once(
    "src/frontend/parser_statement.c",
    "    statement.target_statement = find_function_label(parser, name_span);\n",
    "    statement.target_statement = minic_parser_find_label_statement(parser, name_span);\n",
    "goto-local-label-resolution",
)

# Replace parse_label as one bounded unit to keep standard and local-label paths explicit.
start = Path("src/frontend/parser_statement.c")
text = start.read_text()
begin = text.index("static bool parse_label(MinicParser *parser, bool allow_declaration) {")
end = text.index("\nstatic bool parse_break(MinicParser *parser) {", begin)
new_parse_label = r'''static bool parse_label(MinicParser *parser, bool allow_declaration) {
    MinicStatement statement;
    MinicSourceSpan name_span;
    MinicStatementId label_statement_id;
    MinicStatementId local_label_id;
    size_t statement_index;
    bool is_local_label;

    name_span = parser->current.span;
    local_label_id = minic_parser_find_local_label(parser, name_span);
    is_local_label = local_label_id != MINIC_STATEMENT_INVALID;
    if (!is_local_label && find_function_label(parser, name_span) != MINIC_STATEMENT_INVALID) {
        minic_parser_error(parser, "duplicate label definition");
        return false;
    }

    if (is_local_label) {
        MinicStatement *local_label;

        if (!minic_parser_define_local_label(parser, name_span, &label_statement_id)) {
            return false;
        }
        local_label = &parser->program->statements[label_statement_id];
        local_label->span = name_span;
    } else {
        (void)memset(&statement, 0, sizeof(statement));
        statement.kind = MINIC_STATEMENT_LABEL;
        statement.span = name_span;
        statement.target_expression = MINIC_EXPRESSION_INVALID;
        statement.expression = MINIC_EXPRESSION_INVALID;
        statement.target_statement = MINIC_STATEMENT_INVALID;
        statement.then_block = MINIC_BLOCK_INVALID;
        statement.else_block = MINIC_BLOCK_INVALID;
        label_statement_id = parser->program->statement_count;
    }

    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_COLON, "expected ':' after label")) {
        return false;
    }
    if (is_local_label) {
        if (!minic_c0_block_add_statement(
                parser->program, parser->current_block, label_statement_id)) {
            minic_parser_error(parser, "cannot materialize GNU local label definition");
            return false;
        }
    } else if (!minic_parser_add_statement(parser, &statement)) {
        return false;
    }

    if (!is_local_label) {
        for (statement_index = parser->function_statement_begin;
             statement_index < label_statement_id;
             ++statement_index) {
            MinicStatement *pending;

            pending = &parser->program->statements[statement_index];
            if (pending->kind == MINIC_STATEMENT_GOTO &&
                pending->target_statement == MINIC_STATEMENT_INVALID &&
                minic_parser_span_equals(parser, pending->span, name_span)) {
                pending->target_statement = label_statement_id;
            }
        }
    }

    if (parser->current.kind == MINIC_TOKEN_RBRACE || parser->current.kind == MINIC_TOKEN_EOF) {
        minic_parser_error(parser, "label must be followed by a statement");
        return false;
    }
    return minic_parser_parse_statement(parser, allow_declaration);
}
'''
start.write_text(text[:begin] + new_parse_label + text[end:])

replace_once(
    "src/frontend/parser_statement.c",
    "           kind == MINIC_TOKEN_BANG || kind == MINIC_TOKEN_AMPERSAND || kind == MINIC_TOKEN_STAR;\n",
    "           kind == MINIC_TOKEN_BANG || kind == MINIC_TOKEN_AMPERSAND ||\n"
    "           kind == MINIC_TOKEN_AMPERSAND_AMPERSAND || kind == MINIC_TOKEN_STAR;\n",
    "label-address-expression-start",
)
replace_once(
    "src/frontend/parser_statement.c",
    "    if (current_identifier_is_goto(parser)) {\n"
    "        return parse_goto(parser);\n"
    "    }\n",
    "    if (current_identifier_is_local_label_keyword(parser)) {\n"
    "        if (!allow_declaration) {\n"
    "            minic_parser_error(parser, \"GNU local label requires a compound statement scope\");\n"
    "            return false;\n"
    "        }\n"
    "        return parse_gnu_local_label_declaration(parser);\n"
    "    }\n"
    "    if (current_identifier_is_goto(parser)) {\n"
    "        return parse_goto(parser);\n"
    "    }\n",
    "local-label-statement-dispatch",
)

# Semantic AST label-address expression is a leaf keyed by the existing label statement ID.
replace_once(
    "src/frontend/ast.h",
    "    MINIC_EXPRESSION_FUNCTION,\n    MINIC_EXPRESSION_SIZEOF,\n",
    "    MINIC_EXPRESSION_FUNCTION,\n    MINIC_EXPRESSION_LABEL_ADDRESS,\n    MINIC_EXPRESSION_SIZEOF,\n",
    "label-address-expression-kind",
)
replace_once(
    "src/frontend/ast.h",
    "        MinicFunctionId function_id;\n        MinicType sizeof_type;\n",
    "        MinicFunctionId function_id;\n"
    "        MinicStatementId label_statement_id;\n"
    "        MinicType sizeof_type;\n",
    "label-address-expression-payload",
)

# Parse GNU &&label before ordinary unary operators; it has void * type in GNU C.
replace_once(
    "src/frontend/parser_expression.c",
    "static bool parse_unary(MinicParser *parser, MinicExpressionId *expression_id, bool decay_array) {\n",
    r'''static bool parse_label_address(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicExpression expression;
    MinicSourcePosition begin;
    MinicSourceSpan name_span;
    MinicStatementId statement_id;

    if (parser == NULL || expression_id == NULL ||
        parser->current.kind != MINIC_TOKEN_AMPERSAND_AMPERSAND) {
        return false;
    }
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected label name after '&&'");
        return false;
    }
    name_span = parser->current.span;
    statement_id = minic_parser_find_label_statement(parser, name_span);
    if (statement_id == MINIC_STATEMENT_INVALID) {
        minic_parser_error(parser, "address of unknown label");
        return false;
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_LABEL_ADDRESS;
    expression.span.begin = begin;
    expression.span.end = name_span.end;
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.label_statement_id = statement_id;
    if (!minic_type_pointer_to(minic_type_void(), &expression.type)) {
        minic_parser_error(parser, "cannot form GNU label-address type");
        return false;
    }
    return minic_parser_advance(parser) &&
           minic_parser_add_expression(parser, &expression, expression_id);
}

static bool parse_unary(MinicParser *parser, MinicExpressionId *expression_id, bool decay_array) {
''',
    "label-address-parser",
)
replace_once(
    "src/frontend/parser_expression.c",
    "    if (current_is_alignof(parser)) {\n"
    "        return parse_alignof(parser, expression_id);\n"
    "    }\n"
    "    if (parenthesis_starts_cast(parser)) {\n",
    "    if (current_is_alignof(parser)) {\n"
    "        return parse_alignof(parser, expression_id);\n"
    "    }\n"
    "    if (parser->current.kind == MINIC_TOKEN_AMPERSAND_AMPERSAND) {\n"
    "        return parse_label_address(parser, expression_id);\n"
    "    }\n"
    "    if (parenthesis_starts_cast(parser)) {\n",
    "label-address-unary-dispatch",
)

# Verifier and cast normalizer treat label address as a typed leaf.
replace_once(
    "src/frontend/ast_verifier.c",
    "    case MINIC_EXPRESSION_SIZEOF: {\n",
    r'''    case MINIC_EXPRESSION_LABEL_ADDRESS: {
        const MinicStatement *label;
        MinicType pointee;

        label = minic_c0_program_statement(program, expression->value.label_statement_id);
        return label != NULL && label->kind == MINIC_STATEMENT_LABEL &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_pointee(expression->type, &pointee) && minic_type_is_void(pointee);
    }
    case MINIC_EXPRESSION_SIZEOF: {
''',
    "label-address-verifier",
)
replace_once(
    "src/frontend/cast_normalization.c",
    "    case MINIC_EXPRESSION_FUNCTION:\n"
    "    case MINIC_EXPRESSION_SIZEOF:\n",
    "    case MINIC_EXPRESSION_FUNCTION:\n"
    "    case MINIC_EXPRESSION_LABEL_ADDRESS:\n"
    "    case MINIC_EXPRESSION_SIZEOF:\n",
    "label-address-normalization-leaf",
)

# RV64 materializes the address of the same user-label statement identity used by goto.
replace_once(
    "src/target/riscv64/codegen_expression.c",
    "    case MINIC_EXPRESSION_SIZEOF: {\n",
    r'''    case MINIC_EXPRESSION_LABEL_ADDRESS: {
        const MinicStatement *label;
        MinicType pointee;

        label = minic_c0_program_statement(program, expression->value.label_statement_id);
        return label != NULL && label->kind == MINIC_STATEMENT_LABEL &&
               minic_type_pointee(expression->type, &pointee) && minic_type_is_void(pointee) &&
               fprintf(file,
                       "  la a0, .Luser_%zu\n",
                       (size_t)expression->value.label_statement_id) >= 0;
    }
    case MINIC_EXPRESSION_SIZEOF: {
''',
    "label-address-rv64-lowering",
)
