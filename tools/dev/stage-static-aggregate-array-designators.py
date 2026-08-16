#!/usr/bin/env python3
from pathlib import Path
import re


def read(path: str) -> str:
    return Path(path).read_text()


def write(path: str, text: str) -> None:
    Path(path).write_text(text)


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


def regex_once(text: str, pattern: str, replacement: str, label: str) -> str:
    updated, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if count != 1:
        raise SystemExit(f"{label}: expected one regex match, found {count}")
    return updated


path = "src/frontend/parser_global.c"
text = read(path)

new_array_parser = r'''static bool parse_static_forward_array_initializer(MinicParser *parser,
                                                   MinicGlobalObjectId object_id,
                                                   MinicType element_type,
                                                   size_t element_count,
                                                   bool infer_bound,
                                                   size_t *parsed_extent) {
    size_t next_index;
    size_t extent;

    if (parser == NULL || object_id >= parser->program->global_object_count ||
        (!infer_bound && element_count == 0U) || parser->current.kind != MINIC_TOKEN_LBRACE) {
        if (parser != NULL) {
            minic_parser_error(parser, "invalid static aggregate array initializer");
        }
        return false;
    }
    next_index = 0U;
    extent = infer_bound ? 0U : element_count;
    if (!minic_parser_advance(parser)) {
        return false;
    }
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        size_t first;
        size_t last;

        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            if (!minic_parser_parse_array_designator(
                    parser, element_count, infer_bound, &first, &last)) {
                return false;
            }
            if (last != first) {
                minic_parser_error(
                    parser,
                    "GNU range designators for aggregate static arrays are not supported yet");
                return false;
            }
            if (first < next_index) {
                minic_parser_error(
                    parser,
                    "backward static aggregate array designator is not supported yet");
                return false;
            }
        } else {
            first = next_index;
            if (!infer_bound && first >= element_count) {
                minic_parser_error(parser, "too many nested static array initializers");
                return false;
            }
        }
        while (next_index < first) {
            if (!append_static_constant_zero(parser, object_id, element_type)) {
                minic_parser_error(parser, "cannot zero-fill skipped static array element");
                return false;
            }
            next_index += 1U;
        }
        if (!minic_parser_parse_static_storage_initializer_value(parser, object_id, element_type)) {
            return false;
        }
        if (first == SIZE_MAX) {
            minic_parser_error(parser, "static array initializer extent overflows");
            return false;
        }
        next_index = first + 1U;
        if (next_index > extent) {
            extent = next_index;
        }
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in static array initializer");
            return false;
        }
    }
    if (!minic_parser_expect(
            parser, MINIC_TOKEN_RBRACE, "expected '}' after static array initializer")) {
        return false;
    }
    if (infer_bound) {
        if (extent == 0U) {
            minic_parser_error(parser, "cannot infer static array bound from an empty initializer");
            return false;
        }
    } else {
        while (next_index < element_count) {
            if (!append_static_constant_zero(parser, object_id, element_type)) {
                minic_parser_error(parser, "cannot zero-fill static array initializer tail");
                return false;
            }
            next_index += 1U;
        }
    }
    if (parsed_extent != NULL) {
        *parsed_extent = extent;
    }
    return true;
}

static bool parse_static_array_constant(MinicParser *parser,
                                        MinicGlobalObjectId object_id,
                                        const MinicArrayType *array_type) {
    MinicType element_type;
    size_t element_count;
    size_t parsed_extent;
    bool infer_bound;

    if (array_type == NULL) {
        minic_parser_error(parser, "invalid static array initializer type");
        return false;
    }
    element_type = array_type->element_type;
    element_count = array_type->element_count;
    infer_bound = element_count == 0U && !array_type->is_zero_length;
    if (element_count == 0U && array_type->is_zero_length) {
        minic_parser_error(parser, "invalid zero-length static array initializer type");
        return false;
    }
    if (minic_type_is_integer(element_type) || minic_type_is_pointer(element_type)) {
        return parse_static_scalar_array_transaction(
            parser, object_id, element_type, element_count, infer_bound);
    }
    parsed_extent = 0U;
    if (!parse_static_forward_array_initializer(
            parser, object_id, element_type, element_count, infer_bound, &parsed_extent)) {
        return false;
    }
    if (infer_bound) {
        const MinicGlobalObject *object;

        object = minic_c0_program_global_object(parser->program, object_id);
        if (object == NULL || !minic_type_is_array(object->type) ||
            !minic_c0_program_complete_array_type(parser->program, object->type, parsed_extent)) {
            minic_parser_error(parser, "cannot complete inferred static aggregate array type");
            return false;
        }
    }
    return true;
}

static bool parse_static_record_constant'''
text = regex_once(
    text,
    r"static bool parse_static_array_constant\(.*?\n\}\n\nstatic bool parse_static_record_constant",
    new_array_parser,
    "non-scalar array initializer owner",
)

