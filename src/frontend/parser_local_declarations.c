#include "frontend/parser_local_declarations.h"
#include "frontend/parser_runtime_initializer.h"
#include "frontend/declaration_sema.h"
#include "frontend/initializer.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Cohesive block-scope declarations: VLA bounds, local attributes,
 * auto_type, extern, static and automatic object construction. */

typedef struct MinicLocalObjectAttributes {
    MinicFunctionId cleanup_function;
    MinicSourceSpan cleanup_attribute_span;
    size_t explicit_alignment;
    bool function_pointer_noreturn;
} MinicLocalObjectAttributes;

static bool parse_cleanup_attribute_function(MinicParser *parser,
                                             const MinicParsedAttribute *attribute,
                                             MinicFunctionId *function_id) {
    MinicParser probe;
    MinicSourceSpan name_span;

    if (parser == NULL || attribute == NULL || function_id == NULL || !attribute->has_arguments ||
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
    if (parser == NULL || attribute == NULL) {
        return false;
    }
    descriptor = attribute->descriptor;
    if (descriptor == NULL) {
        minic_parser_error(parser, "unsupported GNU attribute on local object");
        return false;
    }
    /* `void __attribute__((noreturn)) (*hibernate_exit)(...)` declares a
     * local pointer to a non-returning function, not an annotated object.
     * Defer the function-type check until the parenthesized declarator has
     * been built. MiniC does not infer unreachable code from noreturn calls,
     * so preserving ordinary indirect-call flow is conservative. */
    if (descriptor->kind == MINIC_ATTRIBUTE_NORETURN && !attribute->has_arguments) {
        if (attributes == NULL) {
            return false;
        }
        attributes->function_pointer_noreturn = true;
        return true;
    }
    if (!minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_OBJECT)) {
        minic_parser_error(parser, "GNU attribute is not valid on a local object");
        return false;
    }
    /* GNU may_alias on a local declarator prevents type-based alias
     * assumptions for accesses through that pointer. Current MiniC Core
     * has no type-based alias analysis or TBAA transforms, so ignoring
     * precisely this no-argument optimization attribute is conservative.
     * Any future TBAA pass must first preserve this source annotation. */
    if (descriptor->kind == MINIC_ATTRIBUTE_MAY_ALIAS) {
        if (attribute->has_arguments) {
            minic_parser_error(parser, "GNU may_alias local attribute takes no arguments");
            return false;
        }
        return true;
    }
    /* Linux cmd-db's `u8 query[N] __nonstring` indicates that an array may
     * lack a trailing NUL. This annotation controls GCC string warnings;
     * MiniC does not issue such diagnostics or infer string bounds from the
     * attribute, so it is safely observational on a local object. */
    if (descriptor->kind == MINIC_ATTRIBUTE_NONSTRING) {
        if (attribute->has_arguments) {
            minic_parser_error(parser, "GNU nonstring local attribute takes no arguments");
            return false;
        }
        return true;
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_ALIGNED) {
        if (attributes == NULL) {
            minic_parser_error(parser, "invalid GNU aligned local object context");
            return false;
        }
        return minic_parser_apply_alignment_attribute(
            parser, attribute, "local object", &attributes->explicit_alignment);
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_CLEANUP) {
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
    return minic_parser_parse_gnu_attribute_lists(
        parser, consume_local_object_attribute, attributes);
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
        !minic_type_assignment_compatible(function.parameter_types[0], pointer_type)) {
        minic_parser_error(parser, "GNU cleanup function must accept the local object's address");
        return false;
    }
    if (!minic_parser_statement_local_lvalue(parser, local_id, local->name_span, &local_expression_id)) {
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
    if (!minic_c0_call_set_argument(&call, 0U, address_id)) {
        minic_parser_error(parser, "cannot represent GNU cleanup call argument");
        return false;
    }
    if (!minic_parser_add_expression(parser, &call, &call_id) ||
        !minic_c0_program_add_cleanup_context(
            parser->program, parser->cleanup_context, call_id, &cleanup_context_id)) {
        minic_parser_error(parser, "cannot record GNU cleanup lifetime");
        return false;
    }
    parser->cleanup_context = cleanup_context_id;
    return true;
}

static bool local_identifier_is(const MinicParser *parser, const char *name) {
    size_t name_length;

    if (parser == NULL || name == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        return false;
    }
    name_length = strlen(name);
    return minic_parser_span_length(parser->current.span) == name_length &&
           memcmp(parser->source + parser->current.span.begin.offset, name, name_length) == 0;
}

static bool parse_local_fixed_register_name(
    MinicParser *parser, char *buffer, size_t capacity, size_t *length, bool *has_binding) {
    if (parser == NULL || buffer == NULL || capacity == 0U || length == NULL ||
        has_binding == NULL) {
        return false;
    }
    *length = 0U;
    *has_binding = false;
    if (!local_identifier_is(parser, "asm") && !local_identifier_is(parser, "__asm") &&
        !local_identifier_is(parser, "__asm__")) {
        return true;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after local asm") ||
        parser->current.kind != MINIC_TOKEN_STRING_LITERAL) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "local fixed register requires one string literal");
        }
        return false;
    }
    if (!minic_parser_parse_gnu_asm_name(parser, buffer, capacity, length))
        return false;
    *has_binding = true;
    return minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after local asm name");
}

static bool local_declarator_starts_function_pointer(const MinicParser *parser) {
    MinicParser probe;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_LPAREN) {
        return false;
    }
    probe = *parser;
    return minic_parser_advance(&probe) && probe.current.kind == MINIC_TOKEN_STAR;
}

static bool local_declarator_starts_pointer_to_array(const MinicParser *parser) {
    MinicParser probe;
    size_t parenthesis_depth;

    if (!local_declarator_starts_function_pointer(parser)) {
        return false;
    }
    probe = *parser;
    parenthesis_depth = 0U;
    for (;;) {
        if (probe.current.kind == MINIC_TOKEN_LPAREN) {
            parenthesis_depth += 1U;
        } else if (probe.current.kind == MINIC_TOKEN_RPAREN) {
            if (parenthesis_depth == 0U) {
                return false;
            }
            parenthesis_depth -= 1U;
            if (parenthesis_depth == 0U) {
                return minic_parser_advance(&probe) && probe.current.kind == MINIC_TOKEN_LBRACKET;
            }
        } else if (probe.current.kind == MINIC_TOKEN_EOF) {
            return false;
        }
        if (!minic_parser_advance(&probe)) {
            return false;
        }
    }
}

static bool parse_local_array_bound_or_vla(MinicParser *parser,
                                           size_t *element_count,
                                           MinicExpressionId *runtime_bound_id) {
    MinicExpressionId bound_id;
    const MinicExpression *bound;
    MinicConstValue constant;
    int64_t value;

    if (parser == NULL || element_count == NULL || runtime_bound_id == NULL ||
        !minic_parser_parse_expression(parser, &bound_id, 0U)) {
        return false;
    }
    bound = minic_c0_program_expression(parser->program, bound_id);
    if (bound == NULL || !minic_type_is_integer(bound->type)) {
        minic_parser_error(parser, "array bound must have integer type");
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_RBRACKET) {
        minic_parser_error(parser, "expected ']'");
        return false;
    }

    if (minic_const_eval_integer(parser->program, parser->target_info, bound_id, &constant)) {
        if (!minic_const_value_as_int64(
                parser->program, parser->target_info, &constant, &value)) {
            minic_parser_error(parser, "array bound exceeds target object range");
            return false;
        }
        if (value < 0) {
            minic_parser_error(parser, "array bound must not be negative");
            return false;
        }
        if ((uint64_t)value > (uint64_t)SIZE_MAX) {
            minic_parser_error(parser, "array bound exceeds target object range");
            return false;
        }
        *element_count = (size_t)value;
        *runtime_bound_id = MINIC_EXPRESSION_INVALID;
    } else {
        /* A non-ICE local bound is a VLA. Preserve the parsed expression so
           declaration materialization can evaluate it exactly once. */
        *element_count = 0U;
        *runtime_bound_id = bound_id;
    }
    return minic_parser_advance(parser);
}

