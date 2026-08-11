from pathlib import Path

root = Path(__file__).resolve().parents[2]

# Shared parser-internal promoted-member path contract.
internal = root / "src/frontend/parser_internal.h"
text = internal.read_text()
anchor = '''#define MINIC_MAX_PARSED_ATTRIBUTES 32U
'''
insert = '''#define MINIC_MAX_PARSED_ATTRIBUTES 32U
#define MINIC_RECORD_MEMBER_MAX_DEPTH 8U

typedef struct MinicRecordFieldPath {
    MinicRecordId record_ids[MINIC_RECORD_MEMBER_MAX_DEPTH];
    size_t field_indices[MINIC_RECORD_MEMBER_MAX_DEPTH];
    size_t depth;
    bool found;
    bool ambiguous;
} MinicRecordFieldPath;
'''
if text.count(anchor) != 1:
    raise SystemExit("parser-internal member path anchor mismatch")
text = text.replace(anchor, insert, 1)
proto_anchor = '''bool minic_parser_parse_enum_specifier(MinicParser *parser, MinicType *enum_type);
void minic_parser_destroy_enum_constants(MinicParser *parser);
'''
proto_insert = '''bool minic_parser_parse_enum_specifier(MinicParser *parser, MinicType *enum_type);
bool minic_parser_find_record_field_path(const MinicParser *parser,
                                         const MinicRecord *record,
                                         MinicSourceSpan name_span,
                                         MinicRecordFieldPath *result);
void minic_parser_destroy_enum_constants(MinicParser *parser);
'''
if text.count(proto_anchor) != 1:
    raise SystemExit("parser-internal member path prototype anchor mismatch")
internal.write_text(text.replace(proto_anchor, proto_insert, 1))

# Move existing promoted-member search to the shared parser-internal contract.
member = root / "src/frontend/parser_member.c"
text = member.read_text()
start = text.find("#define MINIC_ANONYMOUS_MEMBER_MAX_DEPTH")
end = text.find("static bool add_pointer_record_field(", start)
if start < 0 or end < 0:
    raise SystemExit("parser member private path boundaries missing")
shared = r'''static void search_record_field_path(const MinicParser *parser,
                                     const MinicRecord *record,
                                     MinicSourceSpan name_span,
                                     MinicRecordId *record_stack,
                                     size_t *field_stack,
                                     size_t depth,
                                     MinicRecordFieldPath *result) {
    size_t name_length;
    size_t index;

    if (parser == NULL || record == NULL || result == NULL || result->ambiguous ||
        depth >= MINIC_RECORD_MEMBER_MAX_DEPTH) {
        return;
    }
    name_length = minic_parser_span_length(name_span);
    for (index = 0U; index < record->field_count; ++index) {
        const MinicRecordField *field;

        field = minic_c0_record_field(record, index);
        if (field == NULL) {
            continue;
        }
        record_stack[depth] = (MinicRecordId)(record - parser->program->records);
        field_stack[depth] = index;
        if (!field->is_anonymous_member && field->name_length == name_length &&
            memcmp(field->name, parser->source + name_span.begin.offset, name_length) == 0) {
            if (result->found) {
                result->ambiguous = true;
                return;
            }
            result->depth = depth + 1U;
            (void)memcpy(
                result->record_ids, record_stack, result->depth * sizeof(result->record_ids[0]));
            (void)memcpy(result->field_indices,
                         field_stack,
                         result->depth * sizeof(result->field_indices[0]));
            result->found = true;
            continue;
        }
        if (field->is_anonymous_member && minic_type_is_record(field->type)) {
            const MinicRecord *nested;

            nested = minic_c0_program_record(parser->program, field->type.record_id);
            if (nested != NULL && nested->is_complete) {
                search_record_field_path(
                    parser, nested, name_span, record_stack, field_stack, depth + 1U, result);
            }
        }
    }
}

bool minic_parser_find_record_field_path(const MinicParser *parser,
                                         const MinicRecord *record,
                                         MinicSourceSpan name_span,
                                         MinicRecordFieldPath *result) {
    MinicRecordId record_stack[MINIC_RECORD_MEMBER_MAX_DEPTH];
    size_t field_stack[MINIC_RECORD_MEMBER_MAX_DEPTH];

    if (parser == NULL || record == NULL || result == NULL) {
        return false;
    }
    (void)memset(result, 0, sizeof(*result));
    (void)memset(record_stack, 0, sizeof(record_stack));
    (void)memset(field_stack, 0, sizeof(field_stack));
    search_record_field_path(parser, record, name_span, record_stack, field_stack, 0U, result);
    return result->found && !result->ambiguous;
}

'''
text = text[:start] + shared + text[end:]
text = text.replace("find_record_field_path(parser, record, field_span, &path)",
                    "minic_parser_find_record_field_path(parser, record, field_span, &path)")
