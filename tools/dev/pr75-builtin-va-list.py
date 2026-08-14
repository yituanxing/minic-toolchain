#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_type.c")
text = path.read_text()

old_include = '''#include "frontend/parser_internal.h"\n'''
new_include = '''#include "frontend/parser_internal.h"\n\n#include <string.h>\n'''
if text.count(old_include) != 1:
    raise SystemExit(f"parser_type include anchor: expected 1 match, found {text.count(old_include)}")
text = text.replace(old_include, new_include, 1)

anchor = '''static bool minic_parser_is_integer_type_specifier(MinicTokenKind kind) {\n'''
helper = '''static bool minic_parser_identifier_is(const MinicParser *parser, const char *name) {
    size_t name_length;

    if (parser == NULL || name == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        return false;
    }
    name_length = strlen(name);
    return minic_parser_span_length(parser->current.span) == name_length &&
           memcmp(parser->source + parser->current.span.begin.offset, name, name_length) == 0;
}

'''
if text.count(anchor) != 1:
    raise SystemExit(f"parser_type helper anchor: expected 1 match, found {text.count(anchor)}")
text = text.replace(anchor, helper + anchor, 1)

old = '''    } else if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        MinicTypeAliasId alias_id;
        const MinicTypeAlias *alias;

        alias_id = minic_parser_find_type_alias(parser, parser->current.span);
        alias = minic_c0_program_type_alias(parser->program, alias_id);
        if (alias == NULL) {
            minic_parser_error(parser, "expected type name");
            return false;
        }
        parsed_type = alias->type;
        if (!minic_parser_advance(parser)) {
            return false;
        }
'''
new = '''    } else if (minic_parser_identifier_is(parser, "__builtin_va_list")) {
        if (!minic_type_pointer_to(minic_type_void(), &parsed_type) || !minic_parser_advance(parser)) {
            minic_parser_error(parser, "cannot build __builtin_va_list type");
            return false;
        }
    } else if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        MinicTypeAliasId alias_id;
        const MinicTypeAlias *alias;

        alias_id = minic_parser_find_type_alias(parser, parser->current.span);
        alias = minic_c0_program_type_alias(parser->program, alias_id);
        if (alias == NULL) {
            minic_parser_error(parser, "expected type name");
            return false;
        }
        parsed_type = alias->type;
        if (!minic_parser_advance(parser)) {
            return false;
        }
'''
if text.count(old) != 1:
    raise SystemExit(f"builtin va_list type anchor: expected 1 match, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))
print("staged __builtin_va_list as the RV64 GCC default void-pointer builtin type")
