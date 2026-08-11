from pathlib import Path


def replace_once(path_name: str, old: str, new: str) -> None:
    path = Path(path_name)
    source = path.read_text()
    if old not in source:
        if new in source:
            return
        raise SystemExit(f"patch anchor not found: {path_name}: {old[:80]!r}")
    path.write_text(source.replace(old, new, 1))


# ---- Program-owned cleanup contexts -------------------------------------------------
path = "src/frontend/ast.h"
replace_once(
    path,
    "typedef size_t MinicInlineAsmId;\n",
    "typedef size_t MinicInlineAsmId;\ntypedef size_t MinicCleanupContextId;\n",
)
replace_once(
    path,
    "#define MINIC_INLINE_ASM_INVALID ((MinicInlineAsmId) - 1)\n",
    "#define MINIC_INLINE_ASM_INVALID ((MinicInlineAsmId) - 1)\n"
    "#define MINIC_CLEANUP_CONTEXT_ROOT ((MinicCleanupContextId)0)\n",
)
replace_once(
    path,
    """typedef struct MinicLocal {
    MinicSourceSpan name_span;
    MinicType type;
    size_t element_count;
    size_t storage_offset;
    bool is_array;
    bool is_register_storage;
} MinicLocal;

typedef enum MinicStatementKind {
""",
    """typedef struct MinicLocal {
    MinicSourceSpan name_span;
    MinicType type;
    size_t element_count;
    size_t storage_offset;
    bool is_array;
    bool is_register_storage;
} MinicLocal;

typedef struct MinicCleanupContext {
    MinicCleanupContextId parent;
    MinicExpressionId cleanup_expression;
} MinicCleanupContext;

typedef enum MinicStatementKind {
""",
)
replace_once(
    path,
    """    MinicStatementId target_statement;
    MinicInlineAsmId inline_asm_id;
    MinicBlockId then_block;
""",
    """    MinicStatementId target_statement;
    MinicInlineAsmId inline_asm_id;
    MinicCleanupContextId cleanup_context;
    MinicCleanupContextId cleanup_stop_context;
    MinicBlockId then_block;
""",
)
replace_once(
    path,
    """    MinicLocal *locals;
    size_t local_count;
    size_t local_capacity;

    MinicStatement *statements;
""",
    """    MinicLocal *locals;
    size_t local_count;
    size_t local_capacity;

    MinicCleanupContext *cleanup_contexts;
    size_t cleanup_context_count;
    size_t cleanup_context_capacity;

    MinicStatement *statements;
""",
)
replace_once(
    path,
    """bool minic_c0_program_add_local(MinicC0Program *program,
                                const MinicLocal *local,
                                MinicLocalId *local_id);
bool minic_c0_program_add_statement(MinicC0Program *program,
""",
    """bool minic_c0_program_add_local(MinicC0Program *program,
                                const MinicLocal *local,
                                MinicLocalId *local_id);
bool minic_c0_program_add_cleanup_context(MinicC0Program *program,
                                          MinicCleanupContextId parent,
                                          MinicExpressionId cleanup_expression,
                                          MinicCleanupContextId *cleanup_context_id);
bool minic_c0_cleanup_context_reaches(const MinicC0Program *program,
                                      MinicCleanupContextId current,
                                      MinicCleanupContextId stop);
bool minic_c0_program_add_statement(MinicC0Program *program,
""",
)
replace_once(
    path,
    """/* Program entity accessors return borrowed pointers into growable owner arrays.
 * IDs remain stable, but growing the same entity array may relocate its storage.
 * Keep an ID or copy required value fields across any operation that may grow that pool. */
/* Program entity accessors return borrowed pointers into growable owner arrays.
 * IDs remain stable, but growing the same entity array may relocate its storage.
 * Keep an ID or copy required value fields across any operation that may grow that pool. */
""",
    """/* Program entity accessors return borrowed pointers into growable owner arrays.
 * IDs remain stable, but growing the same entity array may relocate its storage.
 * Keep an ID or copy required value fields across any operation that may grow that pool. */
""",
)
replace_once(
    path,
    """const MinicLocal *minic_c0_program_local(const MinicC0Program *program, MinicLocalId local_id);
const MinicStatement *minic_c0_program_statement(const MinicC0Program *program,
""",
    """const MinicLocal *minic_c0_program_local(const MinicC0Program *program, MinicLocalId local_id);
const MinicCleanupContext *minic_c0_program_cleanup_context(const MinicC0Program *program,
                                                            MinicCleanupContextId cleanup_context_id);
const MinicStatement *minic_c0_program_statement(const MinicC0Program *program,
""",
)

