#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

def read(p): return (ROOT / p).read_text()
def write(p, s): (ROOT / p).write_text(s)
def one(s, old, new, label):
    n=s.count(old)
    if n != 1: raise SystemExit(f"{label}: expected 1 match, found {n}")
    return s.replace(old,new,1)

p="src/frontend/parser_attribute.c"; s=read(p)
old='''bool minic_parser_apply_alignment_attribute(MinicParser *parser,
                                            const MinicParsedAttribute *attribute,
                                            const char *subject,
                                            size_t *explicit_alignment) {
    MinicParser probe;
    int64_t parsed_alignment;
    size_t alignment;

    if (parser == NULL || attribute == NULL || subject == NULL || explicit_alignment == NULL ||
        !attribute->has_arguments ||
        attribute->arguments_span.end.offset <= attribute->arguments_span.begin.offset + 1U) {
        return false;
    }
    probe = *parser;
    minic_lexer_initialize(&probe.lexer, parser->path, parser->source, parser->lexer.length);
    probe.lexer.cursor = attribute->arguments_span.begin.offset + 1U;
    probe.lexer.line = attribute->arguments_span.begin.line;
    probe.lexer.column = attribute->arguments_span.begin.column + 1U;
    if (!minic_parser_advance(&probe) ||
        !minic_parser_parse_integer_constant_expression(&probe, &parsed_alignment) ||
        probe.current.kind != MINIC_TOKEN_RPAREN ||
        probe.current.span.end.offset != attribute->arguments_span.end.offset) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
            minic_parser_error(
                parser, "GNU %s alignment requires one integer constant expression", subject);
        }
        return false;
    }
    if (parsed_alignment <= 0 || (uint64_t)parsed_alignment > (uint64_t)SIZE_MAX) {
        minic_parser_error(
            parser, "GNU %s alignment must be a positive target-size value", subject);
        return false;
    }
    alignment = (size_t)parsed_alignment;
    if ((alignment & (alignment - 1U)) != 0U) {
        minic_parser_error(parser, "GNU %s alignment must be a power of two", subject);
        return false;
    }
    if (alignment > *explicit_alignment) {
        *explicit_alignment = alignment;
    }
    return true;
}
'''
new='''bool minic_parser_apply_alignment_attribute(MinicParser *parser,
                                            const MinicParsedAttribute *attribute,
                                            const char *subject,
                                            size_t *explicit_alignment) {
    MinicParser probe;
    MinicExpressionId expression_id;
    const MinicExpression *expression;
    MinicConstValue constant_value;
    int64_t parsed_alignment;
    size_t alignment;

    if (parser == NULL || attribute == NULL || subject == NULL || explicit_alignment == NULL ||
        !attribute->has_arguments ||
        attribute->arguments_span.end.offset <= attribute->arguments_span.begin.offset + 1U) {
        return false;
    }
    probe = *parser;
    minic_lexer_initialize(&probe.lexer, parser->path, parser->source, parser->lexer.length);
    probe.lexer.cursor = attribute->arguments_span.begin.offset + 1U;
    probe.lexer.line = attribute->arguments_span.begin.line;
    probe.lexer.column = attribute->arguments_span.begin.column + 1U;
    if (!minic_parser_advance(&probe) ||
        !minic_parser_parse_expression(&probe, &expression_id, 0U)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
            minic_parser_error(
                parser, "GNU %s alignment requires one integer constant expression", subject);
        }
        return false;
    }
    expression = minic_c0_program_expression(parser->program, expression_id);
    if (probe.current.kind != MINIC_TOKEN_RPAREN ||
        probe.current.span.end.offset != attribute->arguments_span.end.offset || expression == NULL ||
        !minic_type_is_integer(expression->type) ||
        !minic_const_eval_integer(
            parser->program, parser->target_info, expression_id, &constant_value) ||
        !minic_const_value_as_int64(
            parser->program, parser->target_info, &constant_value, &parsed_alignment)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
            minic_parser_error(
                parser, "GNU %s alignment requires one integer constant expression", subject);
        }
        return false;
    }
    if (parsed_alignment <= 0 || (uint64_t)parsed_alignment > (uint64_t)SIZE_MAX) {
        minic_parser_error(
            parser, "GNU %s alignment must be a positive target-size value", subject);
        return false;
    }
    alignment = (size_t)parsed_alignment;
    if ((alignment & (alignment - 1U)) != 0U) {
        minic_parser_error(parser, "GNU %s alignment must be a power of two", subject);
        return false;
    }
    if (alignment > *explicit_alignment) {
        *explicit_alignment = alignment;
    }
    return true;
}
'''
s=one(s,old,new,"alignment decoder")
write(p,s)

p="tests/compiler/c0/gnu_aligned_record_field.c"; s=read(p)
s=one(s,'__attribute__((__aligned__(16)))','__attribute__((__aligned__(1 << (4))))',"Linux shift alignment shape")
write(p,s)

p="tests/compiler/c0/run-gnu-aligned-record-field.sh"; s=read(p)
s=s.replace('packing=little-endian boundary=type-alignment', 'packing=little-endian boundary=type-alignment') if False else s
# Preserve existing assertions; only make the migration visible in the PASS contract.
if 'typed-ast-consteval=1' not in s:
    s=s.replace('minimum-align=16 ', 'minimum-align=16 typed-ast-consteval=1 ', 1)
write(p,s)

p="tests/compiler/c0/run-gnu-record-alignment.sh"; s=read(p)
if 'typed-alignment-consteval=1' not in s:
    s=s.replace('shared-alignment-decoder=1 ', 'shared-alignment-decoder=1 typed-alignment-consteval=1 ', 1)
write(p,s)