static bool materialize_dynamic_local_array(MinicParser *parser,
                                            MinicLocalId local_id,
                                            MinicExpressionId runtime_bound_id) {
    const MinicLocal *local;
    const MinicExpression *bound;
    MinicLocal local_snapshot;
    MinicExpression bound_snapshot;
    MinicLocal count_local;
    MinicLocal address_local;
    MinicLocalId count_local_id;
    MinicLocalId address_local_id;
    MinicExpression expression;
    MinicExpressionId count_value_id;
    MinicExpressionId count_target_id;
    MinicExpressionId count_read_id;
    MinicExpressionId size_constant_id;
    MinicExpressionId byte_count_id;
    MinicExpressionId alloca_id;
    MinicExpressionId pointer_value_id;
    MinicExpressionId address_target_id;
    MinicStatement statement;
    MinicType pointer_type;
    MinicType void_pointer_type;
    size_t element_size;

    if (parser == NULL || runtime_bound_id == MINIC_EXPRESSION_INVALID) {
        return false;
    }
    local = minic_c0_program_local(parser->program, local_id);
    bound = minic_c0_program_expression(parser->program, runtime_bound_id);
    if (local == NULL || bound == NULL) {
        return false;
    }
    local_snapshot = *local;
    bound_snapshot = *bound;
    if (!local_snapshot.is_array || local_snapshot.element_count != 0U ||
        local_snapshot.dynamic_count_local_id != MINIC_LOCAL_INVALID ||
        local_snapshot.dynamic_address_local_id != MINIC_LOCAL_INVALID ||
        local_snapshot.is_register_storage || local_snapshot.explicit_alignment != 0U ||
        !minic_type_is_integer(bound_snapshot.type) ||
        !minic_target_info_sizeof_type(
            parser->target_info, parser->program, local_snapshot.type, &element_size) ||
        element_size == 0U ||
        !minic_type_pointer_to(local_snapshot.type, &pointer_type) ||
        !minic_type_pointer_to(minic_type_void(), &void_pointer_type)) {
        minic_parser_error(parser, "unsupported variable length array declaration");
        return false;
    }

    minic_c0_local_initialize(&count_local);
    count_local.name_span = local_snapshot.name_span;
    count_local.type = minic_type_unsigned_long();
    count_local.element_count = 1U;
    count_local.is_array = false;
    count_local.is_register_storage = false;
    if (!minic_c0_program_add_local(parser->program, &count_local, &count_local_id)) {
        minic_parser_error(parser, "cannot allocate VLA bound storage");
        return false;
    }

    minic_c0_local_initialize(&address_local);
    address_local.name_span = local_snapshot.name_span;
    address_local.type = pointer_type;
    address_local.element_count = 1U;
    address_local.is_array = false;
    address_local.is_register_storage = false;
    if (!minic_c0_program_add_local(parser->program, &address_local, &address_local_id)) {
        minic_parser_error(parser, "cannot allocate VLA address storage");
        return false;
    }
    parser->program->locals[local_id].dynamic_count_local_id = count_local_id;
    parser->program->locals[local_id].dynamic_address_local_id = address_local_id;

    count_value_id = runtime_bound_id;
    if (!minic_type_equal(bound_snapshot.type, minic_type_unsigned_long())) {
        (void)memset(&expression, 0, sizeof(expression));
        expression.kind = MINIC_EXPRESSION_CAST;
        expression.span = bound_snapshot.span;
        expression.type = minic_type_unsigned_long();
        expression.value_category = MINIC_VALUE_RVALUE;
        expression.value.unary.operand = runtime_bound_id;
        if (!minic_parser_add_expression(parser, &expression, &count_value_id)) {
            return false;
        }
    }

    if (!minic_parser_statement_local_lvalue(
            parser, count_local_id, local_snapshot.name_span, &count_target_id)) {
        return false;
    }
    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_ASSIGN;
    statement.span = bound_snapshot.span;
    statement.target_expression = count_target_id;
    statement.expression = count_value_id;
    statement.target_statement = MINIC_STATEMENT_INVALID;
    statement.cleanup_context = parser->cleanup_context;
    statement.cleanup_stop_context = MINIC_CLEANUP_CONTEXT_ROOT;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;
    if (!minic_parser_add_statement(parser, &statement)) {
        return false;
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_LVALUE_READ;
    expression.span = bound_snapshot.span;
    expression.type = minic_type_unsigned_long();
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.unary.operand = count_target_id;
    if (!minic_parser_add_expression(parser, &expression, &count_read_id)) {
        return false;
    }
    byte_count_id = count_read_id;

    if (element_size != 1U) {
        (void)memset(&expression, 0, sizeof(expression));
        expression.kind = MINIC_EXPRESSION_INTEGER;
        expression.span = bound_snapshot.span;
        expression.type = minic_type_unsigned_long();
        expression.value_category = MINIC_VALUE_RVALUE;
        expression.value.integer_value = (int64_t)element_size;
        if (!minic_parser_add_expression(parser, &expression, &size_constant_id)) {
            return false;
        }

        (void)memset(&expression, 0, sizeof(expression));
        expression.kind = MINIC_EXPRESSION_BINARY;
        expression.span = bound_snapshot.span;
        expression.type = minic_type_unsigned_long();
        expression.value_category = MINIC_VALUE_RVALUE;
        expression.value.binary.operator_kind = MINIC_BINARY_MULTIPLY;
        expression.value.binary.left = count_read_id;
        expression.value.binary.right = size_constant_id;
        if (!minic_parser_add_expression(parser, &expression, &byte_count_id)) {
            return false;
        }
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_BUILTIN_ALLOCA;
    expression.span = bound_snapshot.span;
    expression.type = void_pointer_type;
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.unary.operand = byte_count_id;
    if (!minic_parser_add_expression(parser, &expression, &alloca_id)) {
        return false;
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_CAST;
    expression.span = bound_snapshot.span;
    expression.type = pointer_type;
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.unary.operand = alloca_id;
    if (!minic_parser_add_expression(parser, &expression, &pointer_value_id) ||
        !minic_parser_statement_local_lvalue(
            parser, address_local_id, local_snapshot.name_span, &address_target_id)) {
        return false;
    }

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_ASSIGN;
    statement.span = bound_snapshot.span;
    statement.target_expression = address_target_id;
    statement.expression = pointer_value_id;
    statement.target_statement = MINIC_STATEMENT_INVALID;
    statement.cleanup_context = parser->cleanup_context;
    statement.cleanup_stop_context = MINIC_CLEANUP_CONTEXT_ROOT;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;
    return minic_parser_add_statement(parser, &statement);
}

static bool
parse_local_declarator(MinicParser *parser,
                       MinicType base_type,
                       bool is_register_storage,
                       const MinicLocalObjectAttributes *declaration_attributes) {
    MinicLocal local;
    minic_c0_local_initialize(&local);
    MinicLocalId local_id;
    MinicLocalObjectAttributes attributes;
    MinicType declared_type;
    char fixed_register_name[32];
    size_t fixed_register_name_length;
    bool has_fixed_register_binding;
    bool parenthesized_name_is_array;
    bool parenthesized_name_array_inferred;
    size_t parenthesized_name_array_count;
    MinicExpressionId runtime_array_bound;

    if (declaration_attributes != NULL) {
        attributes = *declaration_attributes;
    } else {
        (void)memset(&attributes, 0, sizeof(attributes));
        attributes.cleanup_function = MINIC_FUNCTION_INVALID;
    }
    (void)memset(fixed_register_name, 0, sizeof(fixed_register_name));
    fixed_register_name_length = 0U;
    has_fixed_register_binding = false;
    parenthesized_name_is_array = false;
    parenthesized_name_array_inferred = false;
    parenthesized_name_array_count = 0U;
    runtime_array_bound = MINIC_EXPRESSION_INVALID;
    if (!minic_parser_parse_pointer_declarator(parser, base_type, &declared_type) ||
        !parse_local_object_attributes(parser, &attributes)) {
        return false;
    }
    if (local_declarator_starts_pointer_to_array(parser)) {
        if (!minic_parser_parse_parenthesized_pointer_to_array_declarator(
                parser,
                declared_type,
                &local.name_span,
                &declared_type,
                &parenthesized_name_is_array,
                &parenthesized_name_array_inferred,
                &parenthesized_name_array_count)) {
            return false;
        }
    } else if (local_declarator_starts_function_pointer(parser)) {
        MinicParsedFunctionDeclarator declarator;

        if (!minic_parser_parse_parenthesized_function_declarator(
                parser, true, true, &declarator)) {
            return false;
        }
        if (declarator.is_variadic) {
            minic_parser_error(parser,
                               "variadic direct local function pointers are not supported yet");
            return false;
        }
        if (!minic_parser_build_function_declarator_type(
                parser, declared_type, &declarator, &declared_type)) {
            minic_parser_error(parser, "cannot build local function pointer type");
            return false;
        }
        local.name_span = declarator.name_span;
    } else if (!minic_parser_parse_direct_declarator_name(parser, &local.name_span)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "expected local name");
        }
        return false;
    }
    if (attributes.function_pointer_noreturn &&
        (!minic_type_is_pointer(declared_type) ||
         declared_type.base_kind != MINIC_TYPE_BASE_FUNCTION)) {
        minic_parser_error(parser, "GNU noreturn on a local requires a function pointer");
        return false;
    }
    if (minic_type_is_void(declared_type)) {
        minic_parser_error(parser, "local object cannot have void type");
        return false;
    }
    /* M175C_LOCAL_COMPLETE_OBJECT_OWNER: a local declaration owns storage, so
       record/enum object types must be complete here. Pointer declarators remain
       valid because the shared complete-object query accepts pointer types. */
    if (!minic_parser_require_complete_object_type(
            parser, declared_type, "local object type is incomplete")) {
        return false;
    }
    if (!parse_local_fixed_register_name(parser,
                                         fixed_register_name,
                                         sizeof(fixed_register_name),
                                         &fixed_register_name_length,
                                         &has_fixed_register_binding)) {
        return false;
    }
    if (has_fixed_register_binding && !is_register_storage) {
        minic_parser_error(parser, "local fixed register binding requires register storage class");
        return false;
    }
    if (has_fixed_register_binding &&
        (!minic_type_is_integer(declared_type) && !minic_type_is_pointer(declared_type))) {
        minic_parser_error(parser,
                           "local fixed register binding requires scalar integer or pointer type");
        return false;
    }
    if (has_fixed_register_binding &&
        !minic_target_info_local_fixed_register_supported(
            parser->target_info, fixed_register_name, fixed_register_name_length)) {
        minic_parser_error(parser, "local fixed register binding is not supported by this target");
        return false;
    }
    if (has_fixed_register_binding &&
        (parenthesized_name_is_array || parser->current.kind == MINIC_TOKEN_LBRACKET)) {
        minic_parser_error(parser, "local fixed register arrays are not supported");
        return false;
    }

    local.type = declared_type;
    local.element_count =
        parenthesized_name_is_array ? parenthesized_name_array_count : 1U;
    local.explicit_alignment = attributes.explicit_alignment;
    local.is_array = parenthesized_name_is_array;
    local.is_register_storage = is_register_storage;
    if (minic_parser_name_bound_in_current_scope(parser, local.name_span)) {
        minic_parser_error(parser, "duplicate local declaration");
        return false;
    }
    if (parenthesized_name_is_array || parser->current.kind == MINIC_TOKEN_LBRACKET) {
        bool inferred_array;

        inferred_array = parenthesized_name_is_array && parenthesized_name_array_inferred;
        if (!parenthesized_name_is_array) {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACKET) {
                inferred_array = true;
                local.element_count = 0U;
                if (!minic_parser_advance(parser)) {
                    return false;
                }
            } else if (!parse_local_array_bound_or_vla(
                           parser, &local.element_count, &runtime_array_bound)) {
                return false;
            }
        }
        if (!parenthesized_name_is_array && parser->current.kind == MINIC_TOKEN_LBRACKET) {
            MinicType nested_element_type;
            bool has_nested_array;

            if (!minic_parser_parse_array_declarator_suffix(
                    parser, declared_type, false, &nested_element_type, &has_nested_array) ||
                !has_nested_array) {
                return false;
            }
            declared_type = nested_element_type;
            local.type = declared_type;
        }
        local.is_array = true;
        local.is_zero_length_array = !inferred_array &&
                                     local.element_count == 0U &&
                                     runtime_array_bound == MINIC_EXPRESSION_INVALID;
        if (!parse_local_object_attributes(parser, &attributes)) {
            return false;
        }
        local.explicit_alignment = attributes.explicit_alignment;
        if (!minic_c0_program_add_local(parser->program, &local, &local_id)) {
            minic_parser_error(parser, "out of memory while adding local");
            return false;
        }
        if (has_fixed_register_binding) {
            MinicFixedRegisterBindingId binding_id;

            if (!minic_c0_program_add_local_fixed_register_binding(
                    parser->program,
                    local_id,
                    parser->source + local.name_span.begin.offset,
                    minic_parser_span_length(local.name_span),
                    fixed_register_name,
                    fixed_register_name_length,
                    &binding_id)) {
                minic_parser_error(parser, "cannot record local fixed register binding");
                return false;
            }
            (void)binding_id;
        }
        if (!minic_parser_bind_local(parser, local.name_span, local_id)) {
            return false;
        }
        if (runtime_array_bound != MINIC_EXPRESSION_INVALID) {
            if (inferred_array || parenthesized_name_is_array ||
                parser->current.kind == MINIC_TOKEN_LBRACKET ||
                !materialize_dynamic_local_array(parser, local_id, runtime_array_bound)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser, "unsupported variable length array declarator");
                }
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_EQUAL) {
                minic_parser_error(parser, "variable length array cannot be initialized");
                return false;
            }
            return finalize_local_cleanup(parser, &attributes, &local, local_id);
        }
        if (inferred_array && parser->current.kind != MINIC_TOKEN_EQUAL) {
            minic_parser_error(parser, "inferred local array requires an initializer");
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_EQUAL) {
            if (!minic_parser_runtime_local_array(parser, local_id, inferred_array)) {
                return false;
            }
            local.element_count = parser->program->locals[local_id].element_count;
            return finalize_local_cleanup(parser, &attributes, &local, local_id);
        }
    }
    if (!local.is_array) {
        if (!parse_local_object_attributes(parser, &attributes)) {
            return false;
        }
        local.explicit_alignment = attributes.explicit_alignment;
        if (!minic_c0_program_add_local(parser->program, &local, &local_id)) {
            minic_parser_error(parser, "out of memory while adding local");
            return false;
        }
        if (has_fixed_register_binding) {
            MinicFixedRegisterBindingId binding_id;

            if (!minic_c0_program_add_local_fixed_register_binding(
                    parser->program,
                    local_id,
                    parser->source + local.name_span.begin.offset,
                    minic_parser_span_length(local.name_span),
                    fixed_register_name,
                    fixed_register_name_length,
                    &binding_id)) {
                minic_parser_error(parser, "cannot record local fixed register binding");
                return false;
            }
            (void)binding_id;
        }
        if (!minic_parser_bind_local(parser, local.name_span, local_id)) {
            return false;
        }
    }

    if (parser->current.kind == MINIC_TOKEN_EQUAL) {
        MinicStatement statement;
        const MinicExpression *initializer;

        if (local.is_array) {
            minic_parser_error(parser,
                               "internal error: local array initializer escaped array path");
            return false;
        }
        if (minic_type_is_record(local.type)) {
            MinicExpressionId target_id;

            if (!minic_parser_statement_local_lvalue(parser, local_id, local.name_span, &target_id) ||
                !minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_LBRACE) {
                if (!minic_parser_parse_runtime_record_initializer(parser, target_id)) {
                    return false;
                }
                return finalize_local_cleanup(parser, &attributes, &local, local_id);
            } else {
                MinicExpressionId source_id;
                const MinicExpression *source;

                if (!minic_parser_parse_expression(parser, &source_id, 0U)) {
                    return false;
                }
                source = minic_c0_program_expression(parser->program, source_id);
                if (source == NULL ||
                    !minic_c0_record_value_is_copy_source(parser->program, source_id) ||
                    !minic_type_is_record(source->type) ||
                    source->type.record_id != local.type.record_id) {
                    minic_parser_error(
                        parser, "record local initializer requires a matching record copy source");
                    return false;
                }
                if (!minic_parser_statement_record_initialize(parser, target_id, source_id, source->span)) {
                    return false;
                }
                return finalize_local_cleanup(parser, &attributes, &local, local_id);
            }
        }
        (void)memset(&statement, 0, sizeof(statement));
        statement.kind = MINIC_STATEMENT_ASSIGN;
        statement.span.begin = local.name_span.begin;
        if (!minic_parser_statement_local_lvalue(
                parser, local_id, local.name_span, &statement.target_expression) ||
            !minic_parser_advance(parser) ||
            !minic_parser_parse_expression(parser, &statement.expression, 0U) ||
            !minic_parser_statement_assignment_conversion(parser, local.type, &statement.expression)) {
            return false;
        }
        initializer = minic_c0_program_expression(parser->program, statement.expression);
        if (initializer == NULL ||
            !minic_c0_assignment_compatible(parser->program, local.type, statement.expression)) {
            minic_parser_error(parser, "initializer type does not match local type");
            return false;
        }
        statement.span.end = initializer->span.end;
        if (!minic_parser_add_statement(parser, &statement)) {
            return false;
        }
    }
    return finalize_local_cleanup(parser, &attributes, &local, local_id);
}