path = "src/frontend/ast.c"
replace_once(
    path,
    """    free(program->expressions);
    free(program->locals);
    free(program->statements);
""",
    """    free(program->expressions);
    free(program->locals);
    free(program->cleanup_contexts);
    free(program->statements);
""",
)
replace_once(
    path,
    """bool minic_c0_program_add_statement(MinicC0Program *program,
                                    const MinicStatement *statement,
                                    MinicStatementId *statement_id) {
""",
    """bool minic_c0_program_add_cleanup_context(MinicC0Program *program,
                                          MinicCleanupContextId parent,
                                          MinicExpressionId cleanup_expression,
                                          MinicCleanupContextId *cleanup_context_id) {
    MinicCleanupContext context;

    if (program == NULL || cleanup_context_id == NULL ||
        parent > program->cleanup_context_count || cleanup_expression >= program->expression_count ||
        !minic_grow_array((void **)&program->cleanup_contexts,
                          &program->cleanup_context_capacity,
                          program->cleanup_context_count,
                          sizeof(*program->cleanup_contexts))) {
        return false;
    }
    context.parent = parent;
    context.cleanup_expression = cleanup_expression;
    program->cleanup_contexts[program->cleanup_context_count] = context;
    program->cleanup_context_count += 1U;
    *cleanup_context_id = program->cleanup_context_count;
    return true;
}

const MinicCleanupContext *minic_c0_program_cleanup_context(
    const MinicC0Program *program, MinicCleanupContextId cleanup_context_id) {
    if (program == NULL || cleanup_context_id == MINIC_CLEANUP_CONTEXT_ROOT ||
        cleanup_context_id > program->cleanup_context_count) {
        return NULL;
    }
    return &program->cleanup_contexts[cleanup_context_id - 1U];
}

bool minic_c0_cleanup_context_reaches(const MinicC0Program *program,
                                      MinicCleanupContextId current,
                                      MinicCleanupContextId stop) {
    if (program == NULL || current > program->cleanup_context_count ||
        stop > program->cleanup_context_count) {
        return false;
    }
    while (current != stop) {
        const MinicCleanupContext *context;

        context = minic_c0_program_cleanup_context(program, current);
        if (context == NULL) {
            return false;
        }
        current = context->parent;
    }
    return true;
}

bool minic_c0_program_add_statement(MinicC0Program *program,
                                    const MinicStatement *statement,
                                    MinicStatementId *statement_id) {
""",
)

# ---- Attribute registry -------------------------------------------------------------
path = "src/frontend/attribute.h"
replace_once(
    path,
    """    MINIC_ATTRIBUTE_PACKED,
    MINIC_ATTRIBUTE_ALIGNED
""",
    """    MINIC_ATTRIBUTE_PACKED,
    MINIC_ATTRIBUTE_ALIGNED,
    MINIC_ATTRIBUTE_CLEANUP
""",
)

path = "src/frontend/attribute.c"
replace_once(
    path,
    """    MINIC_ATTRIBUTE_ENTRY("__aligned__",
                          MINIC_ATTRIBUTE_ALIGNED,
                          MINIC_ATTRIBUTE_CLASS_LAYOUT,
                          MINIC_ATTRIBUTE_TARGET_OBJECT | MINIC_ATTRIBUTE_TARGET_TYPE |
                              MINIC_ATTRIBUTE_TARGET_FIELD),
};
""",
    """    MINIC_ATTRIBUTE_ENTRY("__aligned__",
                          MINIC_ATTRIBUTE_ALIGNED,
                          MINIC_ATTRIBUTE_CLASS_LAYOUT,
                          MINIC_ATTRIBUTE_TARGET_OBJECT | MINIC_ATTRIBUTE_TARGET_TYPE |
                              MINIC_ATTRIBUTE_TARGET_FIELD),
    {
        "cleanup",
        sizeof("cleanup") - 1U,
        MINIC_ATTRIBUTE_CLEANUP,
        MINIC_ATTRIBUTE_CLASS_LANGUAGE_SEMANTIC,
        MINIC_ATTRIBUTE_TARGET_OBJECT,
        1U,
        1U,
        true,
    },
    {
        "__cleanup__",
        sizeof("__cleanup__") - 1U,
        MINIC_ATTRIBUTE_CLEANUP,
        MINIC_ATTRIBUTE_CLASS_LANGUAGE_SEMANTIC,
        MINIC_ATTRIBUTE_TARGET_OBJECT,
        1U,
        1U,
        true,
    },
};
""",
)

# ---- Parser lexical frames and cleanup state ----------------------------------------
path = "src/frontend/parser_internal.h"
replace_once(
    path,
    """typedef struct MinicParserLocalLabel {
""",
    """typedef struct MinicParserScopeFrame {
    size_t binding_begin;
    MinicCleanupContextId cleanup_context;
} MinicParserScopeFrame;

typedef struct MinicParserLocalLabel {
""",
)
replace_once(
    path,
    """    size_t loop_depth;
    MinicStatementId continue_target_statement;
    size_t switch_depth;
""",
    """    size_t loop_depth;
    MinicStatementId continue_target_statement;
    MinicCleanupContextId cleanup_context;
    MinicCleanupContextId break_cleanup_context;
    MinicCleanupContextId continue_cleanup_context;
    size_t statement_expression_depth;
    size_t switch_depth;
""",
)
replace_once(
    path,
    """    size_t *scope_binding_begins;
    size_t scope_count;
    size_t scope_capacity;
""",
    """    MinicParserScopeFrame *scopes;
    size_t scope_count;
    size_t scope_capacity;
""",
)
replace_once(
    path,
    """bool minic_parser_add_statement(MinicParser *parser, const MinicStatement *statement);

bool minic_parser_begin_scope(MinicParser *parser);
""",
    """bool minic_parser_add_statement(MinicParser *parser, const MinicStatement *statement);
bool minic_parser_materialize_cleanup_contexts(MinicParser *parser,
                                               MinicCleanupContextId stop_context);

bool minic_parser_begin_scope(MinicParser *parser);
""",
)

