#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    target.write_text(text.replace(old, new, 1))


# Share type-name lookahead and a no-decay expression entry point between parser modules.
replace_once(
    "src/frontend/parser_internal.h",
    """bool minic_parser_parse_type_specifiers(MinicParser *parser, MinicType *type);
""",
    """bool minic_parser_token_starts_type_name(const MinicParser *parser, MinicToken token);
bool minic_parser_parse_type_specifiers(MinicParser *parser, MinicType *type);
""",
    "type-name-lookahead-prototype",
)
replace_once(
    "src/frontend/parser_internal.h",
    """bool minic_parser_parse_expression(MinicParser *parser,
                                   MinicExpressionId *expression_id,
                                   unsigned int minimum_precedence);
""",
    """bool minic_parser_parse_expression(MinicParser *parser,
                                   MinicExpressionId *expression_id,
                                   unsigned int minimum_precedence);
bool minic_parser_parse_expression_no_decay(MinicParser *parser,
                                            MinicExpressionId *expression_id);
""",
    "no-decay-expression-prototype",
)

# parser_type.c already has <string.h> after GNU int128 staging. Add token text helpers and
# central type-name lookahead there so casts, sizeof and typeof do not grow separate lists.
path = Path("src/frontend/parser_type.c")
text = path.read_text()
helper_anchor = "static bool minic_parser_gnu_int128_name(const MinicParser *parser, bool *is_unsigned_name) {\n"
helper = r'''static bool minic_parser_token_text_equals(const MinicParser *parser,
                                           MinicToken token,
                                           const char *text) {
    size_t length;

    if (parser == NULL || text == NULL || token.kind != MINIC_TOKEN_IDENTIFIER) {
        return false;
    }
    length = minic_parser_span_length(token.span);
    return strlen(text) == length &&
           memcmp(parser->source + token.span.begin.offset, text, length) == 0;
}

static bool minic_parser_token_is_gnu_typeof(const MinicParser *parser, MinicToken token) {
    return minic_parser_token_text_equals(parser, token, "typeof") ||
           minic_parser_token_text_equals(parser, token, "__typeof") ||
           minic_parser_token_text_equals(parser, token, "__typeof__");
}

'''
if text.count(helper_anchor) != 1:
    raise SystemExit(f"parser type helper anchor: expected one match, found {text.count(helper_anchor)}")
text = text.replace(helper_anchor, helper + helper_anchor, 1)

# Insert the shared lookahead after the GNU-int128-name helper; it recognizes int128 aliases,
# typeof, typedef names, and all currently supported builtin/record type tokens.
marker = "static bool minic_parser_try_gnu_int128(MinicParser *parser,\n"
lookahead = r'''bool minic_parser_token_starts_type_name(const MinicParser *parser, MinicToken token) {
    bool int128_unsigned;
    MinicParser probe;

    switch (token.kind) {
    case MINIC_TOKEN_KW_CONST:
    case MINIC_TOKEN_KW_VOLATILE:
    case MINIC_TOKEN_KW_BOOL:
    case MINIC_TOKEN_KW_CHAR:
    case MINIC_TOKEN_KW_INT:
    case MINIC_TOKEN_KW_LONG:
    case MINIC_TOKEN_KW_SIGNED:
    case MINIC_TOKEN_KW_UNSIGNED:
    case MINIC_TOKEN_KW_FLOAT:
    case MINIC_TOKEN_KW_DOUBLE:
    case MINIC_TOKEN_KW_VOID:
    case MINIC_TOKEN_KW_STRUCT:
        return true;
    case MINIC_TOKEN_IDENTIFIER:
        if (parser == NULL) {
            return false;
        }
        if (minic_parser_token_is_gnu_typeof(parser, token)) {
            return true;
        }
        probe = *parser;
        probe.current = token;
        int128_unsigned = false;
        if (minic_parser_gnu_int128_name(&probe, &int128_unsigned)) {
            return true;
        }
        return minic_parser_find_local(parser, token.span) == MINIC_LOCAL_INVALID &&
               minic_parser_find_type_alias(parser, token.span) != MINIC_TYPE_ALIAS_INVALID;
    default:
        return false;
    }
}

'''
if text.count(marker) != 1:
    raise SystemExit(f"int128 parser helper marker: expected one match, found {text.count(marker)}")
