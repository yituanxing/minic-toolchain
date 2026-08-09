#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_core.c")
text = path.read_text()

marker = "static bool parse_array_bound_primary(MinicParser *parser, int64_t *value) {\n"
start = text.find(marker)
if start < 0 or text.find(marker, start + 1) >= 0:
    raise SystemExit("unexpected constant-expression primary marker")

helper = r'''static bool constant_expression_starts_integer_type_name(const MinicParser *parser) {
    MinicTypeAliasId alias_id;
    const MinicTypeAlias *alias;

    if (parser == NULL) {
        return false;
    }
    switch (parser->current.kind) {
    case MINIC_TOKEN_KW_CONST:
    case MINIC_TOKEN_KW_VOLATILE:
    case MINIC_TOKEN_KW_SIGNED:
    case MINIC_TOKEN_KW_UNSIGNED:
    case MINIC_TOKEN_KW_CHAR:
    case MINIC_TOKEN_KW_SHORT:
    case MINIC_TOKEN_KW_INT:
    case MINIC_TOKEN_KW_LONG:
        return true;
    case MINIC_TOKEN_IDENTIFIER:
        alias_id = minic_parser_find_type_alias(parser, parser->current.span);
        if (alias_id == MINIC_TYPE_ALIAS_INVALID) {
            return false;
        }
        alias = minic_c0_program_type_alias(parser->program, alias_id);
        return alias != NULL && minic_type_is_integer(alias->type);
    default:
        return false;
    }
}

static bool constant_expression_integer_cast(MinicParser *parser,
                                             MinicType target_type,
                                             int64_t operand,
                                             int64_t *value) {
    unsigned int width;
    uint64_t bits;
    uint64_t mask;

    if (parser == NULL || value == NULL || !minic_type_is_integer(target_type)) {
        return false;
    }
    switch (target_type.integer_rank) {
    case MINIC_INTEGER_RANK_CHAR:
        width = 8U;
        break;
    case MINIC_INTEGER_RANK_SHORT:
        width = 16U;
        break;
    case MINIC_INTEGER_RANK_INT:
        width = 32U;
        break;
    case MINIC_INTEGER_RANK_LONG:
    case MINIC_INTEGER_RANK_LONG_LONG:
        width = 64U;
        break;
    case MINIC_INTEGER_RANK_NONE:
        minic_parser_error(parser, "invalid integer cast in constant expression");
        return false;
    }

    bits = (uint64_t)operand;
    if (width < 64U) {
        mask = (UINT64_C(1) << width) - UINT64_C(1);
        bits &= mask;
        if (!minic_type_is_unsigned_integer(target_type) &&
            (bits & (UINT64_C(1) << (width - 1U))) != 0U) {
            bits |= ~mask;
        }
    }
    *value = (int64_t)bits;
    return true;
}

'''
text = text[:start] + helper + text[start:]

old = '''    if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        if (!minic_parser_advance(parser) || !parse_array_bound_additive(parser, value) ||
            !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' in array bound")) {
            return false;
        }
        return true;
    }
'''
new = '''    if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        MinicParser probe;

        probe = *parser;
        if (!minic_parser_advance(&probe)) {
            return false;
        }
        if (constant_expression_starts_integer_type_name(&probe)) {
            MinicType target_type;
            int64_t operand;

            if (!minic_parser_advance(parser) ||
                !minic_parser_parse_type_name(parser, &target_type) ||
                !minic_type_is_integer(target_type) ||
                !minic_parser_expect(parser, MINIC_TOKEN_RPAREN,
                                     "expected ')' after integer cast type") ||
                !parse_array_bound_unary(parser, &operand)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser,
                                       "constant expression cast requires an integer type");
                }
                return false;
            }
            return constant_expression_integer_cast(parser, target_type, operand, value);
        }
        if (!minic_parser_advance(parser) || !parse_array_bound_additive(parser, value) ||
            !minic_parser_expect(parser, MINIC_TOKEN_RPAREN,
                                 "expected ')' in integer constant expression")) {
            return false;
        }
        return true;
    }
'''
if text.count(old) != 1:
    raise SystemExit(f"unexpected parenthesized constant-expression block count={text.count(old)}")
path.write_text(text.replace(old, new, 1))
print("staged integer casts in shared constant-expression evaluator")