path = "src/frontend/parser_core.c"
replace_once(
    path,
    """bool minic_parser_begin_scope(MinicParser *parser) {
    if (parser->scope_count == parser->scope_capacity &&
        !minic_parser_grow_array((void **)&parser->scope_binding_begins,
                                 &parser->scope_capacity,
                                 sizeof(*parser->scope_binding_begins))) {
        minic_parser_error(parser, "out of memory while entering scope");
        return false;
    }
    parser->scope_binding_begins[parser->scope_count] = parser->local_binding_count;
    parser->scope_count += 1U;
    return true;
}
""",
    """bool minic_parser_begin_scope(MinicParser *parser) {
    MinicParserScopeFrame *scope;

    if (parser->scope_count == parser->scope_capacity &&
        !minic_parser_grow_array(
            (void **)&parser->scopes, &parser->scope_capacity, sizeof(*parser->scopes))) {
        minic_parser_error(parser, "out of memory while entering scope");
        return false;
    }
    scope = &parser->scopes[parser->scope_count];
    scope->binding_begin = parser->local_binding_count;
    scope->cleanup_context = parser->cleanup_context;
    parser->scope_count += 1U;
    return true;
}
""",
)
replace_once(
    path,
    """    parser->scope_count -= 1U;
    parser->local_binding_count = parser->scope_binding_begins[parser->scope_count];
}
""",
    """    parser->scope_count -= 1U;
    parser->local_binding_count = parser->scopes[parser->scope_count].binding_begin;
    parser->cleanup_context = parser->scopes[parser->scope_count].cleanup_context;
}
""",
)
replace_once(
    path,
    "scope_begin = parser->scope_binding_begins[parser->scope_count - 1U];",
    "scope_begin = parser->scopes[parser->scope_count - 1U].binding_begin;",
)
replace_once(
    path,
    "scope_begin = parser->scope_binding_begins[parser->scope_count - 1U];",
    "scope_begin = parser->scopes[parser->scope_count - 1U].binding_begin;",
)
replace_once(
    path,
    """    free(parser->local_bindings);
    free(parser->scope_binding_begins);
    parser->local_bindings = NULL;
""",
    """    free(parser->local_bindings);
    free(parser->scopes);
    parser->local_bindings = NULL;
""",
)
replace_once(
    path,
    """    parser->scope_binding_begins = NULL;
    parser->scope_count = 0U;
""",
    """    parser->scopes = NULL;
    parser->scope_count = 0U;
""",
)
replace_once(
    path,
    """bool minic_parser_begin_scope(MinicParser *parser) {
""",
    """bool minic_parser_materialize_cleanup_contexts(MinicParser *parser,
                                               MinicCleanupContextId stop_context) {
    MinicCleanupContextId current;

    if (parser == NULL ||
        !minic_c0_cleanup_context_reaches(parser->program, parser->cleanup_context, stop_context)) {
        if (parser != NULL) {
            minic_parser_error(parser, "invalid cleanup lifetime exit");
        }
        return false;
    }
    current = parser->cleanup_context;
    while (current != stop_context) {
        const MinicCleanupContext *context;
        const MinicExpression *expression;
        MinicStatement statement;

        context = minic_c0_program_cleanup_context(parser->program, current);
        expression = context == NULL
                         ? NULL
                         : minic_c0_program_expression(parser->program, context->cleanup_expression);
        if (context == NULL || expression == NULL) {
            minic_parser_error(parser, "invalid cleanup lifetime context");
            return false;
        }
        (void)memset(&statement, 0, sizeof(statement));
        statement.kind = MINIC_STATEMENT_EXPRESSION;
        statement.span = expression->span;
        statement.target_expression = MINIC_EXPRESSION_INVALID;
        statement.expression = context->cleanup_expression;
        statement.target_statement = MINIC_STATEMENT_INVALID;
        statement.inline_asm_id = MINIC_INLINE_ASM_INVALID;
        statement.cleanup_context = MINIC_CLEANUP_CONTEXT_ROOT;
        statement.cleanup_stop_context = MINIC_CLEANUP_CONTEXT_ROOT;
        statement.then_block = MINIC_BLOCK_INVALID;
        statement.else_block = MINIC_BLOCK_INVALID;
        if (!minic_parser_add_statement(parser, &statement)) {
            return false;
        }
        current = context->parent;
    }
    return true;
}

bool minic_parser_begin_scope(MinicParser *parser) {
""",
)

