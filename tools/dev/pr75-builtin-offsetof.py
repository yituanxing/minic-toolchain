#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:140]!r}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/ast.h",
    """    MINIC_EXPRESSION_FUNCTION,
    MINIC_EXPRESSION_SIZEOF,
    MINIC_EXPRESSION_ADDRESS_OF,
""",
    """    MINIC_EXPRESSION_FUNCTION,
    MINIC_EXPRESSION_SIZEOF,
    MINIC_EXPRESSION_OFFSETOF,
    MINIC_EXPRESSION_ADDRESS_OF,
""",
)

replace_once(
    "src/frontend/ast.h",
    """        MinicType sizeof_type;
        struct {
            MinicFunctionId function_id;
""",
    """        MinicType sizeof_type;
        struct {
            MinicRecordId record_id;
            size_t field_index;
        } offsetof_value;
        struct {
            MinicFunctionId function_id;
""",
)

marker = "static bool parse_primary(MinicParser *parser,\n"
parser_path = Path("src/frontend/parser_expression.c")
text = parser_path.read_text()
if text.count(marker) != 1:
    raise SystemExit("unexpected parse_primary marker")
helper = r'''static bool current_is_builtin_offsetof(const MinicParser *parser) {
    static const char name[] = "__builtin_offsetof";
    size_t length;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        return false;
    }
    length = minic_parser_span_length(parser->current.span);
    return length == sizeof(name) - 1U &&
           memcmp(parser->source + parser->current.span.begin.offset, name, length) == 0;
}

static bool parse_builtin_offsetof(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicExpression expression;
    MinicSourcePosition begin;
    MinicSourceSpan field_span;
    MinicType record_type;
    const MinicRecord *record;
    size_t field_index;
    size_t field_name_length;

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
    field_name_length = minic_parser_span_length(field_span);
    field_index = 0U;
    while (field_index < record->field_count) {
        const MinicRecordField *field;

        field = &record->fields[field_index];
        if (field->name_length == field_name_length &&
            memcmp(field->name,
                   parser->source + field_span.begin.offset,
                   field_name_length) == 0) {
            break;
        }
        field_index += 1U;
    }
    if (field_index == record->field_count) {
        minic_parser_error(parser, "record has no such field in __builtin_offsetof");
        return false;
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
    expression.value.offsetof_value.record_id = record_type.record_id;
    expression.value.offsetof_value.field_index = field_index;
    return minic_parser_advance(parser) &&
           minic_parser_add_expression(parser, &expression, expression_id);
}

'''
parser_path.write_text(text.replace(marker, helper + marker, 1))

replace_once(
    "src/frontend/parser_expression.c",
    """    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        name_span = parser->current.span;
""",
    """    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        if (current_is_builtin_offsetof(parser)) {
            return parse_builtin_offsetof(parser, expression_id);
        }
        name_span = parser->current.span;
""",
)

replace_once(
    "src/frontend/cast_normalization.c",
    """    case MINIC_EXPRESSION_FUNCTION:
    case MINIC_EXPRESSION_SIZEOF:
        return true;
""",
    """    case MINIC_EXPRESSION_FUNCTION:
    case MINIC_EXPRESSION_SIZEOF:
    case MINIC_EXPRESSION_OFFSETOF:
        return true;
""",
)

replace_once(
    "src/frontend/ast_verifier.c",
    """    case MINIC_EXPRESSION_SIZEOF:
        return expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_equal(expression->type, minic_type_unsigned_long()) &&
               type_is_valid(program, expression->value.sizeof_type) &&
               type_is_complete_object(program, expression->value.sizeof_type);
    case MINIC_EXPRESSION_ADDRESS_OF: {
""",
    """    case MINIC_EXPRESSION_SIZEOF:
        return expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_equal(expression->type, minic_type_unsigned_long()) &&
               type_is_valid(program, expression->value.sizeof_type) &&
               type_is_complete_object(program, expression->value.sizeof_type);
    case MINIC_EXPRESSION_OFFSETOF: {
        const MinicRecord *record;

        record = minic_c0_program_record(program, expression->value.offsetof_value.record_id);
        return record != NULL && record->is_complete &&
               expression->value.offsetof_value.field_index < record->field_count &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_equal(expression->type, minic_type_unsigned_long());
    }
    case MINIC_EXPRESSION_ADDRESS_OF: {
""",
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    """    case MINIC_EXPRESSION_SIZEOF: {
        MinicType measured_type;
        size_t alignment;
        size_t size;

        measured_type = expression->value.sizeof_type;
        if (!minic_type_equal(expression->type, minic_type_unsigned_long()) ||
            !minic_riscv64_type_layout(program, measured_type, &size, &alignment)) {
            return false;
        }
        return fprintf(file, \"  li a0, %zu\\n\", size) >= 0;
    }
    case MINIC_EXPRESSION_CAST:
""",
    """    case MINIC_EXPRESSION_SIZEOF: {
        MinicType measured_type;
        size_t alignment;
        size_t size;

        measured_type = expression->value.sizeof_type;
        if (!minic_type_equal(expression->type, minic_type_unsigned_long()) ||
            !minic_riscv64_type_layout(program, measured_type, &size, &alignment)) {
            return false;
        }
        return fprintf(file, \"  li a0, %zu\\n\", size) >= 0;
    }
    case MINIC_EXPRESSION_OFFSETOF: {
        const MinicRecord *record;
        const MinicRecordField *field;

        record = minic_c0_program_record(program, expression->value.offsetof_value.record_id);
        field = minic_c0_record_field(record, expression->value.offsetof_value.field_index);
        return record != NULL && field != NULL && record->is_complete &&
               minic_type_equal(expression->type, minic_type_unsigned_long()) &&
               fprintf(file, \"  li a0, %zu\\n\", field->storage_offset) >= 0;
    }
    case MINIC_EXPRESSION_CAST:
""",
)

print("staged __builtin_offsetof for direct record members")