bool minic_parser_local_decl_starts_auto_type(const MinicParser *parser) {
    return parser != NULL && parser->current.kind == MINIC_TOKEN_IDENTIFIER &&
           minic_parser_span_length(parser->current.span) == 11U &&
           memcmp(parser->source + parser->current.span.begin.offset, "__auto_type", 11U) == 0;
}

bool minic_parser_local_decl_auto_type(MinicParser *parser) {
    const MinicExpression *initializer;
    MinicExpressionId initializer_id;
    MinicExpressionId target_id;
    MinicLocal local;
    MinicLocalId local_id;
    MinicSourcePosition begin;
    MinicSourceSpan initializer_span;
    MinicType initializer_type;

    if (!minic_parser_local_decl_starts_auto_type(parser)) {
        return false;
    }
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "GNU __auto_type declarator must be an identifier");
        }
        return false;
    }

    minic_c0_local_initialize(&local);
    local.name_span = parser->current.span;
    local.element_count = 1U;
    local.is_array = false;
    local.is_register_storage = false;
    if (minic_parser_name_bound_in_current_scope(parser, local.name_span)) {
        minic_parser_error(parser, "duplicate local declaration");
        return false;
    }
    if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_EQUAL) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "GNU __auto_type declaration requires an initializer");
        }
        return false;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_parse_expression(parser, &initializer_id, 0U)) {
        return false;
    }
    initializer = minic_c0_program_expression(parser->program, initializer_id);
    if (initializer == NULL || minic_type_is_void(initializer->type) ||
        !minic_parser_require_complete_object_type(
            parser,
            initializer->type,
            "GNU __auto_type initializer must determine a complete object type")) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "invalid GNU __auto_type initializer type");
        }
        return false;
    }
    initializer_span = initializer->span;
    initializer_type = initializer->type;
    local.type = initializer_type;

    /* GNU __auto_type deliberately keeps the new name out of scope while the
       initializer is parsed.  Bind it only after the initializer type is known. */
    if (!minic_c0_program_add_local(parser->program, &local, &local_id) ||
        !minic_parser_bind_local(parser, local.name_span, local_id) ||
        !minic_parser_statement_local_lvalue(parser, local_id, local.name_span, &target_id)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot materialize GNU __auto_type local");
        }
        return false;
    }

    if (minic_type_is_record(local.type)) {
        if (!minic_c0_record_value_is_copy_source(parser->program, initializer_id) ||
            !minic_parser_statement_record_initialize(parser, target_id, initializer_id, initializer_span)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(
                    parser,
                    "GNU __auto_type record initializer requires a supported record copy source");
            }
            return false;
        }
    } else {
        MinicStatement statement;

        (void)memset(&statement, 0, sizeof(statement));
        statement.kind = MINIC_STATEMENT_ASSIGN;
        statement.span.begin = begin;
        statement.span.end = initializer_span.end;
        statement.target_expression = target_id;
        statement.expression = initializer_id;
        statement.target_statement = MINIC_STATEMENT_INVALID;
        statement.then_block = MINIC_BLOCK_INVALID;
        statement.else_block = MINIC_BLOCK_INVALID;
        if (!minic_c0_assignment_compatible(parser->program, local.type, initializer_id) ||
            !minic_parser_add_statement(parser, &statement)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot initialize GNU __auto_type local");
            }
            return false;
        }
    }

    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after GNU __auto_type declaration");
}