old_field_array = r'''            } else {
                if (!minic_parser_expect(parser,
                                         MINIC_TOKEN_LBRACE,
                                         "expected '{' in record field array initializer")) {
                    return false;
                }
                element_index = 0U;
                while (parser->current.kind != MINIC_TOKEN_RBRACE) {
                    if (element_index >= field->element_count ||
                        !minic_parser_parse_static_storage_initializer_value(
                            parser, object_id, field->type)) {
                        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                            minic_parser_error(parser, "too many record field array initializers");
                        }
                        return false;
                    }
                    element_index += 1U;
                    if (parser->current.kind == MINIC_TOKEN_COMMA) {
                        if (!minic_parser_advance(parser)) {
                            return false;
                        }
                        if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                            break;
                        }
                    } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
                        minic_parser_error(parser,
                                           "expected ',' or '}' in record field array initializer");
                        return false;
                    }
                }
                while (element_index < field->element_count) {
                    if (!append_static_constant_zero(parser, object_id, field->type)) {
                        return false;
                    }
                    element_index += 1U;
                }
                if (!minic_parser_expect(parser,
                                         MINIC_TOKEN_RBRACE,
                                         "expected '}' after record field array initializer")) {
                    return false;
                }
            }
'''
new_field_array = r'''            } else if (!parse_static_forward_array_initializer(parser,
                                                               object_id,
                                                               field->type,
                                                               field->element_count,
                                                               false,
                                                               NULL)) {
                return false;
            }
'''
text = replace_once(text, old_field_array, new_field_array, "record field array owner")

new_inferred_record_array = r'''static bool
parse_static_record_array(MinicParser *parser, MinicType element_type, MinicSourceSpan name_span) {
    const MinicRecord *record;
    MinicType object_type;
    MinicGlobalObjectId object_id;

    record = minic_c0_program_record(parser->program, element_type.record_id);
    if (record == NULL || !record->is_complete || record->is_union || record->field_count == 0U) {
        minic_parser_error(parser, "static record array requires a complete non-empty struct type");
        return false;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_LBRACKET, "expected '['") ||
        !minic_parser_expect(parser,
                             MINIC_TOKEN_RBRACKET,
                             "inferred static record array requires an empty bound") ||
        !minic_c0_program_add_incomplete_array_type(parser->program, element_type, &object_type) ||
        !minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            object_type,
                                            true,
                                            minic_type_is_const(element_type),
                                            &object_id) ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after static record array") ||
        !minic_parser_parse_static_storage_initializer_value(parser, object_id, object_type)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot parse inferred static record array initializer");
        }
        return false;
    }
    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after static record array");
}

static bool parse_static_record'''
text = regex_once(
    text,
    r"static bool count_static_array_initializer_elements\(.*?\n\}\n\nstatic bool\nparse_static_record_array\(.*?\n\}\n\nstatic bool parse_static_record",
    new_inferred_record_array,
    "inferred record array owner",
)

