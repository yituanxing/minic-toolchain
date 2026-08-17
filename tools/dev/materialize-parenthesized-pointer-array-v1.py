#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / "src/frontend/parser_statement.c"
text = path.read_text()

anchor = '''static bool local_declarator_starts_function_pointer(const MinicParser *parser) {
    MinicParser probe;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_LPAREN) {
        return false;
    }
    probe = *parser;
    return minic_parser_advance(&probe) && probe.current.kind == MINIC_TOKEN_STAR;
}

static bool
parse_local_declarator(MinicParser *parser, MinicType base_type, bool is_register_storage) {
'''
replacement = '''static bool local_declarator_starts_parenthesized_pointer(const MinicParser *parser) {
    MinicParser probe;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_LPAREN) {
        return false;
    }
    probe = *parser;
    return minic_parser_advance(&probe) && probe.current.kind == MINIC_TOKEN_STAR;
}

static bool local_parenthesized_pointer_has_array_suffix(const MinicParser *parser) {
    MinicParser probe;
    size_t depth;

    if (!local_declarator_starts_parenthesized_pointer(parser)) {
        return false;
    }
    probe = *parser;
    depth = 0U;
    for (;;) {
        if (probe.current.kind == MINIC_TOKEN_LPAREN) {
            depth += 1U;
        } else if (probe.current.kind == MINIC_TOKEN_RPAREN) {
            if (depth == 0U) {
                return false;
            }
            depth -= 1U;
            if (depth == 0U) {
                return minic_parser_advance(&probe) &&
                       probe.current.kind == MINIC_TOKEN_LBRACKET;
            }
        }
        if (!minic_parser_advance(&probe)) {
            return false;
        }
    }
}

static bool parse_local_parenthesized_pointer_to_array(MinicParser *parser,
                                                       MinicType base_type,
                                                       MinicSourceSpan *name_span,
                                                       MinicType *declared_type) {
    unsigned int pointer_const_qualifiers;
    unsigned int pointer_volatile_qualifiers;
    MinicType type;
    size_t pointer_depth;
    size_t level;
    bool is_array;

    if (parser == NULL || name_span == NULL || declared_type == NULL ||
        !minic_parser_expect(parser,
                             MINIC_TOKEN_LPAREN,
                             "expected '(' before pointer-to-array declarator")) {
        return false;
    }
    pointer_depth = 0U;
    pointer_const_qualifiers = 0U;
    pointer_volatile_qualifiers = 0U;
    while (parser->current.kind == MINIC_TOKEN_STAR) {
        pointer_depth += 1U;
        if (pointer_depth > sizeof(unsigned int) * CHAR_BIT ||
            !minic_parser_advance(parser) ||
            !minic_parser_parse_pointer_qualifier_sequence(parser,
                                                           pointer_depth,
                                                           &pointer_const_qualifiers,
                                                           &pointer_volatile_qualifiers)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                minic_parser_error(parser, "invalid pointer-to-array indirection");
            }
            return false;
        }
    }
    if (pointer_depth == 0U || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected pointer-to-array declarator name");
        return false;
    }
    *name_span = parser->current.span;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser,
                             MINIC_TOKEN_RPAREN,
                             "expected ')' after pointer-to-array declarator") ||
        !minic_parser_parse_array_declarator_suffix(
            parser, base_type, true, &type, &is_array) ||
        !is_array) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
            minic_parser_error(parser, "pointer declarator requires an array suffix");
        }
        return false;
    }

    for (level = 0U; level < pointer_depth; ++level) {
        unsigned int bit;

        if (!minic_type_pointer_to(type, &type)) {
            minic_parser_error(parser, "cannot build pointer-to-array type");
            return false;
        }
        bit = 1U << level;
        if ((pointer_const_qualifiers & bit) != 0U &&
            !minic_type_add_const(type, &type)) {
            return false;
        }
        if ((pointer_volatile_qualifiers & bit) != 0U &&
            !minic_type_add_volatile(type, &type)) {
            return false;
        }
    }
    *declared_type = type;
    return true;
}

static bool
parse_local_declarator(MinicParser *parser, MinicType base_type, bool is_register_storage) {
'''
if text.count(anchor) != 1:
    raise SystemExit(f"local parenthesized classifier anchor count={text.count(anchor)}")
text = text.replace(anchor, replacement, 1)

old = '''    if (local_declarator_starts_function_pointer(parser)) {
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
'''
new = '''    if (local_parenthesized_pointer_has_array_suffix(parser)) {
        if (!parse_local_parenthesized_pointer_to_array(
                parser, declared_type, &local.name_span, &declared_type)) {
            return false;
        }
    } else if (local_declarator_starts_parenthesized_pointer(parser)) {
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
'''
if text.count(old) != 1:
    raise SystemExit(f"local function-pointer branch count={text.count(old)}")
path.write_text(text.replace(old, new, 1))