static bool parse_block_scope_function_declaration_after_head(
    MinicParser *parser, MinicType return_type, MinicSourceSpan name_span) {
    MinicType parameter_types[MINIC_MAX_FUNCTION_PARAMETERS];
    MinicFunctionId function_id;
    const MinicFunction *existing_function;
    size_t parameter_count;
    bool is_variadic;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_LPAREN) {
        return false;
    }
    parameter_count = 0U;
    is_variadic = false;
    (void)memset(parameter_types, 0, sizeof(parameter_types));

    if (!minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '('") ||
        !minic_parser_parse_parameter_list(
            parser, NULL, parameter_types, &parameter_count, false, &is_variadic) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')'") ||
        !minic_parser_parse_gnu_function_attributes(parser) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_SEMICOLON, "expected ';' after block-scope function declaration")) {
        return false;
    }

    function_id = minic_parser_find_function(parser, name_span);
    if (function_id != MINIC_FUNCTION_INVALID) {
        existing_function = minic_c0_program_function(parser->program, function_id);
        if (!minic_parser_function_signature_matches(
                parser->program, existing_function, return_type,
                parameter_types, parameter_count, is_variadic)) {
            minic_parser_error(parser, "conflicting block-scope function declaration");
            return false;
        }
        return true;
    }
    if (!minic_c0_program_add_function(parser->program,
                                       parser->source + name_span.begin.offset,
                                       minic_parser_span_length(name_span),
                                       parser->program->local_count,
                                       0U,
                                       MINIC_BLOCK_INVALID,
                                       &function_id) ||
        !minic_c0_program_set_function_signature(
            parser->program, function_id, return_type, parameter_types, parameter_count) ||
        !minic_c0_program_set_function_internal(parser->program, function_id, false) ||
        !minic_c0_program_set_function_variadic(parser->program, function_id, is_variadic)) {
        minic_parser_error(parser, "out of memory while declaring block-scope function");
        return false;
    }
    return true;
}

