#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_statement.c")
text = path.read_text()

marker = "static bool parse_local_declarator(MinicParser *parser, MinicType base_type) {\n"
if text.count(marker) != 1:
    raise SystemExit("unexpected local declarator marker")
prototype = r'''static bool add_record_copy_assignments(MinicParser *parser,
                                        MinicExpressionId target_id,
                                        MinicExpressionId source_id,
                                        MinicSourceSpan span);

'''
text = text.replace(marker, prototype + marker, 1)

old = r'''        if (minic_type_is_record(local.type)) {
            MinicExpressionId target_id;
            MinicSourceSpan initializer_span;

            if (!add_local_lvalue_expression(parser, local_id, local.name_span, &target_id) ||
                !minic_parser_advance(parser) ||
                !parse_zero_aggregate_initializer(parser, &initializer_span) ||
                !add_zero_initialized_record_lvalue(parser, target_id, initializer_span)) {
                return false;
            }
            return true;
        }
'''
new = r'''        if (minic_type_is_record(local.type)) {
            MinicExpressionId target_id;

            if (!add_local_lvalue_expression(parser, local_id, local.name_span, &target_id) ||
                !minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_LBRACE) {
                MinicSourceSpan initializer_span;

                return parse_zero_aggregate_initializer(parser, &initializer_span) &&
                       add_zero_initialized_record_lvalue(parser, target_id, initializer_span);
            } else {
                MinicExpressionId source_id;
                const MinicExpression *source;

                if (!minic_parser_parse_expression(parser, &source_id, 0U)) {
                    return false;
                }
                source = minic_c0_program_expression(parser->program, source_id);
                if (source == NULL || source->value_category != MINIC_VALUE_LVALUE ||
                    !minic_type_is_record(source->type) ||
                    source->type.record_id != local.type.record_id) {
                    minic_parser_error(parser,
                                       "record local initializer requires a matching record lvalue");
                    return false;
                }
                return add_record_copy_assignments(parser, target_id, source_id, source->span);
            }
        }
'''
if text.count(old) != 1:
    raise SystemExit("unexpected record local initializer block")
path.write_text(text.replace(old, new, 1))
print("staged local record initialization from matching record lvalues")
