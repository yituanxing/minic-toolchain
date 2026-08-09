#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_record.c")
text = path.read_text()
old = r'''    element_count = 1U;
    is_flexible_array = false;
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        if (minic_type_is_pointer(field_type) && field_type.base_kind == MINIC_TYPE_BASE_FUNCTION) {
            minic_parser_error(parser, "function pointer field arrays are unsupported");
            return false;
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_RBRACKET) {
            if (record->is_union) {
                minic_parser_error(parser, "flexible array member is not allowed in union");
                return false;
            }
            if (record->field_count == 0U) {
                minic_parser_error(parser,
                                   "flexible array member requires a preceding named field");
                return false;
            }
            is_flexible_array = true;
            if (!minic_parser_advance(parser)) {
                return false;
            }
        } else if (!minic_parser_parse_fixed_array_bound(parser, &element_count)) {
            return false;
        }
    }

    if (!minic_c0_record_add_field(parser->program,
'''
new = r'''    element_count = 1U;
    is_flexible_array = false;
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        size_t bounds[8];
        size_t bound_count;

        if (minic_type_is_pointer(field_type) && field_type.base_kind == MINIC_TYPE_BASE_FUNCTION) {
            minic_parser_error(parser, "function pointer field arrays are unsupported");
            return false;
        }
        bound_count = 0U;
        while (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            if (bound_count >= sizeof(bounds) / sizeof(bounds[0])) {
                minic_parser_error(parser, "record field supports at most eight array dimensions");
                return false;
            }
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACKET) {
                if (bound_count != 0U) {
                    minic_parser_error(parser,
                                       "only the outermost record array dimension may be flexible");
                    return false;
                }
                if (record->is_union) {
                    minic_parser_error(parser, "flexible array member is not allowed in union");
                    return false;
                }
                if (record->field_count == 0U) {
                    minic_parser_error(parser,
                                       "flexible array member requires a preceding named field");
                    return false;
                }
                is_flexible_array = true;
                if (!minic_parser_advance(parser)) {
                    return false;
                }
                if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
                    minic_parser_error(parser,
                                       "multidimensional flexible record arrays are unsupported");
                    return false;
                }
                break;
            }
            if (!minic_parser_parse_fixed_array_bound(parser, &bounds[bound_count])) {
                return false;
            }
            bound_count += 1U;
        }

        if (!is_flexible_array && bound_count != 0U) {
            size_t dimension;

            element_count = bounds[0];
            dimension = bound_count;
            while (dimension > 1U) {
                dimension -= 1U;
                if (!minic_c0_program_add_array_type(
                        parser->program, field_type, bounds[dimension], &field_type)) {
                    minic_parser_error(parser,
                                       "cannot build multidimensional record array type");
                    return false;
                }
            }
        }
    }

    if (!minic_c0_record_add_field(parser->program,
'''
if text.count(old) != 1:
    raise SystemExit(f"unexpected staged record-array block count={text.count(old)}")
path.write_text(text.replace(old, new, 1))
print("staged multidimensional record field arrays")