bool minic_parser_local_decl_ordinary(MinicParser *parser) {
    MinicLocalObjectAttributes declaration_attributes;
    MinicType base_type;
    bool is_register_storage;

    (void)memset(&declaration_attributes, 0, sizeof(declaration_attributes));
    declaration_attributes.cleanup_function = MINIC_FUNCTION_INVALID;
    if (!parse_local_object_attributes(parser, &declaration_attributes) ||
        !minic_parser_parse_local_storage_class(parser, &is_register_storage) ||
        !minic_parser_parse_type_specifiers(parser, &base_type) ||
        !parse_local_object_attributes(parser, &declaration_attributes)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_SEMICOLON &&
        (minic_type_is_record(base_type) || minic_type_is_enum(base_type))) {
        return minic_parser_advance(parser);
    }

    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        MinicParser probe;
        MinicSourceSpan function_name_span;

        probe = *parser;
        function_name_span = parser->current.span;
        if (!minic_parser_advance(&probe)) {
            return false;
        }
        if (probe.current.kind == MINIC_TOKEN_LPAREN) {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            return parse_block_scope_function_declaration_after_head(
                parser, base_type, function_name_span);
        }
    }

    for (;;) {
        if (!parse_local_declarator(
                parser, base_type, is_register_storage, &declaration_attributes)) {
            return false;
        }
        if (parser->current.kind != MINIC_TOKEN_COMMA) {
            break;
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    return minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';'");
}

static bool consume_block_scope_extern_head_attribute(MinicParser *parser,
                                                        const MinicParsedAttribute *attribute,
                                                        void *opaque_is_weak) {
    bool *is_weak = (bool *)opaque_is_weak;
    const MinicAttributeDescriptor *descriptor;

    if (parser == NULL || attribute == NULL || is_weak == NULL ||
        (descriptor = attribute->descriptor) == NULL) {
        return false;
    }
    /* Before the declarator name the entity may be a function or an object.
     * GNU weak is legal for both when the declaration has extern linkage;
     * persist it only after the declarator has resolved the entity kind. */
    if (descriptor->kind == MINIC_ATTRIBUTE_WEAK && !attribute->has_arguments) {
        *is_weak = true;
        return true;
    }
    if (descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_INFORMATIONAL ||
         descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC ||
         descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_OPTIMIZATION ||
         descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_CONTROL_FLOW) {
        return true;
    }
    minic_parser_error(parser, "unsupported block-scope extern declaration-head attribute");
    return false;
}

static bool declare_block_scope_extern_object_declarator(MinicParser *parser,
                                                         MinicType base_type,
                                                         MinicSourceSpan name_span,
                                                         bool is_weak) {
    MinicGlobalObjectId current_scope_object;
    MinicGlobalObjectId existing_id;
    MinicGlobalObjectId object_id;
    MinicType object_type;
    size_t array_type_begin;
    bool is_array;
    bool declarator_is_weak;

    existing_id = minic_parser_find_global_object_entity(parser, name_span);
    array_type_begin = parser->program->array_type_count;
    declarator_is_weak = is_weak;
    object_type = base_type;
    if (!minic_parser_parse_array_declarator_suffix(
            parser, object_type, true, &object_type, &is_array) ||
        !minic_parser_parse_gnu_attribute_lists(
            parser, consume_block_scope_extern_head_attribute, &declarator_is_weak)) {
        return false;
    }
    (void)is_array;
    if (!minic_parser_declare_block_scope_extern_object(
            parser, name_span, object_type, &object_id)) {
        return false;
    }
    if (declarator_is_weak &&
        !minic_c0_global_object_set_weak(parser->program, object_id, true)) {
        minic_parser_error(parser, "GNU weak requires external object linkage");
        return false;
    }
    /* Redeclaration parsing may materialize temporary array descriptors.
     * The entity merge owns any composite-type mutation; descriptors created
     * only to describe the incoming declarator must not survive ownerless. */
    if (existing_id != MINIC_GLOBAL_OBJECT_INVALID) {
        parser->program->array_type_count = array_type_begin;
    }
    current_scope_object =
        minic_parser_find_scoped_global_object_in_current_scope(parser, name_span);
    if (minic_parser_name_bound_in_current_scope(parser, name_span)) {
        if (current_scope_object != object_id) {
            minic_parser_error(parser,
                               "block-scope extern object conflicts with local declaration");
            return false;
        }
    } else if (!minic_parser_bind_scoped_global_object(parser, name_span, object_id)) {
        return false;
    }
    return true;
}

bool minic_parser_local_decl_extern(MinicParser *parser) {
    MinicSourceSpan name_span;
    MinicType parameter_types[16];
    MinicType return_type;
    MinicFunctionId function_id;
    const MinicFunction *existing_function;
    size_t parameter_count;
    bool is_variadic;
    bool is_weak;

    parameter_count = 0U;
    is_variadic = false;
    is_weak = false;
    (void)memset(parameter_types, 0, sizeof(parameter_types));

    if (!minic_parser_parse_gnu_prefix_function_attributes(parser, false, false) ||
        !minic_parser_expect(parser, MINIC_TOKEN_KW_EXTERN, "expected keyword 'extern'") ||
        /* C permits block-scope extern declarations of incomplete record
         * objects: extern struct user_namespace init_user_ns;
         * This owns NO storage and only its address can be consumed until
         * the record is completed. The shared complete-object validator
         * remains required for ordinary local definitions, initialized
         * locals, and record-by-value operations. Do not relax that API. */
        !minic_parser_parse_type_name_preserving_incomplete(parser, &return_type) ||
        !minic_parser_parse_gnu_attribute_lists(
            parser, consume_block_scope_extern_head_attribute, &is_weak)) {
        return false;
    }
    if (!minic_parser_parse_direct_declarator_name(parser, &name_span)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "expected block-scope extern function name");
        }
        return false;
    }

    if (minic_type_is_function(return_type)) {
        /* A function typedef remains a function declaration at block scope,
         * even without a parameter suffix:
         *   typedef void (replace_t)(unsigned long);
         *   extern replace_t idmap_cpu_replace_ttbr1;
         * Reuse the exact existing function entity/signature path; it is
         * never an extern *object* with function-typed storage. */
        const MinicFunctionType *signature =
            minic_c0_program_function_type(parser->program, return_type.function_type_id);
        size_t index;
        if (signature == NULL || signature->parameter_count > MINIC_MAX_FUNCTION_PARAMETERS ||
            parser->current.kind != MINIC_TOKEN_SEMICOLON) {
            minic_parser_error(parser, "invalid block-scope function-typed extern declarator");
            return false;
        }
        parameter_count = signature->parameter_count;
        is_variadic = signature->is_variadic;
        for (index = 0U; index < parameter_count; ++index) {
            parameter_types[index] = signature->parameter_types[index];
        }
        return_type = signature->return_type;
    } else {
        if (parser->current.kind != MINIC_TOKEN_LPAREN) {
            for (;;) {
                if (!declare_block_scope_extern_object_declarator(
                        parser, return_type, name_span, is_weak)) {
                    return false;
                }
                if (parser->current.kind != MINIC_TOKEN_COMMA) {
                    break;
                }
                if (!minic_parser_advance(parser) ||
                    !minic_parser_parse_direct_declarator_name(parser, &name_span)) {
                    if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                        minic_parser_error(parser,
                                           "expected block-scope extern object declarator name");
                    }
                    return false;
                }
            }
            return minic_parser_expect(parser,
                                       MINIC_TOKEN_SEMICOLON,
                                       "expected ';' after block-scope extern object declaration");
        }
        if (!minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '('") ||
            !minic_parser_parse_parameter_list(
                parser, NULL, parameter_types, &parameter_count, false, &is_variadic) ||
            !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')'") ||
            !minic_parser_parse_gnu_function_attributes(parser)) {
            return false;
        }
    }
    if (parser->current.kind != MINIC_TOKEN_SEMICOLON) {
        minic_parser_error(
            parser, "block-scope extern declaration must declare a function and end with ';'");
        return false;
    }

    function_id = minic_parser_find_function(parser, name_span);
    if (function_id != MINIC_FUNCTION_INVALID) {
        existing_function = minic_c0_program_function(parser->program, function_id);
        if (!minic_parser_function_signature_matches(
                parser->program, existing_function, return_type,
                parameter_types, parameter_count, is_variadic)) {
            minic_parser_error(parser, "conflicting block-scope extern function declaration");
            return false;
        }
    } else if (!minic_c0_program_add_function(parser->program,
                                              parser->source + name_span.begin.offset,
                                              minic_parser_span_length(name_span),
                                              parser->program->local_count,
                                              0U,
                                              MINIC_BLOCK_INVALID,
                                              &function_id) ||
               !minic_c0_program_set_function_signature(
                   parser->program, function_id, return_type, parameter_types, parameter_count) ||
               !minic_c0_program_set_function_internal(parser->program, function_id, false) ||
               !minic_c0_program_set_function_variadic(parser->program, function_id, is_variadic)) {
        minic_parser_error(parser, "out of memory while declaring block-scope extern function");
        return false;
    }

    if (is_weak && !minic_c0_program_set_function_weak(parser->program, function_id, true)) {
        minic_parser_error(parser, "GNU weak requires external function linkage");
        return false;
    }

    return minic_parser_advance(parser);
}

