#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_string.c")
text = path.read_text()

# pr73-static-pointer-array.py has already refactored string object construction before this
# transform runs. Keep escape decoding unchanged; only change payload termination and the
# object builder so a translation-phase string-literal sequence creates one array object.
start = text.find("static bool\nadd_string_initializers(")
end = text.find("bool minic_parser_create_string_literal_object(", start)
if start < 0 or end < 0:
    raise SystemExit("parser_string.c: staged string helper boundaries not found")
payload = r'''static bool
add_string_payload(MinicParser *parser, MinicSourceSpan span, MinicGlobalObjectId object_id) {
    size_t cursor;
    size_t end;

    cursor = span.begin.offset + 1U;
    end = span.end.offset - 1U;
    while (cursor < end) {
        int value;

        if (parser->source[cursor] == '\\') {
            cursor += 1U;
            if (!decode_string_escape(parser->source, &cursor, end, &value)) {
                minic_parser_error(parser, "unsupported string escape");
                return false;
            }
        } else {
            value = (int)(unsigned char)parser->source[cursor];
            cursor += 1U;
        }
        if (!minic_c0_global_object_add_initializer(parser->program, object_id, value)) {
            minic_parser_error(parser, "out of memory while storing string literal");
            return false;
        }
    }
    return true;
}

'''
text = text[:start] + payload + text[end:]

start = text.find("bool minic_parser_create_string_literal_object(")
end = text.find("bool minic_parser_parse_string_literal(", start)
if start < 0 or end < 0:
    raise SystemExit("parser_string.c: staged string object builder boundaries not found")
builder = r'''bool minic_parser_create_string_literal_object(MinicParser *parser,
                                               MinicGlobalObjectId *object_id,
                                               MinicType *array_type,
                                               MinicSourceSpan *span) {
    MinicParser probe;
    char object_name[64];
    int object_name_length;
    size_t decoded_length;
    size_t total_length;
    MinicSourceSpan combined_span;

    if (parser == NULL || object_id == NULL || array_type == NULL || span == NULL ||
        parser->current.kind != MINIC_TOKEN_STRING_LITERAL) {
        return false;
    }

    probe = *parser;
    combined_span = probe.current.span;
    total_length = 0U;
    while (probe.current.kind == MINIC_TOKEN_STRING_LITERAL) {
        if (!decoded_string_length(&probe, probe.current.span, &decoded_length) ||
            total_length > SIZE_MAX - decoded_length) {
            if (probe.diagnostic != NULL && probe.diagnostic->message[0] == '\0') {
                minic_parser_error(&probe, "concatenated string literal is too long");
            }
            return false;
        }
        total_length += decoded_length;
        combined_span.end = probe.current.span.end;
        if (!minic_parser_advance(&probe)) {
            return false;
        }
    }
    if (total_length == SIZE_MAX ||
        !minic_c0_program_add_array_type(
            parser->program, minic_type_char(), total_length + 1U, array_type)) {
        minic_parser_error(parser, "cannot build string literal array type");
        return false;
    }

    object_name_length = snprintf(object_name,
                                  sizeof(object_name),
                                  ".Lminic_string_%zu",
                                  parser->program->global_object_count);
    if (object_name_length <= 0 || (size_t)object_name_length >= sizeof(object_name) ||
        !minic_c0_program_add_global_object(parser->program,
                                            object_name,
                                            (size_t)object_name_length,
                                            *array_type,
                                            true,
                                            true,
                                            object_id)) {
        minic_parser_error(parser, "cannot create string literal object");
        return false;
    }

    while (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
        MinicSourceSpan literal_span;

        literal_span = parser->current.span;
        if (!add_string_payload(parser, literal_span, *object_id) || !minic_parser_advance(parser)) {
            return false;
        }
    }
    if (!minic_c0_global_object_add_initializer(parser->program, *object_id, 0)) {
        minic_parser_error(parser, "out of memory while terminating string literal");
        return false;
    }
    *span = combined_span;
    return true;
}

'''
path.write_text(text[:start] + builder + text[end:])
print("staged adjacent string literal concatenation")
