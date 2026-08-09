#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:140]!r}")
    target.write_text(text.replace(old, new, 1))


# An anonymous struct/union member occupies normal record storage but contributes its nested
# member names to the containing record's member namespace.
replace_once(
    "src/frontend/ast.h",
    """    size_t storage_offset;
    bool is_array;
    bool is_flexible_array;
} MinicRecordField;
""",
    """    size_t storage_offset;
    bool is_array;
    bool is_flexible_array;
    bool is_anonymous_member;
} MinicRecordField;
""",
)

# Multiple anonymous members legitimately have empty internal names. Keep duplicate checking
# for actual named fields only.
replace_once(
    "src/frontend/ast.c",
    """        if (existing->name_length == name_length &&
            memcmp(existing->name, name, name_length) == 0) {
            return false;
        }
""",
    """        if (name_length != 0U && existing->name_length == name_length &&
            memcmp(existing->name, name, name_length) == 0) {
            return false;
        }
""",
)

# After the type specifier of a record field, a bare complete struct/union followed by ';'
# is a C11/GNU anonymous member rather than a missing declarator name.
replace_once(
    "src/frontend/parser_record.c",
    """    if (!minic_parser_parse_type_specifiers(parser, &base_type)) {
        return false;
    }

    for (;;) {
""",
    """    if (!minic_parser_parse_type_specifiers(parser, &base_type)) {
        return false;
    }
    if (minic_type_is_record(base_type) && parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        MinicRecord *mutable_record;

        if (!minic_parser_require_complete_object_type(
                parser, base_type, "anonymous record member requires a complete type") ||
            !minic_c0_record_add_field(parser->program, record_id, "", 0U, base_type, 1U)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot add anonymous record member");
            }
            return false;
        }
        mutable_record = &parser->program->records[record_id];
        mutable_record->fields[mutable_record->field_count - 1U].is_anonymous_member = true;
        return minic_parser_advance(parser);
    }

    for (;;) {
""",
)

# Resolve promoted members recursively, and lower the promoted access to an ordinary chain of
# member/address expressions. This preserves one canonical codegen/layout path.
member = Path("src/frontend/parser_member.c")
text = member.read_text()
start = text.find("static bool find_record_field(")
end = text.find("\nstatic bool parse_pointer_record_member(", start)
if start < 0 or end < 0:
    raise SystemExit("cannot locate record field lookup")
lookup = r'''#define MINIC_ANONYMOUS_MEMBER_MAX_DEPTH 8U

typedef struct MinicRecordFieldPath {
    MinicRecordId record_ids[MINIC_ANONYMOUS_MEMBER_MAX_DEPTH];
    size_t field_indices[MINIC_ANONYMOUS_MEMBER_MAX_DEPTH];
    size_t depth;
    bool found;
    bool ambiguous;
} MinicRecordFieldPath;

static void search_record_field_path(const MinicParser *parser,
                                     const MinicRecord *record,
                                     MinicSourceSpan name_span,
                                     MinicRecordId *record_stack,
                                     size_t *field_stack,
                                     size_t depth,
                                     MinicRecordFieldPath *result) {
    size_t name_length;
    size_t index;

    if (parser == NULL || record == NULL || result == NULL || result->ambiguous ||
        depth >= MINIC_ANONYMOUS_MEMBER_MAX_DEPTH) {
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
            (void)memcpy(result->record_ids,
                         record_stack,
                         result->depth * sizeof(result->record_ids[0]));
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
                search_record_field_path(parser,
                                         nested,
                                         name_span,
                                         record_stack,
                                         field_stack,
                                         depth + 1U,
                                         result);
            }
        }
    }
}

static bool find_record_field_path(const MinicParser *parser,
                                   const MinicRecord *record,
                                   MinicSourceSpan name_span,
                                   MinicRecordFieldPath *result) {
    MinicRecordId record_stack[MINIC_ANONYMOUS_MEMBER_MAX_DEPTH];
    size_t field_stack[MINIC_ANONYMOUS_MEMBER_MAX_DEPTH];

    if (parser == NULL || record == NULL || result == NULL) {
        return false;
    }
    (void)memset(result, 0, sizeof(*result));
    (void)memset(record_stack, 0, sizeof(record_stack));
    (void)memset(field_stack, 0, sizeof(field_stack));
    search_record_field_path(
        parser, record, name_span, record_stack, field_stack, 0U, result);
    return result->found && !result->ambiguous;
}

static bool add_pointer_record_field(MinicParser *parser,
                                     MinicExpressionId pointer_base_id,
                                     MinicRecordId record_id,
                                     size_t field_index,
                                     MinicSourcePosition member_begin,
                                     MinicSourceSpan field_span,
                                     MinicExpressionId *expression_id) {
    const MinicExpression *base;
    const MinicRecord *record;
    const MinicRecordField *field;
    MinicType record_type;
    MinicType member_type;
    MinicExpression member;

    base = minic_c0_program_expression(parser->program, pointer_base_id);
    record = minic_c0_program_record(parser->program, record_id);
    field = minic_c0_record_field(record, field_index);
    if (base == NULL || record == NULL || field == NULL ||
        !minic_type_pointee(base->type, &record_type) || !minic_type_is_record(record_type) ||
        record_type.record_id != record_id) {
        return false;
    }
    member_type = field->type;
    if (minic_type_is_const(record_type) && !minic_type_add_const(member_type, &member_type)) {
        minic_parser_error(parser, "cannot propagate const to record member");
        return false;
    }

    (void)memset(&member, 0, sizeof(member));
    member.kind = MINIC_EXPRESSION_MEMBER;
    member.span.begin = member_begin;
    member.span.end = field_span.end;
    member.value.member.base = pointer_base_id;
    member.value.member.record_id = record_id;
    member.value.member.field_index = field_index;
    if (field->is_array) {
        if (!minic_type_pointer_to(member_type, &member.type)) {
            minic_parser_error(parser, "record array member pointer depth is unsupported");
            return false;
        }
        member.value_category = MINIC_VALUE_RVALUE;
    } else {
        member.type = member_type;
        member.value_category = MINIC_VALUE_LVALUE;
    }
    return minic_parser_add_expression(parser, &member, expression_id);
}
'''
member.write_text(text[:start] + lookup + text[end:])

