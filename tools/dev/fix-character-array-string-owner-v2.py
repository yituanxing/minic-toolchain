#!/usr/bin/env python3
from pathlib import Path

# The decoded narrow-character domain is int (0..255), matching the existing
# global initializer API. Keep the shared value vector narrow instead of
# introducing an unnecessary uint64_t transport type.
for name in (
    "src/frontend/parser_internal.h",
    "src/frontend/parser_string.c",
    "src/frontend/parser_statement.c",
):
    path = Path(name)
    text = path.read_text()
    text = text.replace("uint64_t *values", "int *values")
    text = text.replace("(uint64_t *)calloc", "(int *)calloc")
    path.write_text(text)

path = Path("src/frontend/parser_string.c")
text = path.read_text()
old = '''            values[stored_count++] = (uint64_t)(unsigned int)value;
'''
new = '''            values[stored_count++] = value;
'''
if text.count(old) != 1:
    raise SystemExit("unexpected decoded bounded-string value assignment")
text = text.replace(old, new, 1)
old = '''            if (!minic_c0_global_object_add_initializer(
                    parser->program, object_id, (int64_t)values[index])) {
'''
new = '''            if (!minic_c0_global_object_add_initializer(
                    parser->program, object_id, values[index])) {
'''
if text.count(old) != 1:
    raise SystemExit("unexpected bounded-string global initializer write")
text = text.replace(old, new, 1)

# The first staging replacement deliberately refactors the bounded-add function,
# but its original end marker also swallowed this private backward-overwrite
# helper. Restore it unchanged; backward aggregate/string overlay semantics remain
# owned by the existing replace-zero path rather than the new shared decoder.
marker = "bool minic_parser_replace_zero_bounded_string_literal_initializer("
if text.count(marker) != 1:
    raise SystemExit("unexpected replace-zero bounded string owner shape")
if "static bool replace_zero_string_payload(" not in text:
    helper = r'''static bool replace_zero_string_payload(MinicParser *parser,
                                        MinicSourceSpan span,
                                        MinicTokenKind kind,
                                        MinicGlobalObjectId object_id,
                                        size_t *slot_index,
                                        size_t slot_limit) {
    size_t cursor;
    size_t end;

    if (slot_index == NULL || !string_literal_payload_bounds(parser, span, kind, &cursor, &end)) {
        return false;
    }
    while (cursor < end) {
        int value;

        if (*slot_index >= slot_limit) {
            return false;
        }
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
        if (!minic_c0_global_object_replace_zero_initializer_bits(
                parser->program, object_id, *slot_index, (uint64_t)(int64_t)value)) {
            minic_parser_error(parser,
                               "backward string initializer can only replace implicit zero slots");
            return false;
        }
        *slot_index += 1U;
    }
    return true;
}

'''
    text = text.replace(marker, helper + marker, 1)
path.write_text(text)

# Runtime lowering consumes the same int vector; widen only when storing into the
# AST's integer literal payload.
path = Path("src/frontend/parser_statement.c")
text = path.read_text()
text = text.replace("value.value.integer_value = (int64_t)values[index];",
                    "value.value.integer_value = (int64_t)values[index];")
path.write_text(text)

print("tightened bounded character values to int and restored overwrite helper")