# ---- Local cleanup attribute semantics ----------------------------------------------
path = "src/frontend/parser_statement.c"
replace_once(
    path,
    """static bool consume_local_object_attribute(MinicParser *parser,
                                           const MinicParsedAttribute *attribute,
                                           void *context) {
    const MinicAttributeDescriptor *descriptor;

    (void)context;
""",
    """typedef struct MinicLocalObjectAttributes {
    MinicFunctionId cleanup_function;
    MinicSourceSpan cleanup_attribute_span;
} MinicLocalObjectAttributes;

static bool parse_cleanup_attribute_function(MinicParser *parser,
                                             const MinicParsedAttribute *attribute,
                                             MinicFunctionId *function_id) {
    MinicParser probe;
    MinicSourceSpan name_span;

    if (parser == NULL || attribute == NULL || function_id == NULL ||
        !attribute->has_arguments ||
        attribute->arguments_span.end.offset <= attribute->arguments_span.begin.offset + 1U) {
        return false;
    }
    probe = *parser;
    minic_lexer_initialize(&probe.lexer, parser->path, parser->source, parser->lexer.length);
    probe.lexer.cursor = attribute->arguments_span.begin.offset + 1U;
    probe.lexer.line = attribute->arguments_span.begin.line;
    probe.lexer.column = attribute->arguments_span.begin.column + 1U;
    if (!minic_parser_advance(&probe) || probe.current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "GNU cleanup attribute requires one function name");
        return false;
    }
    name_span = probe.current.span;
    if (!minic_parser_advance(&probe) || probe.current.kind != MINIC_TOKEN_RPAREN ||
        probe.current.span.end.offset != attribute->arguments_span.end.offset) {
        minic_parser_error(parser, "GNU cleanup attribute requires one function name");
        return false;
    }
    *function_id = minic_parser_find_function(parser, name_span);
    if (*function_id == MINIC_FUNCTION_INVALID) {
        minic_parser_error(parser, "GNU cleanup function must be declared before the local object");
        return false;
    }
    return true;
}

static bool consume_local_object_attribute(MinicParser *parser,
                                           const MinicParsedAttribute *attribute,
                                           void *context) {
    MinicLocalObjectAttributes *attributes;
    const MinicAttributeDescriptor *descriptor;

    attributes = (MinicLocalObjectAttributes *)context;
""",
)
replace_once(
    path,
    """    if (attribute->has_arguments ||
        descriptor->semantic_class != MINIC_ATTRIBUTE_CLASS_INFORMATIONAL) {
        minic_parser_error(parser, "local object attribute semantics are not supported yet");
        return false;
    }
    return true;
}

static bool parse_local_object_attributes(MinicParser *parser) {
    return minic_parser_parse_gnu_attribute_lists(parser, consume_local_object_attribute, NULL);
}
""",
    """    if (descriptor->kind == MINIC_ATTRIBUTE_CLEANUP) {
        if (attributes == NULL || parser->statement_expression_depth != 0U) {
            minic_parser_error(
                parser,
                parser->statement_expression_depth != 0U
                    ? "GNU cleanup inside a statement expression is not supported yet"
                    : "invalid GNU cleanup attribute context");
            return false;
        }
        if (attributes->cleanup_function != MINIC_FUNCTION_INVALID) {
            minic_parser_error(parser, "local object may have only one GNU cleanup function");
            return false;
        }
        if (!parse_cleanup_attribute_function(parser, attribute, &attributes->cleanup_function)) {
            return false;
        }
        attributes->cleanup_attribute_span = attribute->name_span;
        return true;
    }
    if (attribute->has_arguments ||
        descriptor->semantic_class != MINIC_ATTRIBUTE_CLASS_INFORMATIONAL) {
        minic_parser_error(parser, "local object attribute semantics are not supported yet");
        return false;
    }
    return true;
}

static bool parse_local_object_attributes(MinicParser *parser,
                                          MinicLocalObjectAttributes *attributes) {
    return minic_parser_parse_gnu_attribute_lists(parser, consume_local_object_attribute, attributes);
}

static bool finalize_local_cleanup(MinicParser *parser,
                                   const MinicLocalObjectAttributes *attributes,
                                   const MinicLocal *local,
                                   MinicLocalId local_id) {
    MinicExpression address;
    MinicExpression call;
    MinicExpressionId address_id;
    MinicExpressionId call_id;
    MinicExpressionId local_expression_id;
    MinicCleanupContextId cleanup_context_id;
    MinicFunction function;
    const MinicFunction *function_borrow;
    MinicType pointer_type;

    if (attributes == NULL || local == NULL ||
        attributes->cleanup_function == MINIC_FUNCTION_INVALID) {
        return true;
    }
    if (local->is_register_storage) {
        minic_parser_error(parser, "GNU cleanup cannot be applied to a register local");
        return false;
    }
    if (local->is_array) {
        minic_parser_error(parser, "GNU cleanup on local arrays is not supported yet");
        return false;
    }
    function_borrow = minic_c0_program_function(parser->program, attributes->cleanup_function);
    if (function_borrow == NULL) {
        minic_parser_error(parser, "invalid GNU cleanup function");
        return false;
    }
    function = *function_borrow;
    if (function.parameter_count != 1U || function.is_variadic ||
        !minic_type_pointer_to(local->type, &pointer_type) ||
        !minic_c0_types_compatible(parser->program, function.parameter_types[0], pointer_type)) {
        minic_parser_error(parser, "GNU cleanup function must accept a pointer to the local type");
        return false;
    }
    if (!add_local_lvalue_expression(parser, local_id, local->name_span, &local_expression_id)) {
        return false;
    }

    (void)memset(&address, 0, sizeof(address));
    address.kind = MINIC_EXPRESSION_ADDRESS_OF;
    address.span = local->name_span;
    address.type = pointer_type;
    address.value_category = MINIC_VALUE_RVALUE;
    address.value.unary.operand = local_expression_id;
    if (!minic_parser_add_expression(parser, &address, &address_id)) {
        return false;
    }

    (void)memset(&call, 0, sizeof(call));
    call.kind = MINIC_EXPRESSION_CALL;
    call.span = attributes->cleanup_attribute_span;
    call.type = function.return_type;
    call.value_category = MINIC_VALUE_RVALUE;
    call.value.call.function_id = attributes->cleanup_function;
    call.value.call.callee = MINIC_EXPRESSION_INVALID;
    call.value.call.argument_count = 1U;
    call.value.call.arguments[0] = address_id;
    if (!minic_parser_add_expression(parser, &call, &call_id) ||
        !minic_c0_program_add_cleanup_context(parser->program,
                                              parser->cleanup_context,
                                              call_id,
                                              &cleanup_context_id)) {
        minic_parser_error(parser, "cannot record GNU cleanup lifetime");
        return false;
    }
    parser->cleanup_context = cleanup_context_id;
    return true;
}
""",
)
replace_once(
    path,
    """    MinicLocal local;
    MinicLocalId local_id;
    MinicType declared_type;

    if (!minic_parser_parse_pointer_declarator(parser, base_type, &declared_type)) {
""",
    """    MinicLocal local;
    MinicLocalId local_id;
    MinicLocalObjectAttributes attributes;
    MinicType declared_type;

    (void)memset(&attributes, 0, sizeof(attributes));
    attributes.cleanup_function = MINIC_FUNCTION_INVALID;
    if (!minic_parser_parse_pointer_declarator(parser, base_type, &declared_type)) {
""",
)
replace_once(
    path,
    """    if (!parse_local_object_attributes(parser)) {
""",
    """    if (!parse_local_object_attributes(parser, &attributes)) {
""",
)
replace_once(
    path,
    """        if (local.element_count != 1U) {
            return parse_local_array_zero_initializer(parser, local_id, local.name_span);
        }
""",
    """        if (local.element_count != 1U) {
            if (!parse_local_array_zero_initializer(parser, local_id, local.name_span)) {
                return false;
            }
            return finalize_local_cleanup(parser, &attributes, &local, local_id);
        }
""",
)
replace_once(
    path,
    """            if (parser->current.kind == MINIC_TOKEN_LBRACE) {
                return minic_parser_parse_runtime_record_initializer(parser, target_id);
            } else {
""",
    """            if (parser->current.kind == MINIC_TOKEN_LBRACE) {
                if (!minic_parser_parse_runtime_record_initializer(parser, target_id)) {
                    return false;
                }
                return finalize_local_cleanup(parser, &attributes, &local, local_id);
            } else {
""",
)
replace_once(
    path,
    """                return add_record_copy_assignments(parser, target_id, source_id, source->span);
            }
""",
    """                if (!add_record_copy_assignments(parser, target_id, source_id, source->span)) {
                    return false;
                }
                return finalize_local_cleanup(parser, &attributes, &local, local_id);
            }
""",
)
replace_once(
    path,
    """        if (!minic_parser_add_statement(parser, &statement)) {
            return false;
        }
    }
    return true;
}

static bool current_identifier_is_auto_type""",
    """        if (!minic_parser_add_statement(parser, &statement)) {
            return false;
        }
    }
    return finalize_local_cleanup(parser, &attributes, &local, local_id);
}

static bool current_identifier_is_auto_type""",
)

