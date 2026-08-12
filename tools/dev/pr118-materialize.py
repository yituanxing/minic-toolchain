from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"anchor mismatch {path}: {count}: {old[:100]!r}")
    p.write_text(text.replace(old, new, 1))


def replace_region(path: str, start_marker: str, end_marker: str, replacement: str) -> None:
    p = Path(path)
    text = p.read_text()
    start = text.find(start_marker)
    end = text.find(end_marker, start + len(start_marker))
    if start < 0 or end < 0:
        raise SystemExit(f"region mismatch {path}: start={start} end={end}")
    p.write_text(text[:start] + replacement + text[end:])


path = "src/frontend/parser_statement.c"

# Generalize the existing local-array element assignment to any semantic array lvalue.
replace_region(
    path,
    "static bool add_local_array_element_assignment(",
    "static bool add_local_array_zero_element(",
    '''static bool add_array_object_element_assignment(MinicParser *parser,
                                                       MinicExpressionId base_id,
                                                       size_t index,
                                                       MinicExpressionId value_id) {
    const MinicExpression *base;
    const MinicExpression *value;
    MinicArrayObjectInfo array_info;
    MinicExpression index_expression;
    MinicExpression subscript;
    MinicExpressionId index_id;
    MinicExpressionId target_id;
    MinicSourceSpan base_span;
    MinicSourceSpan value_span;
    MinicStatement statement;
    MinicType element_type;

    base = minic_c0_program_expression(parser->program, base_id);
    value = minic_c0_program_expression(parser->program, value_id);
    if (base == NULL || value == NULL ||
        !minic_c0_expression_array_object_info(parser->program, base, &array_info) ||
        index > (size_t)INT64_MAX) {
        minic_parser_error(parser, "invalid runtime array initializer element");
        return false;
    }
    base_span = base->span;
    element_type = array_info.element_type;
    value_span = value->span;
    if (minic_type_is_record(element_type)) {
        minic_parser_error(parser, "record array initializer elements are not supported yet");
        return false;
    }
    if (!apply_assignment_conversion(parser, element_type, &value_id) ||
        !minic_c0_assignment_compatible(parser->program, element_type, value_id)) {
        minic_parser_error(parser, "runtime array initializer element type mismatch");
        return false;
    }
    value = minic_c0_program_expression(parser->program, value_id);
    if (value == NULL) {
        minic_parser_error(parser, "invalid converted runtime array initializer element");
        return false;
    }
    value_span = value->span;

    (void)memset(&index_expression, 0, sizeof(index_expression));
    index_expression.kind = MINIC_EXPRESSION_INTEGER;
    index_expression.span = value_span;
    index_expression.type = minic_type_unsigned_long();
    index_expression.value_category = MINIC_VALUE_RVALUE;
    index_expression.value.integer_value = (int64_t)index;
    if (!minic_parser_add_expression(parser, &index_expression, &index_id)) {
        return false;
    }

    (void)memset(&subscript, 0, sizeof(subscript));
    subscript.kind = MINIC_EXPRESSION_SUBSCRIPT;
    subscript.span.begin = base_span.begin;
    subscript.span.end = value_span.end;
    subscript.type = element_type;
    subscript.value_category = MINIC_VALUE_LVALUE;
    subscript.value.subscript.base = base_id;
    subscript.value.subscript.index = index_id;
    if (!minic_parser_add_expression(parser, &subscript, &target_id)) {
        return false;
    }

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_ASSIGN;
    statement.span = subscript.span;
    statement.target_expression = target_id;
    statement.expression = value_id;
    statement.target_statement = MINIC_STATEMENT_INVALID;
    statement.cleanup_context = parser->cleanup_context;
    statement.cleanup_stop_context = MINIC_CLEANUP_CONTEXT_ROOT;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;
    return minic_parser_add_statement(parser, &statement);
}

static bool add_local_array_element_assignment(MinicParser *parser,
                                               MinicLocalId local_id,
                                               size_t index,
                                               MinicExpressionId value_id) {
    const MinicLocal *local;
    MinicExpressionId base_id;

    local = minic_c0_program_local(parser->program, local_id);
    if (local == NULL || !local->is_array) {
        minic_parser_error(parser, "invalid local array initializer element");
        return false;
    }
    if (!add_local_lvalue_expression(parser, local_id, local->name_span, &base_id)) {
        return false;
    }
    return add_array_object_element_assignment(parser, base_id, index, value_id);
}

''',
)

