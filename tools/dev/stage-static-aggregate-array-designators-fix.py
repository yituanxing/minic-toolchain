#!/usr/bin/env python3
from pathlib import Path
import re

path = Path('src/frontend/parser_global.c')
data = path.read_bytes()
if data.count(b"'\x00'") != 1:
    raise SystemExit(f'publisher NUL repair: expected one source NUL, found {data.count(bytes([0]))}')
data = data.replace(b"'\x00'", b"'\\0'", 1)
text = data.decode()

old = '''        const MinicRecordField *field;\n        size_t element_index;\n        bool overwrite_materialized_field;\n'''
new = '''        const MinicRecordField *field;\n        bool overwrite_materialized_field;\n'''
if text.count(old) != 1:
    raise SystemExit(f'obsolete field-array cursor: expected one anchor, found {text.count(old)}')
text = text.replace(old, new, 1)

old = '''        if (parser->current.kind == MINIC_TOKEN_LPAREN) {\n            MinicType explicit_type;\n\n            if (!minic_parser_advance(parser) ||\n'''
new = '''        if (parser->current.kind == MINIC_TOKEN_LPAREN) {\n            MinicParser probe;\n            MinicType explicit_type;\n\n            probe = *parser;\n            if (!minic_parser_advance(&probe)) {\n                return false;\n            }\n            if (probe.current.kind == MINIC_TOKEN_LPAREN) {\n                if (!minic_parser_advance(parser) ||\n                    !minic_parser_parse_static_storage_initializer_value(parser, object_id, type) ||\n                    !minic_parser_expect(parser,\n                                         MINIC_TOKEN_RPAREN,\n                                         "expected ')' after grouped static record initializer")) {\n                    return false;\n                }\n                return true;\n            }\n            if (!minic_parser_advance(parser) ||\n'''
if text.count(old) != 1:
    raise SystemExit(f'grouped compound literal: expected one anchor, found {text.count(old)}')
text = text.replace(old, new, 1)

old = '''            if (!minic_type_equal(type, explicit_type)) {\n                minic_parser_error(parser, "static record compound literal type mismatch");\n                return false;\n            }\n'''
new = '''            if (!minic_type_is_record(explicit_type) ||\n                !minic_type_assignment_compatible(type, explicit_type)) {\n                minic_parser_error(parser, "static record compound literal type mismatch");\n                return false;\n            }\n'''
if text.count(old) != 1:
    raise SystemExit(f'compound literal compatibility: expected one anchor, found {text.count(old)}')
text = text.replace(old, new, 1)