# Normal lexical scope exit.
replace_once(
    path,
    """static bool parse_compound_statement(MinicParser *parser) {
    bool success;

    if (parser->current.kind != MINIC_TOKEN_LBRACE) {
""",
    """static bool parse_compound_statement(MinicParser *parser) {
    MinicCleanupContextId scope_cleanup_context;
    bool success;

    if (parser->current.kind != MINIC_TOKEN_LBRACE) {
""",
)
replace_once(
    path,
    """    if (!minic_parser_begin_scope(parser)) {
        return false;
    }

    success = minic_parser_advance(parser);
""",
    """    scope_cleanup_context = parser->cleanup_context;
    if (!minic_parser_begin_scope(parser)) {
        return false;
    }

    success = minic_parser_advance(parser);
""",
)
replace_once(
    path,
    """    if (success) {
        success = minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}'");
    }

    minic_parser_end_scope(parser);
""",
    """    if (success) {
        success = minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}'") &&
                  minic_parser_materialize_cleanup_contexts(parser, scope_cleanup_context);
    }

    minic_parser_end_scope(parser);
""",
)

# Statement expressions deliberately fail closed for local cleanup declarations in v0.
replace_once(
    path,
    """    parser->current_block = block_id;
    success = minic_parser_advance(parser);
""",
    """    parser->current_block = block_id;
    parser->statement_expression_depth += 1U;
    success = minic_parser_advance(parser);
""",
)
replace_once(
    path,
    """    parser->current_block = parent_block;
    minic_parser_end_scope(parser);
    return success;
}

static bool parse_branch""",
    """    parser->statement_expression_depth -= 1U;
    parser->current_block = parent_block;
    minic_parser_end_scope(parser);
    return success;
}

static bool parse_branch""",
)

# Breakable target cleanup contexts.
replace_once(
    path,
    """static bool parse_loop_branch(MinicParser *parser, MinicBlockId *block_id) {
    bool success;

    parser->loop_depth += 1U;
    success = parse_branch(parser, block_id);
    parser->loop_depth -= 1U;
    return success;
}
""",
    """static bool parse_loop_branch(MinicParser *parser, MinicBlockId *block_id) {
    MinicCleanupContextId previous_break_cleanup_context;
    bool success;

    previous_break_cleanup_context = parser->break_cleanup_context;
    parser->break_cleanup_context = parser->cleanup_context;
    parser->loop_depth += 1U;
    success = parse_branch(parser, block_id);
    parser->loop_depth -= 1U;
    parser->break_cleanup_context = previous_break_cleanup_context;
    return success;
}
""",
)
replace_once(
    path,
    """static bool parse_switch_branch(MinicParser *parser, MinicBlockId *block_id) {
    MinicParserSwitchContext *context;
    bool success;
""",
    """static bool parse_switch_branch(MinicParser *parser, MinicBlockId *block_id) {
    MinicParserSwitchContext *context;
    MinicCleanupContextId previous_break_cleanup_context;
    bool success;
""",
)
replace_once(
    path,
    """    context = &parser->switch_contexts[parser->switch_depth];
    (void)memset(context, 0, sizeof(*context));
    parser->switch_depth += 1U;
    success = parse_branch(parser, block_id);
    parser->switch_depth -= 1U;
""",
    """    context = &parser->switch_contexts[parser->switch_depth];
    (void)memset(context, 0, sizeof(*context));
    previous_break_cleanup_context = parser->break_cleanup_context;
    parser->break_cleanup_context = parser->cleanup_context;
    parser->switch_depth += 1U;
    success = parse_branch(parser, block_id);
    parser->switch_depth -= 1U;
    parser->break_cleanup_context = previous_break_cleanup_context;
""",
)