# Replace the direct-only field lookup block with path resolution and chained member lowering.
replace_once(
    "src/frontend/parser_member.c",
    """    const MinicRecordField *field;
    MinicType record_type;
    MinicType member_type;
    MinicSourceSpan field_span;
    MinicExpression member;
    size_t field_index;
""",
    """    MinicType record_type;
    MinicSourceSpan field_span;
    MinicRecordFieldPath path;
    MinicExpressionId current_pointer_id;
    size_t path_index;
""",
)
replace_once(
    "src/frontend/parser_member.c",
    """    field_span = parser->current.span;
    if (!find_record_field(parser, record, field_span, &field_index)) {
        minic_parser_error(parser, "record has no such member");
        return false;
    }
    field = minic_c0_record_field(record, field_index);
    if (field == NULL) {
        minic_parser_error(parser, "invalid record member");
        return false;
    }
    member_type = field->type;
    if (minic_type_is_const(record_type) && !minic_type_add_const(member_type, &member_type)) {
        minic_parser_error(parser, "cannot propagate const to record member");
        return false;
    }
    if (!minic_parser_advance(parser)) {
        return false;
    }

    (void)memset(&member, 0, sizeof(member));
    member.kind = MINIC_EXPRESSION_MEMBER;
    member.span.begin = member_begin;
    member.span.end = field_span.end;
    member.value.member.base = pointer_base_id;
    member.value.member.record_id = record_type.record_id;
    member.value.member.field_index = field_index;
    if (field->is_array) {
        if (!minic_type_pointer_to(member_type, &member.type)) {
            minic_parser_error(parser, "record array member pointer depth is unsupported");
            return false;
        }
        member.value_category = MINIC_VALUE_RVALUE;
    } else {
        member.type = member_type;
        member.value_category = MINIC_VALUE_LVALUE;
    }
    return minic_parser_add_expression(parser, &member, expression_id);
""",
    """    field_span = parser->current.span;
    if (!find_record_field_path(parser, record, field_span, &path)) {
        minic_parser_error(parser, path.ambiguous ? "record member is ambiguous through anonymous members"
                                                : "record has no such member");
        return false;
    }
    if (!minic_parser_advance(parser)) {
        return false;
    }

    current_pointer_id = pointer_base_id;
    for (path_index = 0U; path_index < path.depth; ++path_index) {
        MinicExpressionId member_id;

        if (!add_pointer_record_field(parser,
                                      current_pointer_id,
                                      path.record_ids[path_index],
                                      path.field_indices[path_index],
                                      member_begin,
                                      field_span,
                                      &member_id)) {
            return false;
        }
        if (path_index + 1U == path.depth) {
            *expression_id = member_id;
            return true;
        }
        {
            const MinicExpression *member_expression;
            MinicExpression address;

            member_expression = minic_c0_program_expression(parser->program, member_id);
            if (member_expression == NULL || member_expression->value_category != MINIC_VALUE_LVALUE ||
                !minic_type_is_record(member_expression->type)) {
                minic_parser_error(parser, "anonymous member path does not contain a record");
                return false;
            }
            (void)memset(&address, 0, sizeof(address));
            address.kind = MINIC_EXPRESSION_ADDRESS_OF;
            address.span = member_expression->span;
            if (!minic_type_pointer_to(member_expression->type, &address.type)) {
                minic_parser_error(parser, "anonymous member pointer depth is unsupported");
                return false;
            }
            address.value_category = MINIC_VALUE_RVALUE;
            address.value.unary.operand = member_id;
            if (!minic_parser_add_expression(parser, &address, &current_pointer_id)) {
                return false;
            }
        }
    }
    minic_parser_error(parser, "empty anonymous member path");
    return false;
""",
)

print("staged C11/GNU anonymous struct/union members with promoted member access")
