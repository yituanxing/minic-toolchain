#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_core.c")
text = path.read_text()
old = '''static bool parse_array_bound_additive(MinicParser *parser, int64_t *value);

static bool parse_array_bound_primary(MinicParser *parser, int64_t *value) {
    if (parser == NULL || value == NULL) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_INTEGER_CONSTANT) {
        return minic_parser_parse_integer_value64(parser, value);
    }
'''
new = '''static bool parse_array_bound_additive(MinicParser *parser, int64_t *value);

static bool array_bound_type_size(const MinicC0Program *program, MinicType type, uint64_t *size) {
    if (program == NULL || size == NULL) {
        return false;
    }
    if (minic_type_is_pointer(type)) {
        *size = 8U;
        return true;
    }
    if (minic_type_is_integer(type)) {
        switch (type.integer_rank) {
        case MINIC_INTEGER_RANK_CHAR:
            *size = 1U;
            return true;
        case MINIC_INTEGER_RANK_SHORT:
            *size = 2U;
            return true;
        case MINIC_INTEGER_RANK_INT:
            *size = 4U;
            return true;
        case MINIC_INTEGER_RANK_LONG:
        case MINIC_INTEGER_RANK_LONG_LONG:
            *size = 8U;
            return true;
        case MINIC_INTEGER_RANK_NONE:
            return false;
        }
    }
    if (minic_type_is_float(type)) {
        *size = 4U;
        return true;
    }
    if (minic_type_is_double(type)) {
        *size = 8U;
        return true;
    }
    if (minic_type_is_array(type)) {
        const MinicArrayType *array_type;
        uint64_t element_size;

        array_type = minic_c0_program_array_type(program, type.array_type_id);
        if (array_type == NULL || array_type->element_count == 0U ||
            !array_bound_type_size(program, array_type->element_type, &element_size) ||
            element_size > UINT64_MAX / array_type->element_count) {
            return false;
        }
        *size = element_size * array_type->element_count;
        return true;
    }
    return false;
}

static bool parse_array_bound_sizeof(MinicParser *parser, int64_t *value) {
    MinicType measured_type;
    uint64_t measured_size;

    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_SIZEOF, "expected 'sizeof'") ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after sizeof") ||
        !minic_parser_parse_type_name(parser, &measured_type) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after sizeof type")) {
        return false;
    }
    if (!array_bound_type_size(parser->program, measured_type, &measured_size)) {
        minic_parser_error(parser, "unsupported sizeof type in array bound constant expression");
        return false;
    }
    if (measured_size > (uint64_t)INT64_MAX) {
        minic_parser_error(parser, "sizeof result exceeds array bound constant range");
        return false;
    }
    *value = (int64_t)measured_size;
    return true;
}

static bool parse_array_bound_primary(MinicParser *parser, int64_t *value) {
    if (parser == NULL || value == NULL) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_INTEGER_CONSTANT) {
        return minic_parser_parse_integer_value64(parser, value);
    }
    if (parser->current.kind == MINIC_TOKEN_KW_SIZEOF) {
        return parse_array_bound_sizeof(parser, value);
    }
'''
if text.count(old) != 1:
    raise SystemExit(f"unexpected array-bound primary prefix count={text.count(old)}")
path.write_text(text.replace(old, new, 1))
print("staged sizeof(type) folding in fixed array bounds")