# Shared fixed-array brace initializer for array-valued record members.
marker = '''static bool
parse_local_array_initializer(MinicParser *parser, MinicLocalId local_id, bool infer_count) {
'''
addition = '''static bool add_array_object_zero_elements(MinicParser *parser,
                                           MinicExpressionId base_id,
                                           size_t element_count,
                                           MinicSourceSpan initializer_span) {
    size_t index;

    for (index = 0U; index < element_count; ++index) {
        MinicExpression zero;
        MinicExpressionId zero_id;

        (void)memset(&zero, 0, sizeof(zero));
        zero.kind = MINIC_EXPRESSION_INTEGER;
        zero.span = initializer_span;
        zero.type = minic_type_int();
        zero.value_category = MINIC_VALUE_RVALUE;
        zero.value.integer_value = 0;
        if (!minic_parser_add_expression(parser, &zero, &zero_id) ||
            !add_array_object_element_assignment(parser, base_id, index, zero_id)) {
            return false;
        }
    }
    return true;
}

static bool parse_fixed_runtime_array_initializer(MinicParser *parser,
                                                  MinicExpressionId base_id,
                                                  size_t element_count) {
    MinicSourceSpan initializer_span;
    size_t initializer_count;

    if (parser == NULL || element_count == 0U || parser->current.kind != MINIC_TOKEN_LBRACE) {
        minic_parser_error(parser, "fixed runtime array initializer requires a nonempty array type");
        return false;
    }
    initializer_span.begin = parser->current.span.begin;
    if (!minic_parser_advance(parser)) {
        return false;
    }

    initializer_count = 0U;
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        MinicExpressionId value_id;

        if (initializer_count >= element_count) {
            minic_parser_error(parser, "too many runtime array initializer elements");
            return false;
        }
        if (!minic_parser_parse_expression(parser, &value_id, 0U) ||
            !add_array_object_element_assignment(parser, base_id, initializer_count, value_id)) {
            return false;
        }
        initializer_count += 1U;
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
            continue;
        }
        if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in runtime array initializer");
            return false;
        }
    }
    initializer_span.end = parser->current.span.end;
    while (initializer_count < element_count) {
        MinicExpression zero;
        MinicExpressionId zero_id;

        (void)memset(&zero, 0, sizeof(zero));
        zero.kind = MINIC_EXPRESSION_INTEGER;
        zero.span = initializer_span;
        zero.type = minic_type_int();
        zero.value_category = MINIC_VALUE_RVALUE;
        zero.value.integer_value = 0;
        if (!minic_parser_add_expression(parser, &zero, &zero_id) ||
            !add_array_object_element_assignment(
                parser, base_id, initializer_count, zero_id)) {
            return false;
        }
        initializer_count += 1U;
    }
    return minic_parser_advance(parser);
}

static bool
parse_local_array_initializer(MinicParser *parser, MinicLocalId local_id, bool infer_count) {
'''
replace_once(path, marker, addition)

