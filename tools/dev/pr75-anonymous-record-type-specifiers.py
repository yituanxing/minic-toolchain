#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_type.c")
text = path.read_text()
old = r'''    } else if (parser->current.kind == MINIC_TOKEN_KW_STRUCT) {
        MinicRecordId record_id;

        if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
            minic_parser_error(parser, "expected record tag after 'struct'");
            return false;
        }
        record_id = minic_parser_find_record(parser, parser->current.span);
        if (record_id == MINIC_RECORD_INVALID) {
            if (!minic_c0_program_add_record(parser->program,
                                             parser->source + parser->current.span.begin.offset,
                                             minic_parser_span_length(parser->current.span),
                                             &record_id)) {
                minic_parser_error(parser, "out of memory while declaring record tag");
                return false;
            }
        }
        parsed_type = minic_type_record(record_id);
        if (!minic_parser_advance(parser)) {
            return false;
        }
'''
new = r'''    } else if (parser->current.kind == MINIC_TOKEN_KW_STRUCT ||
               parser->current.kind == MINIC_TOKEN_KW_UNION) {
        MinicParser probe;
        MinicRecordId record_id;
        MinicTokenKind record_keyword;
        bool is_definition;
        bool is_union;

        record_keyword = parser->current.kind;
        is_union = record_keyword == MINIC_TOKEN_KW_UNION;
        probe = *parser;
        if (!minic_parser_advance(&probe)) {
            return false;
        }
        is_definition = probe.current.kind == MINIC_TOKEN_LBRACE;
        if (!is_definition && probe.current.kind == MINIC_TOKEN_IDENTIFIER) {
            if (!minic_parser_advance(&probe)) {
                return false;
            }
            is_definition = probe.current.kind == MINIC_TOKEN_LBRACE;
        }

        if (is_definition) {
            if (!minic_parser_parse_record_definition_specifier(parser, &parsed_type)) {
                return false;
            }
        } else {
            if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
                minic_parser_error(parser, "expected record tag or definition after record keyword");
                return false;
            }
            record_id = minic_parser_find_record(parser, parser->current.span);
            if (record_id == MINIC_RECORD_INVALID) {
                if (!minic_c0_program_add_record(parser->program,
                                                 parser->source + parser->current.span.begin.offset,
                                                 minic_parser_span_length(parser->current.span),
                                                 &record_id)) {
                    minic_parser_error(parser, "out of memory while declaring record tag");
                    return false;
                }
                parser->program->records[record_id].is_union = is_union;
            } else if (parser->program->records[record_id].is_union != is_union) {
                minic_parser_error(parser, "record tag kind does not match prior declaration");
                return false;
            }
            parsed_type = minic_type_record(record_id);
            if (!minic_parser_advance(parser)) {
                return false;
            }
        }
'''
if text.count(old) != 1:
    raise SystemExit(f"unexpected tagged struct type-specifier block count={text.count(old)}")
path.write_text(text.replace(old, new, 1))
print("staged anonymous struct/union type specifiers")