write(path, text)

# Permanent regression mirrors all three current Linux shapes.
write(
    "tests/compiler/c0/static_aggregate_array_designators.c",
    r'''enum slot_index { SLOT_ONE = 1, SLOT_THREE = 3 };

struct pair {
    int left;
    int right;
};

struct holder {
    struct pair limits[4];
};

typedef struct pair pair_t;

static const struct pair sparse_records[] = {
    [SLOT_ONE] = { .left = 11, .right = 12 },
    [SLOT_THREE] = (struct pair){ .left = 31, .right = 32 },
};

static const struct holder nested_records = {
    .limits = {
        [1] = { .left = 21, .right = 22 },
        [3] = { .left = 41, .right = 42 },
    },
};

static const pair_t fixed_records[4] = {
    [2] = ((pair_t){ .left = 51, .right = 52 }),
};

int main(void) {
    return sparse_records[1].left == 11 && sparse_records[3].right == 32 &&
                   nested_records.limits[1].right == 22 &&
                   nested_records.limits[3].left == 41 && fixed_records[2].right == 52
               ? 0
               : 1;
}
''',
)
write(
    "tests/compiler/c0/invalid_static_aggregate_array_backward_designator.c",
    r'''struct pair {
    int left;
    int right;
};

static const struct pair bad[3] = {
    [2] = { 1, 2 },
    [1] = { 3, 4 },
};
''',
)
write(
    "tests/compiler/c0/invalid_static_aggregate_array_range_designator.c",
    r'''struct pair {
    int left;
    int right;
};

static const struct pair bad[3] = {
    [0 ... 1] = { 1, 2 },
};
''',
)
write(
    "tests/compiler/c0/run-static-aggregate-array-designators.sh",
    r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-aggregate-array-designators

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/static_aggregate_array_designators.c" \
    -o "$work/static_aggregate_array_designators.i"
"$minic" -S "$work/static_aggregate_array_designators.i" \
    -o "$work/static_aggregate_array_designators.s"

grep -F '.size sparse_records, 32' "$work/static_aggregate_array_designators.s" >/dev/null
grep -F '.size nested_records, 32' "$work/static_aggregate_array_designators.s" >/dev/null
grep -F '.size fixed_records, 32' "$work/static_aggregate_array_designators.s" >/dev/null
grep -F '  .word 11' "$work/static_aggregate_array_designators.s" >/dev/null
grep -F '  .word 32' "$work/static_aggregate_array_designators.s" >/dev/null
grep -F '  .word 52' "$work/static_aggregate_array_designators.s" >/dev/null

for case_name in backward range; do
    source="$root/tests/compiler/c0/invalid_static_aggregate_array_${case_name}_designator.c"
    "$host_cc" -E -P -std=gnu11 -x c "$source" -o "$work/$case_name.i"
    if "$minic" -S "$work/$case_name.i" -o "$work/$case_name.s" \
        >"$work/$case_name.stdout" 2>"$work/$case_name.stderr"; then
        printf '%s\n' "FAIL static aggregate array $case_name designator unexpectedly succeeded" >&2
        exit 1
    fi
done

grep -F 'backward static aggregate array designator is not supported yet' \
    "$work/backward.stderr" >/dev/null
grep -F 'GNU range designators for aggregate static arrays are not supported yet' \
    "$work/range.stderr" >/dev/null

printf '%s\n' \
    'PASS compiler/c0/static_aggregate_array_designators inferred-bound=designator-extent nested-field=1 compound-literal=1 backward=fail-closed range=fail-closed'
''',
)

path = "tests/compiler/c0/run-foundation-focused.sh"
text = read(path)
text = replace_once(
    text,
    "    run-static-fixed-record-array-zero.sh \\\n",
    "    run-static-fixed-record-array-zero.sh \\\n    run-static-aggregate-array-designators.sh \\\n",
    "foundation runner",
)
write(path, text)
