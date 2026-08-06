#include "frontend/parser.h"
#include "frontend/parser_internal.h"

#include <string.h>

static bool function_signature_matches(const MinicFunction *function,
                                       MinicType return_type,
                                       const MinicType *parameter_types,
                                       size_t parameter_count) {
    size_t parameter_index;

    if (function == NULL || !minic_type_equal(function->return_type, return_type) ||
        function->parameter_count != parameter_count) {
        return false;
    }
    for (parameter_index = 0U; parameter_index < parameter_count; ++parameter_index) {
        if (!minic_type_equal(function->parameter_types[parameter_index],
                              parameter_types[parameter_index])) {
            return false;
        }
    }
    return true;
}

static bool parse_parameter_list(MinicParser *parser,
                                 MinicSourceSpan *parameter_name_spans,
                                 MinicType *parameter_types,
                                 size_t *parameter_count) {
    if (parser->current.kind == MINIC_TOKEN_RPAREN) {
        return true;
    }

    for (;;) {
        MinicType parameter_type;

        if (*parameter_count >= 8U) {
            minic_parser_error(parser, "at most eight parameters are supported");
            return false;
        }
        if (!minic_parser_parse_type_name(parser, &parameter_type)) {
            return false;
        }
        if (minic_type_is_void(parameter_type)) {
            if (*parameter_count == 0U && parser->current.kind == MINIC_TOKEN_RPAREN) {
                return true;
            }
            minic_parser_error(parser, "parameter type cannot be bare void");
            return false;
        }
        if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
            minic_parser_error(parser, "expected parameter name");
            return false;
        }

        parameter_name_spans[*parameter_count] = parser->current.span;
        parameter_types[*parameter_count] = parameter_type;
        *parameter_count += 1U;
        if (!minic_parser_advance(parser)) {
            return false;
        }
        if (parser->current.kind != MINIC_TOKEN_COMMA) {
            return true;
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
}

static bool parse_function(MinicParser *parser, bool is_internal) {
    MinicSourceSpan name_span;
    MinicSourceSpan parameter_name_spans[8];
    MinicType parameter_types[8];
    MinicType return_type;
    MinicBlockId body_block;
    MinicFunctionId function_id;
    const MinicFunction *existing_function;
    MinicLocal parameter_local;
    MinicLocalId parameter_local_id;
    size_t parameter_count;
    size_t local_begin;
    size_t local_count;
    bool is_main;

    body_block = MINIC_BLOCK_INVALID;
    parameter_count = 0U;
    (void)memset(parameter_name_spans, 0, sizeof(parameter_name_spans));
    (void)memset(parameter_types, 0, sizeof(parameter_types));
    if (is_internal &&
        !minic_parser_expect(parser, MINIC_TOKEN_KW_STATIC, "expected keyword 'static'")) {
        return false;
    }
    if (!minic_parser_parse_type_name(parser, &return_type)) {
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected function name");
        return false;
    }

    name_span = parser->current.span;
    function_id = minic_parser_find_function(parser, name_span);
    is_main = minic_parser_span_length(name_span) == 4U &&
              memcmp(parser->source + name_span.begin.offset, "main", 4U) == 0;
    if (is_main && !minic_type_is_integer(return_type)) {
        minic_parser_error(parser, "main must return int");
        return false;
    }
    if (is_main && is_internal) {
        minic_parser_error(parser, "main cannot have internal linkage");
        return false;
    }

    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '('") ||
        !parse_parameter_list(parser, parameter_name_spans, parameter_types, &parameter_count) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')'")) {
        return false;
    }
    if (is_main && parameter_count != 0U) {
        minic_parser_error(parser, "main parameters are not supported yet");
        return false;
    }

    if (function_id != MINIC_FUNCTION_INVALID) {
        existing_function = minic_c0_program_function(parser->program, function_id);
        if (!function_signature_matches(
                existing_function, return_type, parameter_types, parameter_count) ||
            existing_function->is_internal != is_internal) {
            minic_parser_error(parser, "conflicting function declaration");
            return false;
        }
    }

    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        if (function_id == MINIC_FUNCTION_INVALID) {
            if (!minic_c0_program_add_function(parser->program,
                                               parser->source + name_span.begin.offset,
                                               minic_parser_span_length(name_span),
                                               parser->program->local_count,
                                               0U,
                                               MINIC_BLOCK_INVALID,
                                               &function_id) ||
                !minic_c0_program_set_function_signature(
                    parser->program, function_id, return_type, parameter_types, parameter_count) ||
                !minic_c0_program_set_function_internal(
                    parser->program, function_id, is_internal)) {
                minic_parser_error(parser, "out of memory while declaring function");
                return false;
            }
        }
        return minic_parser_advance(parser);
    }

    if (!minic_type_is_integer(return_type) && !minic_type_is_void(return_type) &&
        !minic_type_is_pointer(return_type)) {
        minic_parser_error(parser, "unsupported function return type");
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_LBRACE) {
        minic_parser_error(parser, "expected ';' or '{' after function declarator");
        return false;
    }
    if (function_id != MINIC_FUNCTION_INVALID) {
        existing_function = minic_c0_program_function(parser->program, function_id);
        if (existing_function == NULL || existing_function->is_defined) {
            minic_parser_error(parser, "duplicate function definition");
            return false;
        }
    }

    if (!minic_parser_advance(parser) ||
        !minic_c0_program_add_block(parser->program, &body_block)) {
        if (body_block == MINIC_BLOCK_INVALID) {
            minic_parser_error(parser, "out of memory while adding function body");
        }
        return false;
    }

    local_begin = parser->program->local_count;
    parser->local_begin = local_begin;
    if (!minic_parser_begin_scope(parser)) {
        return false;
    }
    {
        size_t parameter_index;

        for (parameter_index = 0U; parameter_index < parameter_count; ++parameter_index) {
            parameter_local.name_span = parameter_name_spans[parameter_index];
            parameter_local.type = parameter_types[parameter_index];
            parameter_local.element_count = 1U;
            parameter_local.storage_offset = 0U;
            if (minic_parser_find_local_in_current_scope(parser, parameter_local.name_span) !=
                MINIC_LOCAL_INVALID) {
                minic_parser_error(parser, "duplicate parameter name");
                return false;
            }
            if (!minic_c0_program_add_local(
                    parser->program, &parameter_local, &parameter_local_id)) {
                minic_parser_error(parser, "out of memory while adding parameter");
                return false;
            }
            if (!minic_parser_bind_local(parser, parameter_local.name_span, parameter_local_id)) {
                return false;
            }
        }
    }

    if (function_id == MINIC_FUNCTION_INVALID) {
        if (!minic_c0_program_add_function(parser->program,
                                           parser->source + name_span.begin.offset,
                                           minic_parser_span_length(name_span),
                                           local_begin,
                                           parameter_count,
                                           body_block,
                                           &function_id) ||
            !minic_c0_program_set_function_signature(
                parser->program, function_id, return_type, parameter_types, parameter_count) ||
            !minic_c0_program_set_function_internal(parser->program, function_id, is_internal)) {
            minic_parser_error(parser, "out of memory while adding function");
            return false;
        }
    } else if (!minic_c0_program_define_function(
                   parser->program, function_id, local_begin, body_block)) {
        minic_parser_error(parser, "cannot define previously declared function");
        return false;
    }
    parser->current_function = function_id;
    if (is_main) {
        parser->program->entry_function = function_id;
        parser->program->body_block = body_block;
    }

    parser->current_block = body_block;
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        if (parser->current.kind == MINIC_TOKEN_EOF) {
            minic_parser_error(parser, "expected '}' before end of file");
            return false;
        }
        if (!minic_parser_parse_statement(parser, true)) {
            return false;
        }
    }
    if (!minic_parser_add_default_return(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}'")) {
        return false;
    }

    local_count = parser->program->local_count - local_begin;
    if (!minic_c0_program_finish_function(parser->program, function_id, local_count)) {
        minic_parser_error(parser, "invalid local range while finishing function");
        return false;
    }
    minic_parser_end_scope(parser);
    parser->current_function = MINIC_FUNCTION_INVALID;
    return true;
}