text = text.replace(marker, lookahead + marker, 1)

# GNU typeof is a type specifier. Parse either a type-name or an unevaluated expression.
# The no-decay expression entry preserves array/function/lvalue type identity for typeof.
parse_start = text.find("bool minic_parser_parse_type_specifiers(MinicParser *parser, MinicType *type) {")
parse_end = text.find("\nbool minic_parser_parse_pointer_declarator", parse_start)
if parse_start < 0 or parse_end < 0:
    raise SystemExit("cannot locate parse_type_specifiers for typeof")
body = text[parse_start:parse_end]
insert_anchor = """    if (parser->current.kind == MINIC_TOKEN_KW_BOOL) {
"""
typeof_arm = r'''    if (minic_parser_token_is_gnu_typeof(parser, parser->current)) {
        MinicSourceSpan typeof_span = parser->current.span;

        if (!minic_parser_advance(parser) ||
            !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after GNU typeof")) {
            return false;
        }
        if (minic_parser_token_starts_type_name(parser, parser->current)) {
            if (!minic_parser_parse_type_name(parser, &parsed_type)) {
                return false;
            }
        } else {
            MinicExpressionId operand_id;
            const MinicExpression *operand;

            if (!minic_parser_parse_expression_no_decay(parser, &operand_id)) {
                return false;
            }
            operand = minic_c0_program_expression(parser->program, operand_id);
            if (operand == NULL) {
                minic_parser_error(parser, "invalid GNU typeof expression operand");
                return false;
            }
            parsed_type = operand->type;
        }
        if (!minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after GNU typeof operand")) {
            return false;
        }
        (void)typeof_span;
        goto parsed_type_specifiers_done;
    }

'''
if body.count(insert_anchor) != 1:
    raise SystemExit(f"typeof insertion anchor: expected one match, found {body.count(insert_anchor)}")
body = body.replace(insert_anchor, typeof_arm + insert_anchor, 1)
text = text[:parse_start] + body + text[parse_end:]
path.write_text(text)

# parser_expression.c: replace private cast-type list with the shared type-name predicate,
# then add C11 _Generic as a primary expression that lowers immediately to the selected arm.
path = Path("src/frontend/parser_expression.c")
text = path.read_text()
start = text.find("static bool token_starts_cast_type(")
end = text.find("\nstatic bool parenthesis_starts_cast", start)
if start < 0 or end < 0:
    raise SystemExit("cannot locate cast type lookahead")
replacement = r'''static bool token_starts_cast_type(const MinicParser *parser, MinicToken token) {
    return minic_parser_token_starts_type_name(parser, token);
}
'''
text = text[:start] + replacement + text[end:]

