#!/usr/bin/env python3
from pathlib import Path

# Expose the bounded narrow-string value decoder so static and runtime character
# arrays share concatenation, escape, exact-fit, terminator, and padding semantics.
header = Path("src/frontend/parser_internal.h")
text = header.read_text()
anchor = '''bool minic_parser_add_bounded_string_literal_initializer(MinicParser *parser,
                                                         MinicGlobalObjectId object_id,
                                                         size_t element_capacity);
'''
addition = '''bool minic_parser_parse_bounded_string_literal_values(MinicParser *parser,
                                                       size_t element_capacity,
                                                       uint64_t *values);
''' + anchor
if text.count(anchor) != 1 or "minic_parser_parse_bounded_string_literal_values" in text:
    raise SystemExit("unexpected parser string declaration shape")
header.write_text(text.replace(anchor, addition, 1))

string_parser = Path("src/frontend/parser_string.c")
text = string_parser.read_text()
start_marker = "bool minic_parser_add_bounded_string_literal_initializer("
end_marker = "bool minic_parser_replace_zero_bounded_string_literal_initializer("
if text.count(start_marker) != 1 or text.count(end_marker) != 1:
    raise SystemExit("unexpected bounded string initializer owner shape")
start = text.index(start_marker)
end = text.index(end_marker, start)
replacement = r'''bool minic_parser_parse_bounded_string_literal_values(MinicParser *parser,
                                                       size_t element_capacity,
                                                       uint64_t *values) {
    MinicParser probe;
    size_t decoded_length;
    size_t total_length;
    size_t stored_count;

    if (parser == NULL || values == NULL || element_capacity == 0U ||
        parser->current.kind != MINIC_TOKEN_STRING_LITERAL) {
        return false;
    }
    probe = *parser;
    total_length = 0U;
    while (probe.current.kind == MINIC_TOKEN_STRING_LITERAL) {
        if (!decoded_string_length(
                &probe, probe.current.span, probe.current.kind, &decoded_length) ||
            total_length > SIZE_MAX - decoded_length || !minic_parser_advance(&probe)) {
            return false;
        }
        total_length += decoded_length;
    }
    if (total_length > element_capacity) {
        minic_parser_error(parser, "string initializer is too long for character array");
        return false;
    }

    (void)memset(values, 0, element_capacity * sizeof(*values));
    stored_count = 0U;
    while (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
        size_t cursor;
        size_t literal_end;
        MinicSourceSpan literal_span;

        literal_span = parser->current.span;
        if (!string_literal_payload_bounds(
                parser, literal_span, MINIC_TOKEN_STRING_LITERAL, &cursor, &literal_end)) {
            return false;
        }
        while (cursor < literal_end) {
            int value;

            if (stored_count >= element_capacity) {
                return false;
            }
            if (parser->source[cursor] == '\\') {
                cursor += 1U;
                if (!decode_string_escape(parser->source, &cursor, literal_end, &value)) {
                    minic_parser_error(parser, "unsupported string escape");
                    return false;
                }
            } else {
                value = (int)(unsigned char)parser->source[cursor];
                cursor += 1U;
            }
            values[stored_count++] = (uint64_t)(unsigned int)value;
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    return true;
}

bool minic_parser_add_bounded_string_literal_initializer(MinicParser *parser,
                                                         MinicGlobalObjectId object_id,
                                                         size_t element_capacity) {
    uint64_t *values;
    size_t index;
    bool success;

    if (parser == NULL || element_capacity == 0U) {
        return false;
    }
    values = (uint64_t *)calloc(element_capacity, sizeof(*values));
    if (values == NULL) {
        minic_parser_error(parser, "out of memory while decoding bounded string initializer");
        return false;
    }
    success = minic_parser_parse_bounded_string_literal_values(
        parser, element_capacity, values);
    if (success) {
        for (index = 0U; index < element_capacity; ++index) {
            if (!minic_c0_global_object_add_initializer(
                    parser->program, object_id, (int64_t)values[index])) {
                minic_parser_error(parser, "out of memory while storing bounded string initializer");
                success = false;
                break;
            }
        }
    }
    free(values);
    return success;
}

'''
string_parser.write_text(text[:start] + replacement + text[end:])