static bool static_declaration_is_function(MinicParser *parser, bool *is_function) {
    MinicParser probe;
    MinicType declared_type;

    if (parser == NULL || is_function == NULL) {
        return false;
    }
    probe = *parser;
    if (!minic_parser_advance(&probe) || !minic_parser_parse_type_name(&probe, &declared_type)) {
        return false;
    }
    (void)declared_type;
    if (probe.current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected static declaration name");
        return false;
    }
    if (!minic_parser_advance(&probe)) {
        return false;
    }
    *is_function = probe.current.kind == MINIC_TOKEN_LPAREN;
    return true;
}

bool minic_parse_c0_program(const char *path,
                            const char *source,
                            size_t length,
                            MinicC0Program *program,
                            MinicDiagnostic *diagnostic) {
    MinicParser parser;
    bool success;

    (void)memset(&parser, 0, sizeof(parser));
    parser.path = path;
    parser.source = source;
    parser.diagnostic = diagnostic;
    parser.program = program;
    parser.current_block = MINIC_BLOCK_INVALID;
    parser.current_function = MINIC_FUNCTION_INVALID;
    minic_lexer_initialize(&parser.lexer, path, source, length);

    success = minic_parser_advance(&parser);
    while (success && parser.current.kind != MINIC_TOKEN_EOF) {
        if (parser.current.kind == MINIC_TOKEN_KW_TYPEDEF) {
            success = minic_parser_parse_typedef(&parser);
        } else if (parser.current.kind == MINIC_TOKEN_KW_STATIC) {
            bool is_function;

            if (!static_declaration_is_function(&parser, &is_function)) {
                success = false;
            } else if (is_function) {
                success = parse_function(&parser, true);
            } else {
                success = minic_parser_parse_static_global(&parser);
            }
        } else if (parser.current.kind == MINIC_TOKEN_KW_STRUCT) {
            success = minic_parser_parse_record_definition(&parser);
        } else {
            success = parse_function(&parser, false);
        }
    }
    if (success && program->entry_function == MINIC_FUNCTION_INVALID) {
        minic_parser_error(&parser, "translation unit requires an int main function");
        success = false;
    }

    minic_parser_destroy_scopes(&parser);
    return success;
}