new_record_array = r'''static bool probe_static_array_designator_extent(MinicParser *probe,
                                                 size_t *first,
                                                 size_t *last) {
    MinicC0Program before;
    MinicC0Program *program;

    if (probe == NULL || first == NULL || last == NULL || probe->program == NULL) {
        return false;
    }
    program = probe->program;
    before = *program;
    if (!minic_parser_parse_array_designator(probe, 0U, true, first, last)) {
        return false;
    }
    if (program->local_count != before.local_count ||
        program->cleanup_context_count != before.cleanup_context_count ||
        program->statement_count != before.statement_count ||
        program->inline_asm_count != before.inline_asm_count ||
        program->file_asm_count != before.file_asm_count ||
        program->block_count != before.block_count ||
        program->function_count != before.function_count ||
        program->record_count != before.record_count ||
        program->array_type_count != before.array_type_count ||
        program->function_type_count != before.function_type_count ||
        program->type_alias_count != before.type_alias_count || program->enum_count != before.enum_count ||
        program->enumerator_count != before.enumerator_count ||
        program->global_object_count != before.global_object_count ||
        program->fixed_register_binding_count != before.fixed_register_binding_count) {
        minic_parser_error(
            probe,
            "inferred aggregate array designator probe requires a side-effect-free integer constant expression");
        return false;
    }
    program->expression_count = before.expression_count;
    return true;
}

static bool inspect_static_array_initializer_extent(MinicParser *parser, size_t *element_count) {
    MinicParser probe;
    size_t extent;
    size_t next_index;

    if (parser == NULL || element_count == NULL || parser->current.kind != MINIC_TOKEN_LBRACE) {
        return false;
    }
    probe = *parser;
    if (!minic_parser_advance(&probe)) {
        return false;
    }
    extent = 0U;
    next_index = 0U;
    while (probe.current.kind != MINIC_TOKEN_RBRACE) {
        size_t first;
        size_t last;
        size_t brace_depth;
        size_t parenthesis_depth;
        size_t bracket_depth;

        first = next_index;
        last = next_index;
        if (probe.current.kind == MINIC_TOKEN_LBRACKET &&
            !probe_static_array_designator_extent(&probe, &first, &last)) {
            return false;
        }
        if (last == SIZE_MAX) {
            minic_parser_error(&probe, "inferred aggregate array initializer extent overflows");
            return false;
        }
        next_index = last + 1U;
        if (next_index > extent) {
            extent = next_index;
        }

        brace_depth = 0U;
        parenthesis_depth = 0U;
        bracket_depth = 0U;
        while (probe.current.kind != MINIC_TOKEN_EOF) {
            if (brace_depth == 0U && parenthesis_depth == 0U && bracket_depth == 0U &&
                (probe.current.kind == MINIC_TOKEN_COMMA ||
                 probe.current.kind == MINIC_TOKEN_RBRACE)) {
                break;
            }
            switch (probe.current.kind) {
            case MINIC_TOKEN_LBRACE:
                brace_depth += 1U;
                break;
            case MINIC_TOKEN_RBRACE:
                if (brace_depth == 0U) {
                    return false;
                }
                brace_depth -= 1U;
                break;
            case MINIC_TOKEN_LPAREN:
                parenthesis_depth += 1U;
                break;
            case MINIC_TOKEN_RPAREN:
                if (parenthesis_depth == 0U) {
                    return false;
                }
                parenthesis_depth -= 1U;
                break;
            case MINIC_TOKEN_LBRACKET:
                bracket_depth += 1U;
                break;
            case MINIC_TOKEN_RBRACKET:
                if (bracket_depth == 0U) {
                    return false;
                }
                bracket_depth -= 1U;
                break;
            default:
                break;
            }
            if (!minic_parser_advance(&probe)) {
                return false;
            }
        }
        if (probe.current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(&probe)) {
                return false;
            }
            if (probe.current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (probe.current.kind != MINIC_TOKEN_RBRACE) {
            return false;
        }
    }
    *element_count = extent;
    return true;
}

static bool
parse_static_record_array(MinicParser *parser, MinicType element_type, MinicSourceSpan name_span) {
    const MinicRecord *record;
    MinicType object_type;
    MinicGlobalObjectId object_id;
    size_t declared_count;
    bool inferred_bound;

    record = minic_c0_program_record(parser->program, element_type.record_id);
    if (record == NULL || !record->is_complete || record->is_union || record->field_count == 0U) {
        minic_parser_error(parser, "static record array requires a complete non-empty struct type");
        return false;
    }

    declared_count = 0U;
    inferred_bound = false;
    if (!minic_parser_expect(parser, MINIC_TOKEN_LBRACKET, "expected '['")) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_RBRACKET) {
        inferred_bound = true;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else if (!minic_parser_parse_fixed_array_bound(parser, &declared_count)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        minic_parser_error(parser, "multi-dimensional static record arrays are not supported yet");
        return false;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after static record array") ||
        parser->current.kind != MINIC_TOKEN_LBRACE) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot inspect static record array initializer");
        }
        return false;
    }
    if (inferred_bound) {
        if (!inspect_static_array_initializer_extent(parser, &declared_count)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot infer static record array initializer extent");
            }
            return false;
        }
        if (declared_count == 0U) {
            minic_parser_error(parser,
                               "cannot infer static record array bound from an empty initializer");
            return false;
        }
    }

    if (!minic_c0_program_add_array_type(
            parser->program, element_type, declared_count, &object_type) ||
        !minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            object_type,
                                            true,
                                            minic_type_is_const(element_type),
                                            &object_id) ||
        !minic_parser_parse_static_storage_initializer_value(parser, object_id, object_type)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot parse static record array initializer");
        }
        return false;
    }
    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after static record array");
}

static bool parse_static_record'''
text, count = re.subn(
    r"static bool\nparse_static_record_array\(.*?\n\}\n\nstatic bool parse_static_record",
    lambda match: new_record_array,
    text,
    count=1,
    flags=re.S,
)
if count != 1:
    raise SystemExit(f'inferred record array transaction: expected one function, found {count}')

path.write_text(text)