# Fixed static scalar arrays already own integer/pointer initializer plans. Route
# fixed character arrays with a string token to the canonical bounded string owner
# before the brace-only scalar-list path.
global_parser = Path("src/frontend/parser_global.c")
text = global_parser.read_text()
anchor = '''    minic_array_initializer_plan_initialize(&plan, element_count, infer_bound);
'''
insert = r'''    if (parser != NULL && !infer_bound && element_count != 0U &&
        minic_type_is_char_integer(element_type) &&
        parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
        return minic_parser_add_bounded_string_literal_initializer(
            parser, object_id, element_count);
    }

'''
if text.count(anchor) != 1 or "parser, object_id, element_count);\n    }\n\n    minic_array_initializer_plan_initialize" in text:
    raise SystemExit("unexpected static scalar-array transaction shape")
global_parser.write_text(text.replace(anchor, insert + anchor, 1))

# Runtime local arrays lower initializers to ordinary element assignments. Decode
# the string once using parser_string.c, then lower the resulting fixed value vector.
statement_parser = Path("src/frontend/parser_statement.c")
text = statement_parser.read_text()
function_marker = '''static bool
parse_local_array_initializer(MinicParser *parser, MinicLocalId local_id, bool infer_count) {
'''
if text.count(function_marker) != 1 or "parse_local_character_array_string_initializer" in text:
    raise SystemExit("unexpected local array initializer owner shape")
helper = r'''static bool parse_local_character_array_string_initializer(MinicParser *parser,
                                                           MinicLocalId local_id,
                                                           bool infer_count,
                                                           size_t declared_count) {
    const MinicLocal *local;
    MinicParser probe;
    MinicSourceSpan initializer_span;
    uint64_t string_size;
    uint64_t *values;
    size_t element_size;
    size_t element_count;
    size_t index;
    bool success;

    local = minic_c0_program_local(parser->program, local_id);
    if (local == NULL || !local->is_array || !minic_type_is_char_integer(local->type) ||
        parser->current.kind != MINIC_TOKEN_STRING_LITERAL) {
        return false;
    }
    initializer_span = parser->current.span;
    element_count = declared_count;
    if (infer_count) {
        probe = *parser;
        if (!minic_parser_parse_string_literal_size(&probe, &string_size) ||
            !minic_target_info_sizeof_type(
                parser->target_info, parser->program, local->type, &element_size) ||
            element_size == 0U || string_size == 0U ||
            string_size % (uint64_t)element_size != 0U ||
            string_size / (uint64_t)element_size > SIZE_MAX) {
            minic_parser_error(parser, "cannot infer local character array extent from string");
            return false;
        }
        element_count = (size_t)(string_size / (uint64_t)element_size);
        parser->program->locals[local_id].element_count = element_count;
    }
    if (element_count == 0U) {
        minic_parser_error(parser, "character array string initializer requires nonzero extent");
        return false;
    }

    values = (uint64_t *)calloc(element_count, sizeof(*values));
    if (values == NULL) {
        minic_parser_error(parser, "out of memory while decoding local string initializer");
        return false;
    }
    success = minic_parser_parse_bounded_string_literal_values(parser, element_count, values);
    for (index = 0U; success && index < element_count; ++index) {
        MinicExpression value;
        MinicExpressionId value_id;

        (void)memset(&value, 0, sizeof(value));
        value.kind = MINIC_EXPRESSION_INTEGER;
        value.span = initializer_span;
        value.type = minic_type_int();
        value.value_category = MINIC_VALUE_RVALUE;
        value.value.integer_value = (int64_t)values[index];
        success = minic_parser_add_expression(parser, &value, &value_id) &&
                  add_local_array_element_assignment(parser, local_id, index, value_id);
    }
    free(values);
    return success;
}

'''
text = text.replace(function_marker, helper + function_marker, 1)
old = '''    declared_count = local->element_count;
    initializer_span.begin = local->name_span.begin;
    if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_LBRACE) {
        minic_parser_error(parser, "array initializers are not supported yet");
        return false;
    }
'''
new = r'''    declared_count = local->element_count;
    initializer_span.begin = local->name_span.begin;
    if (!minic_parser_advance(parser)) {
        return false;
    }
    if (minic_type_is_char_integer(local->type) &&
        parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
        return parse_local_character_array_string_initializer(
            parser, local_id, infer_count, declared_count);
    }
    if (parser->current.kind != MINIC_TOKEN_LBRACE) {
        minic_parser_error(parser, "array initializers are not supported yet");
        return false;
    }
'''
if text.count(old) != 1:
    raise SystemExit("unexpected local array brace-only dispatch shape")