# Internal continue labels carry the loop's cleanup context.
replace_once(
    path,
    """    label.target_statement = MINIC_STATEMENT_INVALID;
    label.then_block = MINIC_BLOCK_INVALID;
""",
    """    label.target_statement = MINIC_STATEMENT_INVALID;
    label.cleanup_context = parser->cleanup_context;
    label.cleanup_stop_context = MINIC_CLEANUP_CONTEXT_ROOT;
    label.then_block = MINIC_BLOCK_INVALID;
""",
)
replace_once(
    path,
    """    statement.target_statement = parser->continue_target_statement;
    statement.then_block = MINIC_BLOCK_INVALID;
""",
    """    statement.target_statement = parser->continue_target_statement;
    statement.cleanup_context = parser->cleanup_context;
    statement.cleanup_stop_context = parser->continue_cleanup_context;
    statement.then_block = MINIC_BLOCK_INVALID;
""",
)

# Loop continue contexts.
replace_once(
    path,
    """    MinicStatementId previous_continue_target;
    MinicSourceSpan while_span;
""",
    """    MinicStatementId previous_continue_target;
    MinicCleanupContextId previous_continue_cleanup_context;
    MinicSourceSpan while_span;
""",
)
replace_once(
    path,
    """    previous_continue_target = parser->continue_target_statement;
    parser->continue_target_statement = continue_label;
    success = parse_loop_branch(parser, &statement.then_block);
    parser->continue_target_statement = previous_continue_target;
""",
    """    previous_continue_target = parser->continue_target_statement;
    previous_continue_cleanup_context = parser->continue_cleanup_context;
    parser->continue_target_statement = continue_label;
    parser->continue_cleanup_context = parser->cleanup_context;
    success = parse_loop_branch(parser, &statement.then_block);
    parser->continue_target_statement = previous_continue_target;
    parser->continue_cleanup_context = previous_continue_cleanup_context;
""",
)
# do-while has the same variable/assignment pattern once more.
replace_once(
    path,
    """    MinicStatementId previous_continue_target;
    MinicBlockId break_block;
""",
    """    MinicStatementId previous_continue_target;
    MinicCleanupContextId previous_continue_cleanup_context;
    MinicBlockId break_block;
""",
)
replace_once(
    path,
    """    previous_continue_target = parser->continue_target_statement;
    parser->continue_target_statement = continue_label;
    success = parse_loop_branch(parser, &statement.then_block);
    parser->continue_target_statement = previous_continue_target;
""",
    """    previous_continue_target = parser->continue_target_statement;
    previous_continue_cleanup_context = parser->continue_cleanup_context;
    parser->continue_target_statement = continue_label;
    parser->continue_cleanup_context = parser->cleanup_context;
    success = parse_loop_branch(parser, &statement.then_block);
    parser->continue_target_statement = previous_continue_target;
    parser->continue_cleanup_context = previous_continue_cleanup_context;
""",
)

# for-init scope owns its cleanup through the whole lowered while.
replace_once(
    path,
    """    MinicStatementId previous_continue_target;
    bool has_update;
    MinicSourceSpan for_span;
""",
    """    MinicStatementId previous_continue_target;
    MinicCleanupContextId previous_continue_cleanup_context;
    MinicCleanupContextId scope_cleanup_context;
    bool has_update;
    MinicSourceSpan for_span;
""",
)
replace_once(
    path,
    """    if (!minic_parser_begin_scope(parser)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
""",
    """    scope_cleanup_context = parser->cleanup_context;
    if (!minic_parser_begin_scope(parser)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
""",
)
replace_once(
    path,
    """    previous_continue_target = parser->continue_target_statement;
    parser->continue_target_statement = continue_label;
    success = parse_loop_branch(parser, &statement.then_block);
    parser->continue_target_statement = previous_continue_target;
""",
    """    previous_continue_target = parser->continue_target_statement;
    previous_continue_cleanup_context = parser->continue_cleanup_context;
    parser->continue_target_statement = continue_label;
    parser->continue_cleanup_context = parser->cleanup_context;
    success = parse_loop_branch(parser, &statement.then_block);
    parser->continue_target_statement = previous_continue_target;
    parser->continue_cleanup_context = previous_continue_cleanup_context;
""",
)
replace_once(
    path,
    """    success = minic_parser_add_statement(parser, &statement);
    minic_parser_end_scope(parser);
    return success;
}

static bool ensure_function_label_context""",
    """    success = minic_parser_add_statement(parser, &statement) &&
              minic_parser_materialize_cleanup_contexts(parser, scope_cleanup_context);
    minic_parser_end_scope(parser);
    return success;
}

static bool ensure_function_label_context""",
)