typedef struct MinicStaticLocalAttributeContext {
    char section_name[256];
    size_t section_name_length;
    size_t explicit_alignment;
    bool has_section;
} MinicStaticLocalAttributeContext;

static bool consume_static_local_interleaved_attribute(MinicParser *parser,
                                                       const MinicParsedAttribute *attribute,
                                                       void *opaque_context);

static bool parse_inferred_static_local_array(MinicParser *parser,
                                              MinicType element_type,
                                              MinicSourceSpan name_span,
                                              MinicStaticLocalAttributeContext *attributes,
                                              MinicGlobalObjectId *out_object_id) {
    const MinicArrayType *literal_array;
    MinicGlobalObjectId object_id;
    MinicSourceSpan literal_span;
    MinicType literal_type;
    MinicType object_type;

    if (parser == NULL || attributes == NULL || parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return false;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RBRACKET, "expected ']'")) {
        return false;
    }
    /* The omitted outer bound belongs to the source-level array. Parse any
       following fixed dimensions as the complete element type of that outer
       inferred array, so T[][N] reuses the normal declarator/type registry. */
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        MinicType nested_element_type;
        bool is_array;

        if (!minic_parser_parse_array_declarator_suffix(
                parser, element_type, false, &nested_element_type, &is_array) ||
            !is_array) {
            return false;
        }
        element_type = nested_element_type;
    }
    if (!minic_parser_parse_gnu_attribute_lists(
            parser, consume_static_local_interleaved_attribute, attributes) ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after inferred array")) {
        return false;
    }

    if (parser->current.kind == MINIC_TOKEN_STRING_LITERAL ||
        parser->current.kind == MINIC_TOKEN_WIDE_STRING_LITERAL) {
        if (!minic_parser_static_string_element_compatible(
                parser, element_type, parser->current.kind)) {
            minic_parser_error(parser,
                               "string initializer requires a compatible static local array element type");
            return false;
        }
        if (!minic_parser_create_string_literal_object(
                parser, &object_id, &literal_type, &literal_span)) {
            return false;
        }
        literal_array = minic_c0_program_array_type(parser->program, literal_type.array_type_id);
        if (literal_array == NULL || !minic_type_is_array(literal_type) ||
            !minic_c0_program_add_array_type(
                parser->program, element_type, literal_array->element_count, &object_type)) {
            minic_parser_error(parser, "cannot infer static local string array type");
            return false;
        }

        /* The literal helper owns decoding and byte initialization. Re-type that internal
           object to the declaration's qualified char element type, then bind the source-level
           static-local name to the same storage object. */
        parser->program->global_objects[object_id].type = object_type;
        parser->program->global_objects[object_id].is_read_only = minic_type_is_const(element_type);
        if (!minic_parser_bind_scoped_global_object(parser, name_span, object_id)) {
            return false;
        }
        *out_object_id = object_id;
        return true;
    }

    if (parser->current.kind == MINIC_TOKEN_LBRACE) {
        char symbol_name[96];
        int symbol_length;

        if (!minic_type_is_integer(element_type) && !minic_type_is_pointer(element_type) &&
            !minic_type_is_record(element_type) && !minic_type_is_array(element_type)) {
            minic_parser_error(
                parser,
                "brace-initialized inferred static array requires scalar or aggregate elements");
            return false;
        }
        if (minic_type_is_record(element_type) &&
            !minic_parser_require_complete_object_type(
                parser,
                element_type,
                "inferred static record array requires a complete element type")) {
            return false;
        }
        symbol_length = snprintf(symbol_name,
                                 sizeof(symbol_name),
                                 "__minic_static_local_%zu_%zu",
                                 (size_t)parser->current_function,
                                 parser->program->global_object_count);
        if (symbol_length <= 0 || (size_t)symbol_length >= sizeof(symbol_name) ||
            !minic_c0_program_add_incomplete_array_type(
                parser->program, element_type, &object_type) ||
            !minic_c0_program_add_global_object(parser->program,
                                                symbol_name,
                                                (size_t)symbol_length,
                                                object_type,
                                                true,
                                                minic_type_is_const(element_type),
                                                &object_id) ||
            !minic_parser_parse_static_storage_initializer_value(parser, object_id, object_type) ||
            !minic_parser_bind_scoped_global_object(parser, name_span, object_id)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot initialize inferred static local array");
            }
            return false;
        }
        *out_object_id = object_id;
        return true;
    }

    minic_parser_error(parser,
                       "inferred static local array requires a string or brace initializer");
    return false;
}