statement_parser.write_text(text.replace(old, new, 1))

# Permanent execution regression: exact-fit static array, padded static array,
# fixed runtime local array, inferred runtime local array, adjacent strings, and escape.
program = Path("tests/programs/c0/character_array_string_initializer.c")
if program.exists():
    raise SystemExit("character array string initializer program already exists")
program.write_text(r'''char global_padded[10] = "ratelimit";
static char global_exact[3] = "abc";

static int runtime_string_check(void) {
    char path[16] = "//enomem";
    char inferred[] = "x" "\\n";

    return path[0] == '/' && path[1] == '/' && path[2] == 'e' && path[7] == 'm' &&
           path[8] == 0 && path[15] == 0 && sizeof(inferred) == 4 && inferred[0] == 'x' &&
           inferred[1] == '\\' && inferred[2] == 'n' && inferred[3] == 0;
}

int main(void) {
    return global_padded[0] == 'r' && global_padded[8] == 't' && global_padded[9] == 0 &&
                   global_exact[0] == 'a' && global_exact[2] == 'c' && runtime_string_check()
               ? 0
               : 1;
}
''')

manifest = Path("tests/programs/c0/manifest.txt")
text = manifest.read_text()
anchor = "string_literals\n"
if text.count(anchor) != 1 or "character_array_string_initializer" in text:
    raise SystemExit("unexpected C0 execution manifest string anchor")
manifest.write_text(text.replace(anchor, anchor + "character_array_string_initializer\n", 1))

# Cheap source-level focused gate also verifies static bytes/extent in emitted assembly.
focused = Path("tests/compiler/c0/run-character-array-string-initializer.sh")
if focused.exists():
    raise SystemExit("focused character array string runner already exists")
focused.write_text(r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-character-array-string-init

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -x c "$root/tests/programs/c0/character_array_string_initializer.c" \
    -o "$work/character_array_string_initializer.i"
"$minic" -S "$work/character_array_string_initializer.i" \
    -o "$work/character_array_string_initializer.s"

grep -F '.type global_padded, @object' "$work/character_array_string_initializer.s" >/dev/null
grep -F '.size global_padded, 10' "$work/character_array_string_initializer.s" >/dev/null
grep -F '  .byte 114' "$work/character_array_string_initializer.s" >/dev/null
grep -F '  .byte 116' "$work/character_array_string_initializer.s" >/dev/null
grep -F '.size global_exact, 3' "$work/character_array_string_initializer.s" >/dev/null
grep -F '  li a0, 47' "$work/character_array_string_initializer.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/character-array-string-initializer static=fixed+exact runtime=fixed+inferred adjacent=1 padding=1'
''')

run = Path("tests/compiler/c0/run.sh")
text = run.read_text()
anchor = '''MINIC="$minic" \\
HOST_CC="$host_cc" \\
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\
sh "$root/tests/compiler/c0/run-runtime-local-array-initializer.sh"
'''
addition = anchor + '''
MINIC="$minic" \\
HOST_CC="$host_cc" \\
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\
sh "$root/tests/compiler/c0/run-character-array-string-initializer.sh"
'''
if text.count(anchor) != 1:
    raise SystemExit("unexpected C0 runtime local array runner anchor")
run.write_text(text.replace(anchor, addition, 1))

print("materialized shared character-array string initialization")