# GNU local labels remember the active cleanup lifetime.
replace_once(
    path,
    """        label.target_statement = MINIC_STATEMENT_INVALID;
        label.then_block = MINIC_BLOCK_INVALID;
""",
    """        label.target_statement = MINIC_STATEMENT_INVALID;
        label.cleanup_context = parser->cleanup_context;
        label.cleanup_stop_context = MINIC_CLEANUP_CONTEXT_ROOT;
        label.then_block = MINIC_BLOCK_INVALID;
""",
)

# Gotos carry source/target cleanup contexts. Forward gotos are fixed when the label appears.
replace_once(
    path,
    """    statement.target_statement = MINIC_STATEMENT_INVALID;
    statement.then_block = MINIC_BLOCK_INVALID;
""",
    """    statement.target_statement = MINIC_STATEMENT_INVALID;
    statement.cleanup_context = parser->cleanup_context;
    statement.cleanup_stop_context = MINIC_CLEANUP_CONTEXT_ROOT;
    statement.then_block = MINIC_BLOCK_INVALID;
""",
)
replace_once(
    path,
    """    statement.target_statement = minic_parser_find_label_statement(parser, name_span);
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after goto")) {
        return false;
    }
    return minic_parser_add_statement(parser, &statement);
}
""",
    """    statement.target_statement = minic_parser_find_label_statement(parser, name_span);
    if (statement.target_statement != MINIC_STATEMENT_INVALID) {
        const MinicStatement *target;

        target = minic_c0_program_statement(parser->program, statement.target_statement);
        if (target == NULL || target->kind != MINIC_STATEMENT_LABEL ||
            !minic_c0_cleanup_context_reaches(
                parser->program, statement.cleanup_context, target->cleanup_context)) {
            minic_parser_error(parser, "goto cannot enter a different GNU cleanup lifetime");
            return false;
        }
        statement.cleanup_stop_context = target->cleanup_context;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after goto")) {
        return false;
    }
    return minic_parser_add_statement(parser, &statement);
}
""",
)

# Label definitions record the active context and resolve pending goto cleanup targets.
replace_once(
    path,
    """        local_label = &parser->program->statements[label_statement_id];
        local_label->span = name_span;
""",
    """        local_label = &parser->program->statements[label_statement_id];
        local_label->span = name_span;
        local_label->cleanup_context = parser->cleanup_context;
""",
)
replace_once(
    path,
    """        statement.target_statement = MINIC_STATEMENT_INVALID;
        statement.then_block = MINIC_BLOCK_INVALID;
""",
    """        statement.target_statement = MINIC_STATEMENT_INVALID;
        statement.cleanup_context = parser->cleanup_context;
        statement.cleanup_stop_context = MINIC_CLEANUP_CONTEXT_ROOT;
        statement.then_block = MINIC_BLOCK_INVALID;
""",
)
replace_once(
    path,
    """            if (pending->kind == MINIC_STATEMENT_GOTO &&
                pending->target_statement == MINIC_STATEMENT_INVALID &&
                minic_parser_span_equals(parser, pending->span, name_span)) {
                pending->target_statement = label_statement_id;
            }
""",
    """            if (pending->kind == MINIC_STATEMENT_GOTO &&
                pending->target_statement == MINIC_STATEMENT_INVALID &&
                minic_parser_span_equals(parser, pending->span, name_span)) {
                if (!minic_c0_cleanup_context_reaches(
                        parser->program, pending->cleanup_context, statement.cleanup_context)) {
                    minic_parser_error(parser, "goto cannot enter a different GNU cleanup lifetime");
                    return false;
                }
                pending->target_statement = label_statement_id;
                pending->cleanup_stop_context = statement.cleanup_context;
            }
""",
)

# break / return exits.
replace_once(
    path,
    """    statement.expression = MINIC_EXPRESSION_INVALID;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;

    if (!minic_parser_advance(parser)) {
""",
    """    statement.expression = MINIC_EXPRESSION_INVALID;
    statement.cleanup_context = parser->cleanup_context;
    statement.cleanup_stop_context = parser->break_cleanup_context;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;

    if (!minic_parser_advance(parser)) {
""",
)
replace_once(
    path,
    """    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;
    if (!minic_parser_advance(parser)) {
""",
    """    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;
    statement.cleanup_context = parser->cleanup_context;
    statement.cleanup_stop_context = MINIC_CLEANUP_CONTEXT_ROOT;
    if (!minic_parser_advance(parser)) {
""",
)

# ---- Function-scope normal fallthrough ----------------------------------------------
path = "src/frontend/parser_function.c"
replace_once(
    path,
    """    if ((!minic_type_is_pointer(return_type) && !minic_type_is_double(return_type) &&
         !minic_type_is_record(return_type) && !minic_parser_add_default_return(parser)) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}'")) {
""",
    """    if (!minic_parser_materialize_cleanup_contexts(parser, MINIC_CLEANUP_CONTEXT_ROOT) ||
        ((!minic_type_is_pointer(return_type) && !minic_type_is_double(return_type) &&
          !minic_type_is_record(return_type) && !minic_parser_add_default_return(parser)) ||
         !minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}'"))) {
""",
)