member.write_text(text)

# Offsetof semantic node keeps only the anonymous-prefix offset plus final field identity.
ast = root / "src/frontend/ast.h"
text = ast.read_text()
old = '''        struct {
            MinicRecordId record_id;
            size_t field_index;
        } offsetof_value;
'''
new = '''        struct {
            MinicRecordId record_id;
            size_t field_index;
            size_t anonymous_prefix_offset;
        } offsetof_value;
'''
if text.count(old) != 1:
    raise SystemExit("offsetof AST payload anchor mismatch")
ast.write_text(text.replace(old, new, 1))

# Parse offsetof member names through the exact same promoted-member resolver.
expr = root / "src/frontend/parser_expression.c"
text = expr.read_text()
start = text.find("static bool parse_builtin_offsetof(")
end = text.find("static bool generic_token_text_equals(", start)
if start < 0 or end < 0:
    raise SystemExit("offsetof parser boundaries missing")
replacement = r'''static bool parse_builtin_offsetof(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicExpression expression;
    MinicSourcePosition begin;
    MinicSourceSpan field_span;
    MinicType record_type;
    const MinicRecord *record;
    MinicRecordFieldPath path;
    size_t anonymous_prefix_offset;
    size_t path_index;

    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __builtin_offsetof") ||
        !minic_parser_parse_type_name(parser, &record_type)) {
        return false;
    }
    if (!minic_type_is_record(record_type)) {
        minic_parser_error(parser, "__builtin_offsetof requires a record type");
        return false;
    }
    record = minic_c0_program_record(parser->program, record_type.record_id);
    if (record == NULL || !record->is_complete) {
        minic_parser_error(parser, "__builtin_offsetof requires a complete record type");
        return false;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_COMMA, "expected ',' in __builtin_offsetof") ||
        parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "expected record field in __builtin_offsetof");
        }
        return false;
    }
    field_span = parser->current.span;
    if (!minic_parser_find_record_field_path(parser, record, field_span, &path)) {
        minic_parser_error(
            parser,
            path.ambiguous ? "record field is ambiguous in __builtin_offsetof"
                           : "record has no such field in __builtin_offsetof");
        return false;
    }
    if (path.depth == 0U) {
        minic_parser_error(parser, "empty record field path in __builtin_offsetof");
        return false;
    }

    anonymous_prefix_offset = 0U;
    for (path_index = 0U; path_index + 1U < path.depth; ++path_index) {
        const MinicRecord *path_record;
        size_t field_offset;

        path_record = minic_c0_program_record(parser->program, path.record_ids[path_index]);
        if (path_record == NULL ||
            !minic_data_layout_record_field_offset(
                minic_target_info_data_layout(parser->target_info),
                parser->program,
                path_record,
                path.field_indices[path_index],
                &field_offset) ||
            anonymous_prefix_offset > SIZE_MAX - field_offset) {
            minic_parser_error(parser, "cannot lay out anonymous member path in __builtin_offsetof");
            return false;
        }
        anonymous_prefix_offset += field_offset;
    }
    if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_RPAREN) {
        minic_parser_error(parser, "expected ')' after __builtin_offsetof");
        return false;
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_OFFSETOF;
    expression.span.begin = begin;
    expression.span.end = parser->current.span.end;
    expression.type = minic_type_unsigned_long();
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.offsetof_value.record_id = path.record_ids[path.depth - 1U];
    expression.value.offsetof_value.field_index = path.field_indices[path.depth - 1U];
    expression.value.offsetof_value.anonymous_prefix_offset = anonymous_prefix_offset;
    return minic_parser_advance(parser) &&
           minic_parser_add_expression(parser, &expression, expression_id);
}

'''
expr.write_text(text[:start] + replacement + text[end:])