# Aggregate-zero record initialization: fixed array members are semantic arrays, not unsupported fields.
replace_once(
    path,
    '''        field = minic_c0_record_field(record, field_index);
        if (field == NULL || field->element_count != 1U) {
            minic_parser_error(parser,
                               "record array members in aggregate initialization are unsupported");
            return false;
        }
''',
    '''        field = minic_c0_record_field(record, field_index);
        if (field == NULL || field->is_flexible_array) {
            minic_parser_error(parser,
                               "flexible array members in aggregate initialization are unsupported");
            return false;
        }
''',
)
replace_once(
    path,
    '''        if (minic_type_is_record(field->type)) {
            if (!add_zero_initialized_record_lvalue(parser, member_id, initializer_span)) {
                return false;
            }
        } else if (!add_zero_assignment_to_lvalue(parser, member_id, initializer_span)) {
            return false;
        }
''',
    '''        if (field->is_array) {
            if (!add_array_object_zero_elements(
                    parser, member_id, field->element_count, initializer_span)) {
                return false;
            }
        } else if (minic_type_is_record(field->type)) {
            if (!add_zero_initialized_record_lvalue(parser, member_id, initializer_span)) {
                return false;
            }
        } else if (!add_zero_assignment_to_lvalue(parser, member_id, initializer_span)) {
            return false;
        }
''',
)

# The member lvalue can represent a fixed array through existing array-object metadata.
replace_once(
    path,
    '''    if (target == NULL || record == NULL || field == NULL ||
        target->value_category != MINIC_VALUE_LVALUE || !minic_type_is_record(target->type) ||
        target->type.record_id != record_id || field->element_count != 1U ||
        field->is_flexible_array) {
''',
    '''    if (target == NULL || record == NULL || field == NULL ||
        target->value_category != MINIC_VALUE_LVALUE || !minic_type_is_record(target->type) ||
        target->type.record_id != record_id || field->is_flexible_array) {
''',
)

# Positional record initializer dispatches array fields to the shared fixed-array initializer.
replace_once(
    path,
    '''        field = minic_c0_record_field(record, field_index);
        if (field == NULL || field->element_count != 1U || field->is_flexible_array) {
            minic_parser_error(parser, "unsupported positional record initializer field");
            return false;
        }
''',
    '''        field = minic_c0_record_field(record, field_index);
        if (field == NULL || field->is_flexible_array) {
            minic_parser_error(parser, "unsupported positional record initializer field");
            return false;
        }
''',
)
replace_once(
    path,
    '''        if (parser->current.kind == MINIC_TOKEN_LBRACE && minic_type_is_record(field->type)) {
            if (!minic_parser_parse_runtime_record_initializer(parser, member_id)) {
                return false;
            }
        } else {
            MinicExpressionId value_id;

            if (!minic_parser_parse_expression(parser, &value_id, 0U) ||
                !add_runtime_record_member_assignment(parser, member_id, value_id)) {
                return false;
            }
        }
''',
    '''        if (parser->current.kind == MINIC_TOKEN_LBRACE && field->is_array) {
            if (!parse_fixed_runtime_array_initializer(parser, member_id, field->element_count)) {
                return false;
            }
        } else if (parser->current.kind == MINIC_TOKEN_LBRACE &&
                   minic_type_is_record(field->type)) {
            if (!minic_parser_parse_runtime_record_initializer(parser, member_id)) {
                return false;
            }
        } else {
            MinicExpressionId value_id;

            if (field->is_array) {
                minic_parser_error(parser, "runtime record array field initializer requires braces");
                return false;
            }
            if (!minic_parser_parse_expression(parser, &value_id, 0U) ||
                !add_runtime_record_member_assignment(parser, member_id, value_id)) {
                return false;
            }
        }
''',
)
replace_once(
    path,
    '''        if (minic_type_is_record(record->fields[field_index].type)) {
            if (!add_zero_initialized_record_lvalue(parser, member_id, initializer_span)) {
                return false;
            }
        } else if (!add_zero_assignment_to_lvalue(parser, member_id, initializer_span)) {
            return false;
        }
''',
    '''        if (record->fields[field_index].is_array) {
            if (!add_array_object_zero_elements(parser,
                                                member_id,
                                                record->fields[field_index].element_count,
                                                initializer_span)) {
                return false;
            }
        } else if (minic_type_is_record(record->fields[field_index].type)) {
            if (!add_zero_initialized_record_lvalue(parser, member_id, initializer_span)) {
                return false;
            }
        } else if (!add_zero_assignment_to_lvalue(parser, member_id, initializer_span)) {
            return false;
        }
''',
)