static bool parse_static_local_record_initializer(MinicParser *parser,
                                                  MinicType declared_type,
                                                  MinicSourceSpan name_span,
                                                  MinicGlobalObjectId *out_object_id) {
    char symbol_name[96];
    MinicGlobalObjectId object_id;
    int symbol_length;

    if (parser == NULL || out_object_id == NULL || !minic_type_is_record(declared_type) ||
        !minic_parser_require_complete_object_type(
            parser, declared_type, "static local record requires a complete record type")) {
        return false;
    }
    symbol_length = snprintf(symbol_name,
                             sizeof(symbol_name),
                             "__minic_static_local_%zu_%zu",
                             (size_t)parser->current_function,
                             parser->program->global_object_count);
    if (symbol_length <= 0 || (size_t)symbol_length >= sizeof(symbol_name)) {
        minic_parser_error(parser, "cannot build static local record symbol name");
        return false;
    }
    if (!minic_c0_program_add_global_object(parser->program,
                                            symbol_name,
                                            (size_t)symbol_length,
                                            declared_type,
                                            true,
                                            minic_type_is_const(declared_type),
                                            &object_id) ||
        !minic_parser_bind_scoped_global_object(parser, name_span, object_id) ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after static record") ||
        !minic_parser_parse_static_storage_initializer_value(parser, object_id, declared_type)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot initialize static local record storage");
        }
        return false;
    }
    *out_object_id = object_id;
    return true;
}

static bool add_implicitly_zero_initialized_static_local(MinicParser *parser,
                                                         MinicType declared_type,
                                                         MinicSourceSpan name_span,
                                                         MinicGlobalObjectId *out_object_id) {
    char symbol_name[96];
    MinicGlobalObjectId object_id;
    int symbol_length;

    if (parser == NULL ||
        !minic_parser_require_complete_object_type(
            parser,
            declared_type,
            "static local object without an initializer requires a complete object type")) {
        return false;
    }
    symbol_length = snprintf(symbol_name,
                             sizeof(symbol_name),
                             "__minic_static_local_%zu_%zu",
                             (size_t)parser->current_function,
                             parser->program->global_object_count);
    if (symbol_length <= 0 || (size_t)symbol_length >= sizeof(symbol_name)) {
        minic_parser_error(parser, "cannot build implicit-zero static local symbol name");
        return false;
    }
    if (!minic_c0_program_add_global_object(parser->program,
                                            symbol_name,
                                            (size_t)symbol_length,
                                            declared_type,
                                            true,
                                            minic_type_is_const(declared_type),
                                            &object_id) ||
        !minic_c0_global_object_set_zero_initialized(parser->program, object_id) ||
        !minic_parser_bind_scoped_global_object(parser, name_span, object_id)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot create implicit-zero static local storage");
        }
        return false;
    }
    *out_object_id = object_id;
    return true;
}

static bool parse_static_local_array_declarator(MinicParser *parser,
                                                MinicType base_type,
                                                MinicStaticLocalAttributeContext *attributes,
                                                MinicGlobalObjectId *out_object_id) {
    char symbol_name[96];
    MinicSourceSpan name_span;
    MinicType declared_type;
    MinicType object_type;
    MinicGlobalObjectId object_id;
    size_t bounds[8];
    size_t bound_count;
    size_t index;
    int symbol_length;
    bool parenthesized_function_declarator;

    if (attributes == NULL || out_object_id == NULL) {
        return false;
    }
    *out_object_id = MINIC_GLOBAL_OBJECT_INVALID;
    /* GNU Linux static-call metadata uses block-scope static declarators
     * with USED/SECTION attributes BETWEEN '*' and the object name:
     * static void * __attribute__((used)) __attribute__((section(...))) key;
     * The shared static attribute consumer already owns validation, section
     * placement and conflict checks after the name. Reuse it at this earlier
     * legal declarator position rather than inventing a parser-only bypass. */
    if (!minic_parser_parse_pointer_declarator(parser, base_type, &declared_type) ||
        !minic_parser_parse_gnu_attribute_lists(
            parser, consume_static_local_interleaved_attribute, attributes)) {
        return false;
    }
    if (minic_type_is_void(declared_type)) {
        minic_parser_error(parser, "static local object cannot have void type");
        return false;
    }
    parenthesized_function_declarator = false;
    if (local_declarator_starts_function_pointer(parser)) {
        MinicParsedFunctionDeclarator declarator;

        if (!minic_parser_parse_parenthesized_function_declarator(
                parser, true, true, &declarator) ||
            !minic_parser_build_function_declarator_type(
                parser, declared_type, &declarator, &declared_type)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot build static local function declarator type");
            }
            return false;
        }
        name_span = declarator.name_span;
        parenthesized_function_declarator = true;
    } else {
        if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
            minic_parser_error(parser, "expected static local name");
            return false;
        }
        name_span = parser->current.span;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    if (minic_parser_name_bound_in_current_scope(parser, name_span)) {
        minic_parser_error(parser, "duplicate local declaration");
        return false;
    }
    if (!minic_parser_parse_gnu_attribute_lists(
            parser, consume_static_local_interleaved_attribute, attributes)) {
        return false;
    }

    if (parenthesized_function_declarator && minic_type_is_array(declared_type)) {
        char array_symbol_name[96];
        MinicGlobalObjectId array_object_id;
        int array_symbol_length;

        array_symbol_length = snprintf(array_symbol_name,
                                       sizeof(array_symbol_name),
                                       "__minic_static_local_%zu_%zu",
                                       (size_t)parser->current_function,
                                       parser->program->global_object_count);
        if (array_symbol_length <= 0 ||
            (size_t)array_symbol_length >= sizeof(array_symbol_name) ||
            !minic_c0_program_add_global_object(parser->program,
                                                array_symbol_name,
                                                (size_t)array_symbol_length,
                                                declared_type,
                                                true,
                                                minic_type_is_const(declared_type),
                                                &array_object_id)) {
            minic_parser_error(parser, "cannot create static local function-pointer array");
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_EQUAL) {
            if (!minic_parser_advance(parser) ||
                !minic_parser_parse_static_storage_initializer_value(
                    parser, array_object_id, declared_type)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(
                        parser, "cannot initialize static local function-pointer array");
                }
                return false;
            }
        } else if (!minic_c0_global_object_set_zero_initialized(
                       parser->program, array_object_id)) {
            minic_parser_error(parser, "cannot zero static local function-pointer array");
            return false;
        }
        if (!minic_parser_bind_scoped_global_object(parser, name_span, array_object_id)) {
            return false;
        }
        *out_object_id = array_object_id;
        return true;
    }

    bound_count = 0U;
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        MinicParser probe;

        probe = *parser;
        if (!minic_parser_advance(&probe)) {
            return false;
        }
        if (probe.current.kind == MINIC_TOKEN_RBRACKET) {
            return parse_inferred_static_local_array(
                parser, declared_type, name_span, attributes, out_object_id);
        }
    }
    while (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        if (bound_count >= sizeof(bounds) / sizeof(bounds[0])) {
            minic_parser_error(parser, "at most eight array dimensions are supported");
            return false;
        }
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_fixed_array_bound(parser, &bounds[bound_count])) {
            return false;
        }
        bound_count += 1U;
    }
    if (!minic_parser_parse_gnu_attribute_lists(
            parser, consume_static_local_interleaved_attribute, attributes)) {
        return false;
    }
    if (bound_count == 0U) {
        char scalar_symbol_name[96];
        MinicGlobalObjectId scalar_object_id;
        uint64_t scalar_bits;
        int scalar_symbol_length;

        if (parser->current.kind != MINIC_TOKEN_EQUAL) {
            return add_implicitly_zero_initialized_static_local(
                parser, declared_type, name_span, out_object_id);
        }
        if (minic_type_is_record(declared_type)) {
            return parse_static_local_record_initializer(
                parser, declared_type, name_span, out_object_id);
        }
        if (!minic_type_is_integer(declared_type) && !minic_type_is_pointer(declared_type)) {
            minic_parser_error(parser,
                               "static local scalar currently requires an integer or pointer type");
            return false;
        }

        scalar_symbol_length = snprintf(scalar_symbol_name,
                                        sizeof(scalar_symbol_name),
                                        "__minic_static_local_%zu_%zu",
                                        (size_t)parser->current_function,
                                        parser->program->global_object_count);
        if (scalar_symbol_length <= 0 ||
            (size_t)scalar_symbol_length >= sizeof(scalar_symbol_name)) {
            minic_parser_error(parser, "cannot build static local scalar symbol name");
            return false;
        }
        if (!minic_c0_program_add_global_object(parser->program,
                                                scalar_symbol_name,
                                                (size_t)scalar_symbol_length,
                                                declared_type,
                                                true,
                                                minic_type_is_const(declared_type),
                                                &scalar_object_id) ||
            !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after static scalar")) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot begin static local scalar initializer");
            }
            return false;
        }

        if (minic_type_is_pointer(declared_type)) {
            if (!minic_parser_parse_static_pointer_object_initializer(
                    parser, scalar_object_id, declared_type) ||
                !minic_parser_bind_scoped_global_object(parser, name_span, scalar_object_id)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser, "cannot finalize static local pointer storage");
                }
                return false;
            }
            *out_object_id = scalar_object_id;
            return true;
        }

        if (!minic_parser_parse_integer_initializer_bits(parser, declared_type, &scalar_bits)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser,
                                   "static local integer requires an integer constant expression");
            }
            return false;
        }
        if ((scalar_bits == 0U &&
             !minic_c0_global_object_set_zero_initialized(parser->program, scalar_object_id)) ||
            (scalar_bits != 0U && !minic_c0_global_object_add_initializer_bits(
                                    parser->program, scalar_object_id, scalar_bits)) ||
            !minic_parser_bind_scoped_global_object(parser, name_span, scalar_object_id)) {
            minic_parser_error(parser, "cannot finalize static local integer storage");
            return false;
        }
        *out_object_id = scalar_object_id;
        return true;
    }
    if (!minic_parser_require_complete_object_type(
            parser, declared_type, "static local array requires a complete element type")) {
        return false;
    }

    object_type = declared_type;
    for (index = bound_count; index > 0U; --index) {
        if (!minic_c0_program_add_array_type(
                parser->program, object_type, bounds[index - 1U], &object_type)) {
            minic_parser_error(parser, "out of memory while building static local array type");
            return false;
        }
    }

    symbol_length = snprintf(symbol_name,
                             sizeof(symbol_name),
                             "__minic_static_local_%zu_%zu",
                             (size_t)parser->current_function,
                             parser->program->global_object_count);
    if (symbol_length <= 0 || (size_t)symbol_length >= sizeof(symbol_name)) {
        minic_parser_error(parser, "cannot build static local symbol name");
        return false;
    }
    if (!minic_c0_program_add_global_object(parser->program,
                                            symbol_name,
                                            (size_t)symbol_length,
                                            object_type,
                                            true,
                                            minic_type_is_const(declared_type),
                                            &object_id)) {
        minic_parser_error(parser, "cannot add static local array object");
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_EQUAL) {
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_static_storage_initializer_value(parser, object_id, object_type)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot initialize static local array storage");
            }
            return false;
        }
    } else if (!minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {
        minic_parser_error(parser, "cannot zero-initialize static local array object");
        return false;
    }
    if (!minic_parser_bind_scoped_global_object(parser, name_span, object_id)) {
        return false;
    }
    *out_object_id = object_id;
    return true;
}

