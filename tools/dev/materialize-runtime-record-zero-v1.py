#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / "src/frontend/parser_statement.c"
text = path.read_text()

anchor = '''bool minic_parser_parse_runtime_record_initializer(MinicParser *parser,\n                                                   MinicExpressionId target_id) {\n'''
helper = '''static bool runtime_record_initializer_is_single_zero(const MinicParser *parser) {\n    MinicParser probe;\n    uint64_t value;\n\n    if (parser == NULL || parser->current.kind != MINIC_TOKEN_INTEGER_CONSTANT) {\n        return false;\n    }\n    probe = *parser;\n    probe.diagnostic = NULL;\n    if (!minic_parser_parse_unsigned_integer_value64(&probe, &value) || value != 0U) {\n        return false;\n    }\n    if (probe.current.kind == MINIC_TOKEN_COMMA && !minic_parser_advance(&probe)) {\n        return false;\n    }\n    return probe.current.kind == MINIC_TOKEN_RBRACE;\n}\n\nbool minic_parser_parse_runtime_record_initializer(MinicParser *parser,\n                                                   MinicExpressionId target_id) {\n'''
if text.count(anchor) != 1:
    raise SystemExit(f"runtime record initializer anchor count={text.count(anchor)}")
text = text.replace(anchor, helper, 1)

old = '''    if (parser->current.kind != MINIC_TOKEN_DOT) {\n        if (parser->current.kind == MINIC_TOKEN_RBRACE) {\n            initializer_span.end = parser->current.span.end;\n            return minic_parser_advance(parser) &&\n                   add_zero_initialized_record_lvalue(parser, target_id, initializer_span);\n        }\n        return parse_positional_runtime_record_initializer(\n            parser, target_id, initializer_span.begin);\n    }\n'''
new = '''    if (parser->current.kind != MINIC_TOKEN_DOT) {\n        if (parser->current.kind == MINIC_TOKEN_RBRACE) {\n            initializer_span.end = parser->current.span.end;\n            return minic_parser_advance(parser) &&\n                   add_zero_initialized_record_lvalue(parser, target_id, initializer_span);\n        }\n        if (runtime_record_initializer_is_single_zero(parser)) {\n            uint64_t zero_value;\n\n            if (!minic_parser_parse_unsigned_integer_value64(parser, &zero_value) ||\n                zero_value != 0U) {\n                return false;\n            }\n            if (parser->current.kind == MINIC_TOKEN_COMMA && !minic_parser_advance(parser)) {\n                return false;\n            }\n            if (parser->current.kind != MINIC_TOKEN_RBRACE) {\n                minic_parser_error(parser, \"expected '}' after aggregate zero initializer\");\n                return false;\n            }\n            initializer_span.end = parser->current.span.end;\n            return minic_parser_advance(parser) &&\n                   add_zero_initialized_record_lvalue(parser, target_id, initializer_span);\n        }\n        return parse_positional_runtime_record_initializer(\n            parser, target_id, initializer_span.begin);\n    }\n'''
if text.count(old) != 1:
    raise SystemExit(f"runtime positional branch count={text.count(old)}")
path.write_text(text.replace(old, new, 1))