# Designated record array members use the same semantic array-object adapter.
replace_once(
    path,
    '''        MinicExpressionId member_id;
        const MinicExpression *member;
        MinicType member_type;
''',
    '''        MinicExpressionId member_id;
        const MinicExpression *member;
        MinicArrayObjectInfo member_array;
        MinicType member_type;
        bool member_is_array;
''',
)
replace_once(
    path,
    '''        member_type = member->type;
        if (parser->current.kind == MINIC_TOKEN_LBRACE && minic_type_is_record(member_type)) {
            if (!minic_parser_parse_runtime_record_initializer(parser, member_id)) {
                return false;
            }
        } else {
            MinicExpressionId value_id;

            if (!minic_parser_parse_expression(parser, &value_id, 0U) ||
                !add_runtime_record_member_assignment(parser, member_id, value_id)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                    minic_parser_error(parser, "record designated initializer type mismatch");
                }
                return false;
            }
        }
''',
    '''        member_type = member->type;
        member_is_array =
            minic_c0_expression_array_object_info(parser->program, member, &member_array);
        if (parser->current.kind == MINIC_TOKEN_LBRACE && member_is_array) {
            if (member_array.is_incomplete || member_array.is_zero_length ||
                !parse_fixed_runtime_array_initializer(
                    parser, member_id, member_array.element_count)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                    minic_parser_error(parser, "unsupported designated record array initializer");
                }
                return false;
            }
        } else if (parser->current.kind == MINIC_TOKEN_LBRACE &&
                   minic_type_is_record(member_type)) {
            if (!minic_parser_parse_runtime_record_initializer(parser, member_id)) {
                return false;
            }
        } else {
            MinicExpressionId value_id;

            if (member_is_array) {
                minic_parser_error(parser, "runtime record array field initializer requires braces");
                return false;
            }
            if (!minic_parser_parse_expression(parser, &value_id, 0U) ||
                !add_runtime_record_member_assignment(parser, member_id, value_id)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                    minic_parser_error(parser, "record designated initializer type mismatch");
                }
                return false;
            }
        }
''',
)

# Permanent GCC/MiniC execution differential: exact nested array-member compound literal shape,
# designated array member, array[1] identity, and zero-fill of omitted elements/fields.
Path("tests/programs/c0/runtime_record_array_initializer.c").write_text(r'''typedef unsigned char u8;

typedef struct {
    u8 b[16];
} guid_t;

typedef struct {
    u8 one[1];
    u8 bytes[4];
    int tail;
} packet_t;

static int guid_score(const guid_t *guid)
{
    return guid->b[0] + guid->b[1] + guid->b[15];
}

static int packet_score(const packet_t *packet)
{
    return packet->one[0] + packet->bytes[0] + packet->bytes[1] +
           packet->bytes[2] + packet->bytes[3] + packet->tail;
}

int main(void)
{
    int score;
    packet_t positional;
    packet_t designated;

    score = guid_score(&(guid_t){ { 0x61 & 0xff, (0x61 >> 1) & 0xff, 3, 4, 5, 6, 7, 8,
                                   9, 10, 11, 12, 13, 14, 15, 16 } });
    positional = (packet_t){ { 7 }, { 2, 3 }, 5 };
    designated = (packet_t){ .one = { 9 }, .bytes = { 4, 5, 6 }, .tail = 8 };
    score += packet_score(&positional);
    score += packet_score(&designated);
    return score;
}
''')
replace_once(
    "tests/programs/c0/manifest.txt",
    "continue_control_flow\n",
    "continue_control_flow\nruntime_record_array_initializer\n",
)

