#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_core.c")
text = path.read_text()
old = '''static bool parse_array_bound_additive(MinicParser *parser, int64_t *value);
'''
new = '''static bool parse_array_bound_additive(MinicParser *parser, int64_t *value);
static bool parse_array_bound_unary(MinicParser *parser, int64_t *value);
'''
if text.count(old) != 1:
    raise SystemExit(f"unexpected array-bound forward declaration count={text.count(old)}")
text = text.replace(old, new, 1)

marker = '''static bool parse_array_bound_primary(MinicParser *parser, int64_t *value) {
'''
helper = r'''static bool array_bound_parenthesis_starts_integer_cast(MinicParser *parser) {
    MinicParser probe;
    MinicTypeAliasId alias_id;
    const MinicTypeAlias *alias;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_LPAREN) {
        return false;
    }
    probe = *parser;
    if (!minic_parser_advance(&probe)) {
        return false;
    }
    switch (probe.current.kind) {
    case MINIC_TOKEN_KW_CONST:
    case MINIC_TOKEN_KW_VOLATILE:
    case MINIC_TOKEN_KW_CHAR:
    case MINIC_TOKEN_KW_SHORT:
    case MINIC_TOKEN_KW_INT:
    case MINIC_TOKEN_KW_LONG:
    case MINIC_TOKEN_KW_SIGNED:
    case MINIC_TOKEN_KW_UNSIGNED:
        return true;
    case MINIC_TOKEN_IDENTIFIER:
        alias_id = minic_parser_find_type_alias(parser, probe.current.span);
        alias = minic_c0_program_type_alias(parser->program, alias_id);
        return alias != NULL && minic_type_is_integer(alias->type);
    default:
        return false;
    }
}

static bool array_bound_apply_integer_cast(MinicParser *parser,
                                           MinicType type,
                                           int64_t operand,
                                           int64_t *value) {
    unsigned int bits;
    uint64_t raw;
    uint64_t mask;
    bool is_unsigned;

    if (parser == NULL || value == NULL || !minic_type_is_integer(type)) {
        if (parser != NULL) {
            minic_parser_error(parser, "array bound cast requires an integer type");
        }
        return false;
    }
    switch (type.integer_rank) {
    case MINIC_INTEGER_RANK_CHAR:
        bits = 8U;
        break;
    case MINIC_INTEGER_RANK_SHORT:
        bits = 16U;
        break;
    case MINIC_INTEGER_RANK_INT:
        bits = 32U;
        break;
    case MINIC_INTEGER_RANK_LONG:
    case MINIC_INTEGER_RANK_LONG_LONG:
        bits = 64U;
        break;
    case MINIC_INTEGER_RANK_NONE:
        minic_parser_error(parser, "invalid integer rank in array bound cast");
        return false;
    }
    raw = (uint64_t)operand;
    is_unsigned = minic_type_is_unsigned_integer(type);
    if (bits == 64U) {
        *value = (int64_t)raw;
        return true;
    }
    mask = (UINT64_C(1) << bits) - UINT64_C(1);
    raw &= mask;
    if (!is_unsigned && (raw & (UINT64_C(1) << (bits - 1U))) != 0U) {
        raw |= ~mask;
    }
    *value = (int64_t)raw;
    return true;
}

static bool parse_array_bound_cast(MinicParser *parser, int64_t *value) {
    MinicType cast_type;
    int64_t operand;

    if (!minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' before array bound cast") ||
        !minic_parser_parse_type_name(parser, &cast_type) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after array bound cast type") ||
        !parse_array_bound_unary(parser, &operand)) {
        return false;
    }
    return array_bound_apply_integer_cast(parser, cast_type, operand, value);
}

'''
if text.count(marker) != 1:
    raise SystemExit("cannot locate array-bound primary")
text = text.replace(marker, helper + marker, 1)

old_group = '''    if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        if (!minic_parser_advance(parser) || !parse_array_bound_additive(parser, value) ||
            !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' in array bound")) {
            return false;
        }
        return true;
    }
'''
new_group = '''    if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        if (array_bound_parenthesis_starts_integer_cast(parser)) {
            return parse_array_bound_cast(parser, value);
        }
        if (!minic_parser_advance(parser) || !parse_array_bound_additive(parser, value) ||
            !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' in array bound")) {
            return false;
        }
        return true;
    }
'''
if text.count(old_group) != 1:
    raise SystemExit(f"unexpected array-bound parenthesis block count={text.count(old_group)}")
path.write_text(text.replace(old_group, new_group, 1))
print("staged integer cast constant expressions in fixed array bounds")