primary_marker = "static bool parse_primary(MinicParser *parser, MinicExpressionId *expression_id, bool decay_array) {\n"
generic_helper = r'''static bool generic_token_text_equals(const MinicParser *parser, const char *text) {
    size_t length;

    if (parser == NULL || text == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        return false;
    }
    length = minic_parser_span_length(parser->current.span);
    return strlen(text) == length &&
           memcmp(parser->source + parser->current.span.begin.offset, text, length) == 0;
}

static bool generic_types_compatible(MinicType left, MinicType right) {
    MinicType left_unqualified;
    MinicType right_unqualified;

    if (!minic_type_unqualified(left, &left_unqualified) ||
        !minic_type_unqualified(right, &right_unqualified)) {
        return minic_type_equal(left, right);
    }
    return minic_type_equal(left_unqualified, right_unqualified);
}

static bool parse_generic_selection(MinicParser *parser,
                                    MinicExpressionId *expression_id,
                                    bool decay_array) {
    MinicExpressionId controlling_id;
    const MinicExpression *controlling;
    MinicExpressionId selected_id = MINIC_EXPRESSION_INVALID;
    MinicExpressionId default_id = MINIC_EXPRESSION_INVALID;
    MinicType controlling_type;
    bool saw_matching_type = false;
    bool saw_default = false;

    if (!generic_token_text_equals(parser, "_Generic") ||
        !minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after _Generic") ||
        !parse_expression_internal(parser, &controlling_id, 0U, true)) {
        return false;
    }
    controlling = minic_c0_program_expression(parser->program, controlling_id);
    if (controlling == NULL) {
        minic_parser_error(parser, "invalid _Generic controlling expression");
        return false;
    }
    controlling_type = controlling->type;
    if (!minic_parser_expect(parser, MINIC_TOKEN_COMMA, "expected ',' after _Generic controlling expression")) {
        return false;
    }

    for (;;) {
        bool is_default = parser->current.kind == MINIC_TOKEN_KW_DEFAULT;
        MinicType association_type = minic_type_void();
        MinicExpressionId association_id;

        if (is_default) {
            if (saw_default) {
                minic_parser_error(parser, "duplicate default association in _Generic");
                return false;
            }
            saw_default = true;
            if (!minic_parser_advance(parser)) {
                return false;
            }
        } else if (!minic_parser_parse_type_name(parser, &association_type)) {
            return false;
        }
        if (!minic_parser_expect(parser, MINIC_TOKEN_COLON, "expected ':' in _Generic association") ||
            !parse_expression_internal(parser, &association_id, 0U, decay_array)) {
            return false;
        }

        if (is_default) {
            default_id = association_id;
        } else if (generic_types_compatible(controlling_type, association_type)) {
            if (saw_matching_type) {
                minic_parser_error(parser, "multiple compatible type associations in _Generic");
                return false;
            }
            saw_matching_type = true;
            selected_id = association_id;
        }

        if (parser->current.kind != MINIC_TOKEN_COMMA) {
            break;
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after _Generic associations")) {
        return false;
    }
    if (!saw_matching_type) {
        selected_id = default_id;
    }
    if (selected_id == MINIC_EXPRESSION_INVALID) {
        minic_parser_error(parser, "no compatible association and no default in _Generic");
        return false;
    }
    *expression_id = selected_id;
    return true;
}

'''
if text.count(primary_marker) != 1:
    raise SystemExit(f"primary parser marker: expected one match, found {text.count(primary_marker)}")
text = text.replace(primary_marker, generic_helper + primary_marker, 1)

primary_entry = """    if (parser->current.kind == MINIC_TOKEN_INTEGER_CONSTANT ||
"""
primary_replacement = """    if (generic_token_text_equals(parser, "_Generic")) {
        if (!parse_generic_selection(parser, &primary_id, decay_array) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (parser->current.kind == MINIC_TOKEN_INTEGER_CONSTANT ||
"""
if text.count(primary_entry) != 1:
    raise SystemExit(f"generic primary entry: expected one match, found {text.count(primary_entry)}")
text = text.replace(primary_entry, primary_replacement, 1)

# Public internal no-decay entry is intentionally tiny: it exposes the parser's existing
# typed expression machinery without creating a second grammar for typeof.
public_marker = """bool minic_parser_parse_expression(MinicParser *parser,
                                   MinicExpressionId *expression_id,
                                   unsigned int minimum_precedence) {
"""
no_decay = r'''bool minic_parser_parse_expression_no_decay(MinicParser *parser,
                                            MinicExpressionId *expression_id) {
    if (parser == NULL || expression_id == NULL) {
        return false;
    }
    return parse_expression_internal(parser, expression_id, 0U, false);
}

'''
if text.count(public_marker) != 1:
    raise SystemExit(f"public expression parser marker: expected one match, found {text.count(public_marker)}")
text = text.replace(public_marker, no_decay + public_marker, 1)
path.write_text(text)

print("staged GNU typeof(type/expression) and C11 _Generic with frontend selection lowering")