# ---- RV64 cleanup emission -----------------------------------------------------------
path = "src/target/riscv64/codegen_statement.c"
replace_once(
    path,
    """static bool minic_riscv64_emit_return(FILE *file,
""",
    """static bool minic_riscv64_emit_cleanup_contexts(FILE *file,
                                               const MinicC0Program *program,
                                               const MinicFunction *function,
                                               MinicCleanupContextId current,
                                               MinicCleanupContextId stop) {
    if (!minic_c0_cleanup_context_reaches(program, current, stop)) {
        return false;
    }
    while (current != stop) {
        const MinicCleanupContext *context;

        context = minic_c0_program_cleanup_context(program, current);
        if (context == NULL ||
            !minic_riscv64_emit_expression(file, program, function, context->cleanup_expression)) {
            return false;
        }
        current = context->parent;
    }
    return true;
}

static bool minic_riscv64_emit_return(FILE *file,
""",
)
# Replace the full return helper to preserve return values across cleanup calls.
old_return_start = """static bool minic_riscv64_emit_return(FILE *file,
                                      const MinicC0Program *program,
                                      const MinicFunction *function,
                                      const MinicStatement *statement) {
"""
start = Path(path).read_text().index(old_return_start)
end_marker = "\nstatic bool minic_riscv64_collect_switch_labels"
source = Path(path).read_text()
end = source.index(end_marker, start)
old_return = source[start:end]
new_return = """static bool minic_riscv64_emit_return(FILE *file,
                                      const MinicC0Program *program,
                                      const MinicFunction *function,
                                      const MinicStatement *statement) {
    bool has_value;

    has_value = statement->expression != MINIC_EXPRESSION_INVALID;
    if (!has_value) {
        if (!minic_type_is_void(function->return_type)) {
            return false;
        }
    } else {
        const MinicExpression *value;

        value = minic_c0_program_expression(program, statement->expression);
        if (minic_type_is_void(function->return_type) || value == NULL ||
            !minic_c0_assignment_compatible(
                program, function->return_type, statement->expression)) {
            return false;
        }
        if (minic_type_is_record(function->return_type)) {
            size_t aggregate_size;
            size_t aggregate_chunks;

            if (!minic_type_is_record(value->type) ||
                value->type.record_id != function->return_type.record_id ||
                !minic_riscv64_integer_aggregate_abi(
                    program, function->return_type, &aggregate_size, &aggregate_chunks)) {
                return false;
            }
            (void)aggregate_size;
            if (value->value_category == MINIC_VALUE_LVALUE) {
                if (!minic_riscv64_emit_lvalue_address(
                        file, program, function, statement->expression) ||
                    fprintf(file, "  mv t0, a0\\n  ld a0, 0(t0)\\n") < 0 ||
                    (aggregate_chunks == 2U && fprintf(file, "  ld a1, 8(t0)\\n") < 0)) {
                    return false;
                }
            } else if (value->kind != MINIC_EXPRESSION_CALL ||
                       !minic_riscv64_emit_expression(
                           file, program, function, statement->expression)) {
                return false;
            }
        } else if (!minic_riscv64_emit_expression(file, program, function, statement->expression)) {
            return false;
        }
        if (minic_type_is_integer(function->return_type) &&
            !minic_riscv64_emit_integer_conversion(file, function->return_type, "a0")) {
            return false;
        }
    }

    if (statement->cleanup_context != statement->cleanup_stop_context) {
        if (has_value && fprintf(file, "  addi sp, sp, -16\\n  sd a0, 0(sp)\\n  sd a1, 8(sp)\\n") < 0) {
            return false;
        }
        if (!minic_riscv64_emit_cleanup_contexts(file,
                                                 program,
                                                 function,
                                                 statement->cleanup_context,
                                                 statement->cleanup_stop_context)) {
            return false;
        }
        if (has_value && fprintf(file, "  ld a0, 0(sp)\\n  ld a1, 8(sp)\\n  addi sp, sp, 16\\n") < 0) {
            return false;
        }
    }
    if (has_value && minic_type_is_double(function->return_type) &&
        fprintf(file, "  fmv.d.x fa0, a0\\n") < 0) {
        return false;
    }
    return fprintf(file, "  j .L%s_return\\n", function->name) >= 0;
}
"""
Path(path).write_text(source[:start] + new_return + source[end:])
replace_once(
    path,
    """    case MINIC_STATEMENT_BREAK:
        return minic_riscv64_emit_break(file, break_target);

    case MINIC_STATEMENT_GOTO:
        return statement->target_statement != MINIC_STATEMENT_INVALID &&
               fprintf(file, "  j .Luser_%zu\\n", (size_t)statement->target_statement) >= 0;
""",
    """    case MINIC_STATEMENT_BREAK:
        return minic_riscv64_emit_cleanup_contexts(file,
                                                   program,
                                                   function,
                                                   statement->cleanup_context,
                                                   statement->cleanup_stop_context) &&
               minic_riscv64_emit_break(file, break_target);

    case MINIC_STATEMENT_GOTO:
        return statement->target_statement != MINIC_STATEMENT_INVALID &&
               minic_riscv64_emit_cleanup_contexts(file,
                                                   program,
                                                   function,
                                                   statement->cleanup_context,
                                                   statement->cleanup_stop_context) &&
               fprintf(file, "  j .Luser_%zu\\n", (size_t)statement->target_statement) >= 0;
""",
)

# ---- Runtime regression --------------------------------------------------------------
path = "tests/compiler/c0/run-runtime.sh"
replace_once(
    path,
    "run_case array_declaration 0 array_declaration\nrun_double_return_abi\n",
    "run_case array_declaration 0 array_declaration\n"
    "run_case gnu_cleanup_runtime 0 gnu_cleanup_runtime\n"
    "run_double_return_abi\n",
)