static bool consume_static_local_interleaved_attribute(MinicParser *parser,
                                                       const MinicParsedAttribute *attribute,
                                                       void *opaque_context) {
    MinicStaticLocalAttributeContext *context;
    const MinicAttributeDescriptor *descriptor;

    if (parser == NULL || attribute == NULL || opaque_context == NULL) {
        return false;
    }
    context = (MinicStaticLocalAttributeContext *)opaque_context;
    descriptor = attribute->descriptor;
    if (descriptor == NULL) {
        minic_parser_error(parser, "unsupported GNU attribute on static local object");
        return false;
    }
    if (!minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_OBJECT)) {
        minic_parser_error(parser, "GNU attribute is not valid on a static local object");
        return false;
    }
    if ((descriptor->kind == MINIC_ATTRIBUTE_UNUSED &&
         descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_INFORMATIONAL) ||
        (descriptor->kind == MINIC_ATTRIBUTE_USED &&
         descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_OPTIMIZATION)) {
        return true;
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_ALIGNED) {
        return minic_parser_apply_alignment_attribute(
            parser, attribute, "static local object", &context->explicit_alignment);
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_SECTION) {
        return minic_parser_apply_section_attribute(parser,
                                                    attribute,
                                                    context->section_name,
                                                    sizeof(context->section_name),
                                                    &context->section_name_length,
                                                    &context->has_section);
    }
    minic_parser_error(
        parser,
        "GNU static local object attribute semantics are not implemented at this placement");
    return false;
}

bool minic_parser_local_decl_static(MinicParser *parser) {
    MinicStaticLocalAttributeContext declaration_attributes;
    MinicType base_type;
    bool leading_const;
    bool leading_volatile;

    (void)memset(&declaration_attributes, 0, sizeof(declaration_attributes));
    if (parser->current_function == MINIC_FUNCTION_INVALID ||
        !minic_parser_expect(parser, MINIC_TOKEN_KW_STATIC, "expected keyword 'static'") ||
        !minic_parser_parse_gnu_attribute_lists(
            parser, consume_static_local_interleaved_attribute, &declaration_attributes)) {
        return false;
    }
    /* A declaration-specifier attribute is permitted between qualifiers and
       its base type: `static const __attribute__((section(...))) ulong x`.
       Preserve semantic attributes on the local global-object owner. */
    leading_const = false;
    leading_volatile = false;
    while (parser->current.kind == MINIC_TOKEN_KW_CONST ||
           parser->current.kind == MINIC_TOKEN_KW_VOLATILE) {
        if (parser->current.kind == MINIC_TOKEN_KW_CONST) {
            leading_const = true;
        } else {
            leading_volatile = true;
        }
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_gnu_attribute_lists(
                parser, consume_static_local_interleaved_attribute, &declaration_attributes)) {
            return false;
        }
    }
    if (!minic_parser_parse_type_specifiers(parser, &base_type) ||
        !minic_parser_parse_gnu_attribute_lists(
            parser, consume_static_local_interleaved_attribute, &declaration_attributes)) {
        return false;
    }
    if ((leading_const && !minic_type_add_const(base_type, &base_type)) ||
        (leading_volatile && !minic_type_add_volatile(base_type, &base_type))) {
        minic_parser_error(parser, "cannot qualify static local object type");
        return false;
    }
    for (;;) {
        MinicStaticLocalAttributeContext attributes;
        MinicGlobalObjectId object_id;

        attributes = declaration_attributes;
        if (!parse_static_local_array_declarator(parser, base_type, &attributes, &object_id)) {
            return false;
        }
        if (attributes.has_section || attributes.explicit_alignment != 0U) {
            MinicDeclarationExternalObjectAttributes semantic_attributes;

            (void)memset(&semantic_attributes, 0, sizeof(semantic_attributes));
            semantic_attributes.section_name = attributes.section_name;
            semantic_attributes.section_name_length = attributes.section_name_length;
            semantic_attributes.explicit_alignment = attributes.explicit_alignment;
            semantic_attributes.has_section = attributes.has_section;
            if (!minic_declaration_apply_object_attributes(
                    parser->program, object_id, &semantic_attributes)) {
                minic_parser_error(parser,
                                   "cannot apply GNU attributes to static local object");
                return false;
            }
        }
        if (parser->current.kind != MINIC_TOKEN_COMMA) {
            break;
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    return minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';'");
}
