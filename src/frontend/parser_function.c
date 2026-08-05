#include "frontend/parser.h"
#include "frontend/parser_internal.h"

#include <string.h>

static bool parse_function(MinicParser *parser)
{
    MinicSourceSpan name_span;
    MinicSourceSpan parameter_name_spans[8];
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
    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_INT, "expected keyword 'int'")) {
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

    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '('") ) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_KW_VOID) {
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else if (parser->current.kind == MINIC_TOKEN_KW_INT) {
        for (;;) {
            if (parameter_count >= 8U ||
                !minic_parser_advance(parser) ||
                parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
                minic_parser_error(
                    parser,
                    parameter_count >= 8U
                        ? "at most eight int parameters are supported"
                        : "expected parameter name after 'int'");
                return false;
            }
            parameter_name_spans[parameter_count] = parser->current.span;
            parameter_count += 1U;
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind != MINIC_TOKEN_COMMA) {
                break;
            }
            if (!minic_parser_advance(parser) ||
                parser->current.kind != MINIC_TOKEN_KW_INT) {
                minic_parser_error(parser, "expected 'int' after ','");
                return false;
            }
        }
    } else if (parser->current.kind != MINIC_TOKEN_RPAREN) {
        minic_parser_error(parser, "expected 'void', 'int', or ')'");
        return false;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')'")) {
        return false;
    }
    if (is_main && parameter_count != 0U) {
        minic_parser_error(parser, "main parameters are not supported yet");
        return false;
    }

    if (function_id != MINIC_FUNCTION_INVALID) {
        existing_function = minic_c0_program_function(parser->program, function_id);
        if (existing_function == NULL ||
            existing_function->parameter_count != parameter_count) {
            minic_parser_error(parser, "conflicting function declaration");
            return false;
        }
    }

    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        if (function_id == MINIC_FUNCTION_INVALID) {
            if (!minic_c0_program_add_function(
                    parser->program,
                    parser->source + name_span.begin.offset,
                    minic_parser_span_length(name_span),
                    parser->program->local_count,
                    0U,
                    MINIC_BLOCK_INVALID,
                    &function_id) ||
                !minic_c0_program_set_function_parameter_count(
                    parser->program,
                    function_id,
                    parameter_count)) {
                minic_parser_error(parser, "out of memory while declaring function");
                return false;
            }
        }
        return minic_parser_advance(parser);
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

        for (parameter_index = 0U;
             parameter_index < parameter_count;
             ++parameter_index) {
            parameter_local.name_span = parameter_name_spans[parameter_index];
            parameter_local.type = minic_type_int();
            parameter_local.element_count = 1U;
            parameter_local.storage_offset = 0U;
            if (minic_parser_find_local_in_current_scope(
                    parser,
                    parameter_local.name_span) != MINIC_LOCAL_INVALID) {
                minic_parser_error(parser, "duplicate parameter name");
                return false;
            }
            if (!minic_c0_program_add_local(
                    parser->program,
                    &parameter_local,
                    &parameter_local_id)) {
                minic_parser_error(parser, "out of memory while adding parameter");
                return false;
            }
            if (!minic_parser_bind_local(
                    parser,
                    parameter_local.name_span,
                    parameter_local_id)) {
                return false;
            }
        }
    }

    if (function_id == MINIC_FUNCTION_INVALID) {
        if (!minic_c0_program_add_function(
                parser->program,
                parser->source + name_span.begin.offset,
                minic_parser_span_length(name_span),
                local_begin,
                parameter_count,
                body_block,
                &function_id) ||
            !minic_c0_program_set_function_parameter_count(
                parser->program,
                function_id,
                parameter_count)) {
            minic_parser_error(parser, "out of memory while adding function");
            return false;
        }
    } else if (!minic_c0_program_define_function(
                   parser->program,
                   function_id,
                   local_begin,
                   body_block)) {
        minic_parser_error(parser, "cannot define previously declared function");
        return false;
    }
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
    if (!minic_c0_program_finish_function(
            parser->program,
            function_id,
            local_count)) {
        minic_parser_error(parser, "invalid local range while finishing function");
        return false;
    }
    minic_parser_end_scope(parser);
    return true;
}

bool minic_parse_c0_program(
    const char *path,
    const char *source,
    size_t length,
    MinicC0Program *program,
    MinicDiagnostic *diagnostic)
{
    MinicParser parser;
    bool success;

    (void)memset(&parser, 0, sizeof(parser));
    parser.path = path;
    parser.source = source;
    parser.diagnostic = diagnostic;
    parser.program = program;
    parser.current_block = MINIC_BLOCK_INVALID;
    minic_lexer_initialize(&parser.lexer, path, source, length);

    success = minic_parser_advance(&parser);
    while (success && parser.current.kind != MINIC_TOKEN_EOF) {
        if (parser.current.kind == MINIC_TOKEN_KW_STRUCT) {
            success = minic_parser_parse_record_definition(&parser);
        } else {
            success = parse_function(&parser);
        }
    }
    if (success && program->entry_function == MINIC_FUNCTION_INVALID) {
        minic_parser_error(&parser, "translation unit requires an int main function");
        success = false;
    }

    minic_parser_destroy_scopes(&parser);
    return success;
}
