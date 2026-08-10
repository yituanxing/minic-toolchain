#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:120]!r}")
    target.write_text(text.replace(old, new, 1))


# Turn the function-declaration pieces that were previously private to the
# translation-unit parser into shared declaration helpers. This is still a
# discovery-stage materialization, but it deliberately removes one source of
# parser-context duplication before adding the Linux block-scope form.
replace_once(
    "src/frontend/parser_function.c",
    "static bool function_signature_matches(const MinicFunction *function,\n",
    "bool minic_parser_function_signature_matches(const MinicFunction *function,\n",
)
replace_once(
    "src/frontend/parser_function.c",
    "!function_signature_matches(\n",
    "!minic_parser_function_signature_matches(\n",
)
path = Path("src/frontend/parser_function.c")
text = path.read_text()
text = text.replace("static bool parse_gnu_function_attributes(MinicParser *parser) {",
                    "bool minic_parser_parse_gnu_function_attributes(MinicParser *parser) {")
text = text.replace("return parse_gnu_function_attributes(parser);",
                    "return minic_parser_parse_gnu_function_attributes(parser);")
text = text.replace("!parse_gnu_function_attributes(parser)",
                    "!minic_parser_parse_gnu_function_attributes(parser)")
text = text.replace("static bool parse_gnu_prefix_function_attributes(MinicParser *parser,",
                    "bool minic_parser_parse_gnu_prefix_function_attributes(MinicParser *parser,")
text = text.replace("!parse_gnu_prefix_function_attributes(parser,",
                    "!minic_parser_parse_gnu_prefix_function_attributes(parser,")
text = text.replace("!parse_gnu_prefix_function_attributes(&probe,",
                    "!minic_parser_parse_gnu_prefix_function_attributes(&probe,")
path.write_text(text)

# GCC's error/warning attributes are compile-time diagnostic attributes, not
# ABI/layout metadata. Parse them explicitly in the suffix attribute path rather
# than silently treating every unknown attribute as harmless. Their call-site
# diagnostic semantics remain a later AttributeSet/Sema consumer; this gate
# first preserves the unchanged Linux declaration and its identity.
path = Path("src/frontend/parser_function.c")
text = path.read_text()
anchor = '''static bool parse_gnu_attribute_arguments(MinicParser *parser) {\n'''
helper = r'''static bool gnu_function_attribute_is_diagnostic(const MinicParser *parser) {
    return function_identifier_is(parser, "error") ||
           function_identifier_is(parser, "__error__") ||
           function_identifier_is(parser, "warning") ||
           function_identifier_is(parser, "__warning__");
}

'''
if text.count(anchor) != 1:
    raise SystemExit(f"diagnostic attribute anchor: expected one match, found {text.count(anchor)}")
