#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one replacement, found {count}")
    target.write_text(text.replace(old, new, 1))


# Preserve storage-class semantics in the local symbol rather than treating
# `register` as whitespace. The current RV64 backend may still spill/register-
# allocate it like any local; Sema can later enforce address-taking restrictions
# from this explicit metadata.
replace_once(
    "src/frontend/ast.h",
    "    bool is_array;\n} MinicLocal;\n",
    "    bool is_array;\n    bool is_register_storage;\n} MinicLocal;\n",
    "register-local-metadata",
)

# Declaration-start recognition and storage-class consumption share one parser
# type/declaration helper. `register` remains an identifier token during staged
# discovery so we do not force an immediate lexer keyword-table migration.
replace_once(
    "src/frontend/parser_internal.h",
    "bool minic_parser_token_starts_type_name(const MinicParser *parser, MinicToken token);\n",
    """bool minic_parser_token_starts_type_name(const MinicParser *parser, MinicToken token);
bool minic_parser_token_starts_declaration_specifiers(const MinicParser *parser,
                                                       MinicToken token);
bool minic_parser_parse_local_storage_class(MinicParser *parser, bool *is_register_storage);
""",
    "register-declaration-helper-prototypes",
)

path = Path("src/frontend/parser_type.c")
text = path.read_text()
if '#include <string.h>\n' not in text:
    text = text.replace('#include "frontend/parser_internal.h"\n',
                        '#include "frontend/parser_internal.h"\n\n#include <string.h>\n', 1)
marker = "static bool minic_parser_is_integer_type_specifier(MinicTokenKind kind) {\n"
helper = r'''static bool declaration_identifier_is(const MinicParser *parser,
                                      MinicToken token,
                                      const char *name) {
    size_t length;

    if (parser == NULL || name == NULL || token.kind != MINIC_TOKEN_IDENTIFIER ||
        token.span.end.offset < token.span.begin.offset) {
        return false;
    }
    length = token.span.end.offset - token.span.begin.offset;
    return strlen(name) == length &&
           memcmp(parser->source + token.span.begin.offset, name, length) == 0;
}

bool minic_parser_token_starts_declaration_specifiers(const MinicParser *parser,
                                                       MinicToken token) {
    return minic_parser_token_starts_type_name(parser, token) ||
           declaration_identifier_is(parser, token, "register") ||
           declaration_identifier_is(parser, token, "auto");
}

bool minic_parser_parse_local_storage_class(MinicParser *parser, bool *is_register_storage) {
    bool saw_storage_class;

    if (parser == NULL || is_register_storage == NULL) {
        return false;
    }
    *is_register_storage = false;
    saw_storage_class = false;
    while (declaration_identifier_is(parser, parser->current, "register") ||
           declaration_identifier_is(parser, parser->current, "auto")) {
        bool is_register;

        if (saw_storage_class) {
            minic_parser_error(parser, "multiple storage-class specifiers in local declaration");
            return false;
        }
        is_register = declaration_identifier_is(parser, parser->current, "register");
        saw_storage_class = true;
        *is_register_storage = is_register;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    return true;
}

'''
if text.count(marker) != 1:
    raise SystemExit(f"register declaration helper: expected one parser_type marker, found {text.count(marker)}")
path.write_text(text.replace(marker, helper + marker, 1))

# Route statement declaration lookahead through the shared declaration-specifier
# predicate and thread the storage class through the existing local declarator.
path = Path("src/frontend/parser_statement.c")
text = path.read_text()
old = r'''static bool token_starts_local_declaration(const MinicParser *parser) {
    return parser != NULL && minic_parser_token_starts_type_name(parser, parser->current);
}
'''
new = r'''static bool token_starts_local_declaration(const MinicParser *parser) {
    return parser != NULL &&
           minic_parser_token_starts_declaration_specifiers(parser, parser->current);
}
'''
if text.count(old) != 1:
    raise SystemExit(f"register local lookahead: expected shared lookahead shape, found {text.count(old)}")
text = text.replace(old, new, 1)
old = "static bool parse_local_declarator(MinicParser *parser, MinicType base_type) {\n"
new = "static bool parse_local_declarator(MinicParser *parser, MinicType base_type, bool is_register_storage) {\n"
if text.count(old) != 1:
    raise SystemExit(f"register local declarator signature: expected one, found {text.count(old)}")
text = text.replace(old, new, 1)
old = "    local.is_array = false;\n"
new = "    local.is_array = false;\n    local.is_register_storage = is_register_storage;\n"
if text.count(old) != 1:
    raise SystemExit(f"register local metadata assignment: expected one normal-local anchor, found {text.count(old)}")
text = text.replace(old, new, 1)
old = r'''static bool parse_declaration(MinicParser *parser) {
    MinicType base_type;

    if (!minic_parser_parse_type_specifiers(parser, &base_type)) {
        return false;
    }

    for (;;) {
        if (!parse_local_declarator(parser, base_type)) {
            return false;
        }
'''
new = r'''static bool parse_declaration(MinicParser *parser) {
    MinicType base_type;
    bool is_register_storage;

    if (!minic_parser_parse_local_storage_class(parser, &is_register_storage) ||
        !minic_parser_parse_type_specifiers(parser, &base_type)) {
        return false;
    }

    for (;;) {
        if (!parse_local_declarator(parser, base_type, is_register_storage)) {
            return false;
        }
'''
if text.count(old) != 1:
    raise SystemExit(f"register declaration parser: expected one declaration block, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))

print("staged local register/auto storage-class declaration dispatch with explicit register metadata")