# Focused compile gate carries the exact Linux 16-byte nested aggregate and fail-closed boundary.
Path("tests/compiler/c0/runtime_record_array_initializer.c").write_text(r'''typedef unsigned char u8;
typedef struct { u8 b[16]; } guid_t;

static int consume_guid(guid_t *guid)
{
    return guid->b[0] + guid->b[15];
}

int linux_guid_compound_literal(void)
{
    return consume_guid(&(guid_t){ { (0x8be4df61) & 0xff,
                                    ((0x8be4df61) >> 8) & 0xff,
                                    ((0x8be4df61) >> 16) & 0xff,
                                    ((0x8be4df61) >> 24) & 0xff,
                                    (0x93ca) & 0xff,
                                    ((0x93ca) >> 8) & 0xff,
                                    (0x11d2) & 0xff,
                                    ((0x11d2) >> 8) & 0xff,
                                    0xaa, 0x0d, 0x00, 0xe0,
                                    0x98, 0x03, 0x2b, 0x8c } });
}
''')
Path("tests/compiler/c0/invalid_record_array_brace_elision.c").write_text(r'''struct packet { int values[2]; int tail; };

int invalid_record_array_brace_elision(void)
{
    struct packet packet = { 1, 2, 3 };
    return packet.tail;
}
''')
Path("tests/compiler/c0/run-runtime-record-array-initializers.sh").write_text(r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-runtime-record-array-initializers

mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/runtime_record_array_initializer.c" \
    -o "$work/runtime_record_array_initializer.i"
"$minic" -S "$work/runtime_record_array_initializer.i" \
    -o "$work/runtime_record_array_initializer.s"
grep -F 'linux_guid_compound_literal:' "$work/runtime_record_array_initializer.s" >/dev/null
grep -F '  sb ' "$work/runtime_record_array_initializer.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/runtime_record_array_initializer compound-literal=1 fixed-array-field=16 scalar-elements=1'

"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/invalid_record_array_brace_elision.c" \
    -o "$work/invalid_record_array_brace_elision.i"
if "$minic" -S "$work/invalid_record_array_brace_elision.i" \
    -o "$work/invalid_record_array_brace_elision.s" \
    >"$work/invalid.stdout" 2>"$work/invalid.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/invalid_record_array_brace_elision: unexpectedly succeeded' >&2
    exit 1
fi
grep -F 'runtime record array field initializer requires braces' \
    "$work/invalid.stderr" >/dev/null
printf '%s\n' 'PASS compiler/c0/invalid_record_array_brace_elision fail-closed=1'
''')

# Promote the focused seam to Compiler C0 alongside other semantic frontiers.
replace_once(
    ".github/scripts/compiler-c0-full-gate.sh",
    '''wide_string_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    HOST_CC=cc \\
    BUILD_DIR="$root/build/ci-wide-string" \\
        sh tests/compiler/c0/run-wide-string-literal.sh
}

''',
    '''wide_string_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    HOST_CC=cc \\
    BUILD_DIR="$root/build/ci-wide-string" \\
        sh tests/compiler/c0/run-wide-string-literal.sh
}

runtime_record_array_initializer_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    HOST_CC=cc \\
    BUILD_DIR="$root/build/ci-runtime-record-array-initializer" \\
        sh tests/compiler/c0/run-runtime-record-array-initializers.sh
}

''',
)
replace_once(
    ".github/scripts/compiler-c0-full-gate.sh",
    "    'Phase 2: focused declaration/static-local/variadic-call/pointer-equality/switch/wide-string/linenoise/SDS/RV64 suites, differential programs, tiny-AES, and cJSON'\n",
    "    'Phase 2: focused declaration/static-local/variadic-call/pointer-equality/switch/wide-string/record-array-init/linenoise/SDS/RV64 suites, differential programs, tiny-AES, and cJSON'\n",
)
replace_once(
    ".github/scripts/compiler-c0-full-gate.sh",
    "start_gate wide-string-focused wide_string_focused\nstart_gate linenoise-driven-focused linenoise_driven_focused\n",
    "start_gate wide-string-focused wide_string_focused\n"
    "start_gate record-array-init-focused runtime_record_array_initializer_focused\n"
    "start_gate linenoise-driven-focused linenoise_driven_focused\n",
)