text = text.replace(anchor, helper + anchor, 1)
old = '''            if (!gnu_function_attribute_is_metadata(parser)) {
                minic_parser_error(parser,
                                   "unsupported GNU function attribute; ABI/layout-affecting and "
                                   "unknown attributes must be implemented explicitly");
'''
new = '''            if (!gnu_function_attribute_is_metadata(parser) &&
                !gnu_function_attribute_is_diagnostic(parser)) {
                minic_parser_error(parser,
                                   "unsupported GNU function attribute; ABI/layout-affecting and "
                                   "unknown attributes must be implemented explicitly");
'''
if text.count(old) != 1:
    raise SystemExit(f"suffix attribute classification anchor: expected one match, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))

# Publish the shared declaration helpers to statement parsing. Permanent
# materialization will move these behind the Declarator/Attribute interfaces;
# the discovery branch should already have one semantic implementation.
path = Path("src/frontend/parser_internal.h")
text = path.read_text()
anchor = '''bool minic_parser_parse_parameter_list(MinicParser *parser,
                                       MinicSourceSpan *parameter_name_spans,
                                       MinicType *parameter_types,
                                       size_t *parameter_count,
                                       bool require_names,
                                       bool *is_variadic);
'''
prototypes = '''bool minic_parser_function_signature_matches(const MinicFunction *function,
                                             MinicType return_type,
                                             const MinicType *parameter_types,
                                             size_t parameter_count,
                                             bool is_variadic);
bool minic_parser_parse_gnu_function_attributes(MinicParser *parser);
bool minic_parser_parse_gnu_prefix_function_attributes(MinicParser *parser,
                                                       bool is_internal,
                                                       bool is_inline);
'''
if text.count(anchor) != 1:
    raise SystemExit(f"parser internal prototype anchor: expected one match, found {text.count(anchor)}")
path.write_text(text.replace(anchor, anchor + prototypes, 1))

# Add the C block-scope extern function-declaration form used by Linux's
# compile-time assertion machinery. It deliberately creates/redeclares the same
# Program-owned function symbol used by file-scope declarations, so direct calls
# and function designators resolve through the existing semantic path.
path = Path("src/frontend/parser_statement.c")
text = path.read_text()
anchor = '''static bool parse_declaration(MinicParser *parser) {
    MinicType base_type;

    if (!minic_parser_parse_type_specifiers(parser, &base_type)) {
        return false;
    }

    for (;;) {
        if (!parse_local_declarator(parser, base_type)) {
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

'''
helper = r'''static bool parse_block_scope_extern_function_declaration(MinicParser *parser) {
    MinicSourceSpan name_span;
    MinicType parameter_types[16];
    MinicType return_type;
    MinicFunctionId function_id;
    const MinicFunction *existing_function;
    size_t parameter_count;
    bool is_variadic;

    parameter_count = 0U;
    is_variadic = false;
    (void)memset(parameter_types, 0, sizeof(parameter_types));

    if (!minic_parser_parse_gnu_prefix_function_attributes(parser, false, false) ||
        !minic_parser_expect(parser, MINIC_TOKEN_KW_EXTERN, "expected keyword 'extern'") ||
        !minic_parser_parse_type_name(parser, &return_type) ||
        !minic_parser_parse_gnu_function_attributes(parser)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        name_span = parser->current.span;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
            minic_parser_error(parser, "expected function name in block-scope extern declarator");
            return false;
        }
        name_span = parser->current.span;
        if (!minic_parser_advance(parser) ||
            !minic_parser_expect(parser,
                                 MINIC_TOKEN_RPAREN,
                                 "expected ')' after block-scope extern function name")) {
            return false;
        }
    } else {
        minic_parser_error(parser, "expected block-scope extern function name");
        return false;
    }

    if (!minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '('") ||
        !minic_parser_parse_parameter_list(
            parser, NULL, parameter_types, &parameter_count, false, &is_variadic) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')'") ||
        !minic_parser_parse_gnu_function_attributes(parser)) {
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_SEMICOLON) {
        minic_parser_error(parser,
                           "block-scope extern declaration must declare a function and end with ';'");
        return false;
    }

    function_id = minic_parser_find_function(parser, name_span);
    if (function_id != MINIC_FUNCTION_INVALID) {
        existing_function = minic_c0_program_function(parser->program, function_id);
        if (!minic_parser_function_signature_matches(
                existing_function, return_type, parameter_types, parameter_count, is_variadic)) {
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
               !minic_c0_program_set_function_variadic(
                   parser->program, function_id, is_variadic)) {
        minic_parser_error(parser, "out of memory while declaring block-scope extern function");
        return false;
    }

    return minic_parser_advance(parser);
}

'''
if text.count(anchor) != 1:
    raise SystemExit(f"block declaration anchor: expected one match, found {text.count(anchor)}")
text = text.replace(anchor, anchor + helper, 1)

old = '''    if (parser->current.kind == MINIC_TOKEN_KW_STATIC) {
        if (!allow_declaration) {
            minic_parser_error(parser, "a declaration requires a compound statement scope");
            return false;
        }
        return parse_static_local_declaration(parser);
    }
'''
new = '''    if (parser->current.kind == MINIC_TOKEN_KW_EXTERN ||
        (parser->current.kind == MINIC_TOKEN_IDENTIFIER &&
         identifier_equals(parser, parser->current.span, "__attribute__", 13U))) {
        if (!allow_declaration) {
            minic_parser_error(parser, "a declaration requires a compound statement scope");
            return false;
        }
        return parse_block_scope_extern_function_declaration(parser);
    }
    if (parser->current.kind == MINIC_TOKEN_KW_STATIC) {
        if (!allow_declaration) {
            minic_parser_error(parser, "a declaration requires a compound statement scope");
            return false;
        }
        return parse_static_local_declaration(parser);
    }
'''
if text.count(old) != 1:
    raise SystemExit(f"statement dispatch anchor: expected one match, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))

print("staged shared GNU function-attribute parsing and block-scope attributed extern function declarations")
