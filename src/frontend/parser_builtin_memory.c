#include "frontend/parser_builtin_memory.h"

#include <string.h>

/* Identifier -> canonical C signature -> function declaration -> call AST.
 * memcpy and memmove require a const source pointer; memset requires int.
 * Never let these library-bridge semantics leak into the target backends. */
typedef struct {
    const char *builtin_name;
    const char *callee_name;
    const char *signature_error;
    const char *conflict_error;
    const char *create_error;
    const char *invalid_error;
    const char *lparen_error;
    bool const_source;
} MinicBuiltinMemorySpec;

static const MinicBuiltinMemorySpec memory_specs[] = {
    {
        "__builtin_memcpy", "memcpy",
        "cannot form __builtin_memcpy signature",
        "__builtin_memcpy conflicts with memcpy declaration",
        "cannot create canonical memcpy builtin callee",
        "invalid canonical memcpy builtin callee",
        "expected '(' after __builtin_memcpy", true
    },
    {
        "__builtin_memmove", "memmove",
        "cannot form __builtin_memmove signature",
        "__builtin_memmove conflicts with memmove declaration",
        "cannot create canonical memmove builtin callee",
        "invalid canonical memmove builtin callee",
        "expected '(' after __builtin_memmove", true
    },
    {
        "__builtin_memset", "memset",
        "cannot form __builtin_memset signature",
        "__builtin_memset conflicts with memset declaration",
        "cannot create canonical memset builtin callee",
        "invalid canonical memset builtin callee",
        "expected '(' after __builtin_memset", false
    }
};

static const MinicBuiltinMemorySpec *memory_spec_for_token(const MinicParser *parser) {
    size_t i;
    for (i = 0U; i < sizeof(memory_specs) / sizeof(memory_specs[0]); ++i) {
        if (minic_parser_current_identifier_lexeme_equals_cstr(
                parser, memory_specs[i].builtin_name)) {
            return &memory_specs[i];
        }
    }
    return NULL;
}

static bool memory_name_equals(const MinicFunction *function, const char *name) {
    size_t length;
    if (function == NULL || name == NULL) {
        return false;
    }
    length = strlen(name);
    return function->name_length == length &&
           memcmp(function->name, name, length) == 0;
}

static bool ensure_builtin_memory_callee(
    MinicParser *parser, const MinicBuiltinMemorySpec *spec,
    MinicFunctionId *function_id) {
    MinicType void_type;
    MinicType const_void_type;
    MinicType void_pointer_type;
    MinicType const_void_pointer_type;
    MinicType parameter_types[3];
    size_t index;

    if (parser == NULL || parser->program == NULL ||
        spec == NULL || function_id == NULL) {
        return false;
    }

    void_type = minic_type_void();
    if (spec->const_source) {
        /* Keep the previous memcpy/memmove construction and error order. */
        if (!minic_type_add_const(void_type, &const_void_type) ||
            !minic_type_pointer_to(void_type, &void_pointer_type) ||
            !minic_type_pointer_to(const_void_type, &const_void_pointer_type)) {
            minic_parser_error(parser, spec->signature_error);
            return false;
        }
    } else if (!minic_type_pointer_to(void_type, &void_pointer_type)) {
        minic_parser_error(parser, spec->signature_error);
        return false;
    }

    parameter_types[0] = void_pointer_type;
    parameter_types[1] = spec->const_source
        ? const_void_pointer_type : minic_type_int();
    /* sizeof/alignof returns unsigned long in MiniC's target-owned model. */
    parameter_types[2] = minic_type_unsigned_long();

    parser->perf_builtin_mem_calls += 1U;
    for (index = 0U; index < parser->program->function_count; ++index) {
        const MinicFunction *function;

        parser->perf_builtin_mem_steps += 1U;
        function = minic_c0_program_function(parser->program, index);
        if (!memory_name_equals(function, spec->callee_name)) {
            continue;
        }
        if (function->is_variadic || function->parameter_count != 3U ||
            !minic_type_equal(function->return_type, void_pointer_type) ||
            !minic_type_equal(function->parameter_types[0], parameter_types[0]) ||
            !minic_type_equal(function->parameter_types[1], parameter_types[1]) ||
            !minic_type_equal(function->parameter_types[2], parameter_types[2])) {
            minic_parser_error(parser, spec->conflict_error);
            return false;
        }
        *function_id = index;
        return true;
    }

    if (!minic_c0_program_add_function(parser->program,
                                       spec->callee_name,
                                       strlen(spec->callee_name),
                                       parser->program->local_count,
                                       0U,
                                       MINIC_BLOCK_INVALID,
                                       function_id) ||
        !minic_c0_program_set_function_signature(
            parser->program, *function_id, void_pointer_type,
            parameter_types, 3U)) {
        minic_parser_error(parser, spec->create_error);
        return false;
    }
    return true;
}

bool minic_parser_parse_builtin_memory(MinicParser *parser,
                                       MinicExpressionId *expression_id) {
    const MinicBuiltinMemorySpec *spec;
    MinicExpression call_expression;
    MinicFunctionId function_id;
    const MinicFunction *callee;
    MinicSourcePosition begin;
    MinicSourcePosition end;

    if (parser == NULL || expression_id == NULL) {
        return false;
    }
    spec = memory_spec_for_token(parser);
    if (spec == NULL) {
        return false;
    }
    begin = parser->current.span.begin;
    if (!ensure_builtin_memory_callee(parser, spec, &function_id) ||
        !minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, spec->lparen_error)) {
        return false;
    }
    callee = minic_c0_program_function(parser->program, function_id);
    if (callee == NULL) {
        minic_parser_error(parser, spec->invalid_error);
        return false;
    }

    (void)memset(&call_expression, 0, sizeof(call_expression));
    call_expression.kind = MINIC_EXPRESSION_CALL;
    call_expression.span.begin = begin;
    call_expression.type = callee->return_type;
    call_expression.value_category = MINIC_VALUE_RVALUE;
    call_expression.value.call.function_id = function_id;
    if (!minic_parser_expression_parse_call_arguments(parser, &call_expression, callee)) {
        return false;
    }
    end = parser->current.span.end;
    if (!minic_parser_advance(parser)) {
        return false;
    }
    call_expression.span.end = end;
    return minic_parser_add_expression(parser, &call_expression, expression_id);
}
