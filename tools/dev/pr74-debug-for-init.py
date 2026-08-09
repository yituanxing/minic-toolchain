#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_statement.c")
text = path.read_text()
old = '''    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else if (token_starts_local_declaration(parser)) {
'''
new = '''    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER &&
        minic_parser_span_length(parser->current.span) == 6U &&
        memcmp(parser->source + parser->current.span.begin.offset, "size_t", 6U) == 0) {
        MinicTypeAliasId debug_alias;
        size_t debug_index;
        size_t debug_raw_alias;

        debug_alias = minic_parser_find_type_alias(parser, parser->current.span);
        debug_raw_alias = SIZE_MAX;
        for (debug_index = 0U; debug_index < parser->program->type_alias_count; ++debug_index) {
            const MinicTypeAlias *debug_entry;

            debug_entry = minic_c0_program_type_alias(parser->program, debug_index);
            if (debug_entry != NULL && debug_entry->name_length == 6U &&
                memcmp(debug_entry->name, "size_t", 6U) == 0) {
                debug_raw_alias = debug_index;
                break;
            }
        }
        (void)fprintf(stderr,
                      "SDS_FOR_INIT_DEBUG line=%zu col=%zu scope=%zu bindings=%zu name_bound=%d alias=%zu raw_alias=%zu\\n",
                      parser->current.span.begin.line,
                      parser->current.span.begin.column,
                      parser->scope_count,
                      parser->local_binding_count,
                      minic_parser_name_bound(parser, parser->current.span) ? 1 : 0,
                      (size_t)debug_alias,
                      debug_raw_alias);
    }
    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else if (token_starts_local_declaration(parser)) {
'''
if text.count(old) != 1:
    raise SystemExit(f"expected one for-init classifier block, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))