# Typed ConstEval adds the prefix to the canonical final-field DataLayout query.
consteval = root / "src/frontend/const_eval.c"
text = consteval.read_text()
old = '''        if (record == NULL ||
            !minic_data_layout_record_field_offset(minic_target_info_data_layout(target),
                                                   program,
                                                   record,
                                                   expression->value.offsetof_value.field_index,
                                                   &offset)) {
            return false;
        }
        value->type = expression->type;
        return normalize_bits(program, target, expression->type, (uint64_t)offset, &value->bits);
'''
new = '''        if (record == NULL ||
            !minic_data_layout_record_field_offset(minic_target_info_data_layout(target),
                                                   program,
                                                   record,
                                                   expression->value.offsetof_value.field_index,
                                                   &offset) ||
            expression->value.offsetof_value.anonymous_prefix_offset > SIZE_MAX - offset) {
            return false;
        }
        offset += expression->value.offsetof_value.anonymous_prefix_offset;
        value->type = expression->type;
        return normalize_bits(program, target, expression->type, (uint64_t)offset, &value->bits);
'''
if text.count(old) != 1:
    raise SystemExit("offsetof ConstEval anchor mismatch")
consteval.write_text(text.replace(old, new, 1))

# Runtime RV64 expression emission uses materialized final field layout plus the same prefix.
codegen = root / "src/target/riscv64/codegen_expression.c"
text = codegen.read_text()
old = '''        record = minic_c0_program_record(program, expression->value.offsetof_value.record_id);
        field = minic_c0_record_field(record, expression->value.offsetof_value.field_index);
        return record != NULL && field != NULL && record->is_complete &&
               minic_type_equal(expression->type, minic_type_unsigned_long()) &&
               fprintf(file, "  li a0, %zu\\n", field->storage_offset) >= 0;
'''
new = '''        size_t offset;

        record = minic_c0_program_record(program, expression->value.offsetof_value.record_id);
        field = minic_c0_record_field(record, expression->value.offsetof_value.field_index);
        if (record == NULL || field == NULL || !record->is_complete ||
            !minic_type_equal(expression->type, minic_type_unsigned_long()) ||
            expression->value.offsetof_value.anonymous_prefix_offset >
                SIZE_MAX - field->storage_offset) {
            return false;
        }
        offset = expression->value.offsetof_value.anonymous_prefix_offset + field->storage_offset;
        return fprintf(file, "  li a0, %zu\\n", offset) >= 0;
'''
if text.count(old) != 1:
    raise SystemExit("offsetof RV64 emission anchor mismatch")
codegen.write_text(text.replace(old, new, 1))

# Strengthen the existing offsetof contract with the already-supported two-level anonymous path.
fixture = root / "tests/compiler/c0/builtin_offsetof.c"
fixture.write_text(r'''struct Sample {
    char first;
    int value;
    unsigned short tail;
};

typedef struct Sample Sample;

struct BranchData {
    const char *func;
    const char *file;
    unsigned line;
    union {
        struct {
            unsigned long correct;
            unsigned long incorrect;
        };
        struct {
            unsigned long miss;
            unsigned long hit;
        };
        unsigned long miss_hit[2];
    };
};

_Static_assert(__builtin_offsetof(struct BranchData, hit) == 32,
               "promoted anonymous member offsetof");
_Static_assert(__builtin_offsetof(struct BranchData, miss_hit) == 24,
               "promoted anonymous array member offsetof");

int main(void) {
    char padding[__builtin_offsetof(Sample, tail)];

    return sizeof(padding) == 8 && __builtin_offsetof(Sample, value) == 4 &&
                   __builtin_offsetof(struct Sample, tail) == 8 &&
                   __builtin_offsetof(struct BranchData, hit) == 32 &&
                   __builtin_offsetof(struct BranchData, miss_hit) == 24
               ? 0
               : 1;
}
''')

runner = root / "tests/compiler/c0/run-builtin-offsetof.sh"
text = runner.read_text()
old = '''printf '%s\\n' 'PASS compiler/c0/builtin_offsetof direct-member=1 typedef=1 target-layout=1 array-bound=8'\n'''
new = '''grep -F '  li a0, 32' "$work/builtin_offsetof.s" >/dev/null\ngrep -F '  li a0, 24' "$work/builtin_offsetof.s" >/dev/null\nprintf '%s\\n' 'PASS compiler/c0/builtin_offsetof direct-member=1 typedef=1 promoted-anonymous=2 shared-member-resolver=1 target-layout=1 array-bound=8'\n'''
if text.count(old) != 1:
    raise SystemExit("offsetof runner summary anchor mismatch")
runner.write_text(text.replace(old, new, 1))

print("PASS generated canonical promoted-member offsetof slice")
