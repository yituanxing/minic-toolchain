#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one exact anchor, found {count}")
    p.write_text(text.replace(old, new, 1))


parser_path = "src/frontend/parser_member.c"
old_direct = r'''bool minic_parser_parse_direct_member(MinicParser *parser,
                                      MinicExpressionId base_id,
                                      MinicExpressionId *expression_id) {
    const MinicExpression *base;
    MinicExpression address;
    MinicExpressionId address_id;
    MinicSourcePosition member_begin;

    base = minic_c0_program_expression(parser->program, base_id);
    if (base == NULL || base->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_record(base->type)) {
        minic_parser_error(parser, "direct member access requires a record lvalue");
        return false;
    }
    member_begin = base->span.begin;
    if (!minic_parser_expect(parser, MINIC_TOKEN_DOT, "expected '.'")) {
        return false;
    }

    (void)memset(&address, 0, sizeof(address));
    address.kind = MINIC_EXPRESSION_ADDRESS_OF;
    address.span = base->span;
    if (!minic_type_pointer_to(base->type, &address.type)) {
        minic_parser_error(parser, "direct member address depth is unsupported");
        return false;
    }
    address.value_category = MINIC_VALUE_RVALUE;
    address.value.unary.operand = base_id;
    if (!minic_parser_add_expression(parser, &address, &address_id)) {
        return false;
    }
    return parse_pointer_record_member(parser, address_id, member_begin, expression_id);
}
'''
new_direct = r'''static bool parse_record_rvalue_member(MinicParser *parser,
                                       MinicExpressionId base_id,
                                       MinicSourcePosition member_begin,
                                       MinicExpressionId *expression_id) {
    const MinicExpression *base;
    const MinicRecord *record;
    const MinicRecordField *field;
    MinicRecordFieldPath path;
    MinicSourceSpan field_span;
    MinicExpression member;
    MinicType base_type;
    MinicType member_type;

    base = minic_c0_program_expression(parser->program, base_id);
    if (base == NULL || base->value_category != MINIC_VALUE_RVALUE ||
        !minic_type_is_record(base->type) ||
        !minic_c0_record_value_is_copy_source(parser->program, base_id)) {
        minic_parser_error(parser, "record rvalue member source is not materializable yet");
        return false;
    }
    base_type = base->type;
    record = minic_c0_program_record(parser->program, base_type.record_id);
    if (record == NULL || !record->is_complete) {
        minic_parser_error(parser, "member access requires a complete record");
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected record member name");
        return false;
    }
    field_span = parser->current.span;
    if (!minic_parser_find_record_field_path(parser, record, field_span, &path)) {
        minic_parser_error(parser,
                           path.ambiguous ? "record member is ambiguous through anonymous members"
                                          : "record has no such member");
        return false;
    }
    if (path.depth != 1U || path.record_ids[0] != base_type.record_id) {
        minic_parser_error(parser,
                           "promoted member access on a record rvalue is not supported yet");
        return false;
    }
    field = minic_c0_record_field(record, path.field_indices[0]);
    if (field == NULL || field->is_array || minic_type_is_record(field->type)) {
        minic_parser_error(parser,
                           "record rvalue member currently requires a scalar field");
        return false;
    }
    member_type = field->type;
    if (minic_type_is_const(base_type) && !minic_type_add_const(member_type, &member_type)) {
        minic_parser_error(parser, "cannot propagate const to record rvalue member");
        return false;
    }
    if (!minic_parser_advance(parser)) {
        return false;
    }

    (void)memset(&member, 0, sizeof(member));
    member.kind = MINIC_EXPRESSION_MEMBER;
    member.span.begin = member_begin;
    member.span.end = field_span.end;
    member.type = member_type;
    member.value_category = MINIC_VALUE_RVALUE;
    member.value.member.base = base_id;
    member.value.member.record_id = base_type.record_id;
    member.value.member.field_index = path.field_indices[0];
    return minic_parser_add_expression(parser, &member, expression_id);
}

bool minic_parser_parse_direct_member(MinicParser *parser,
                                      MinicExpressionId base_id,
                                      MinicExpressionId *expression_id) {
    const MinicExpression *base;
    MinicExpression address;
    MinicExpressionId address_id;
    MinicSourcePosition member_begin;

    base = minic_c0_program_expression(parser->program, base_id);
    if (base == NULL || !minic_type_is_record(base->type)) {
        minic_parser_error(parser, "direct member access requires a record expression");
        return false;
    }
    member_begin = base->span.begin;
    if (!minic_parser_expect(parser, MINIC_TOKEN_DOT, "expected '.'")) {
        return false;
    }
    if (base->value_category == MINIC_VALUE_RVALUE) {
        return parse_record_rvalue_member(parser, base_id, member_begin, expression_id);
    }
    if (base->value_category != MINIC_VALUE_LVALUE) {
        minic_parser_error(parser, "direct member access requires a record value");
        return false;
    }

    (void)memset(&address, 0, sizeof(address));
    address.kind = MINIC_EXPRESSION_ADDRESS_OF;
    address.span = base->span;
    if (!minic_type_pointer_to(base->type, &address.type)) {
        minic_parser_error(parser, "direct member address depth is unsupported");
        return false;
    }
    address.value_category = MINIC_VALUE_RVALUE;
    address.value.unary.operand = base_id;
    if (!minic_parser_add_expression(parser, &address, &address_id)) {
        return false;
    }
    return parse_pointer_record_member(parser, address_id, member_begin, expression_id);
}
'''
replace_once(parser_path, old_direct, new_direct, "record rvalue direct member parser")

verifier_path = "src/frontend/ast_verifier.c"
old_member_verify = r'''    case MINIC_EXPRESSION_MEMBER: {
        const MinicRecord *record;
        const MinicRecordField *field;
        MinicType record_type;
        MinicType expected_type;

        operand = expression_before(program, expression->value.member.base, expression_index);
        record = minic_c0_program_record(program, expression->value.member.record_id);
        field = minic_c0_record_field(record, expression->value.member.field_index);
        if (operand == NULL || record == NULL || field == NULL ||
            !minic_type_pointee(operand->type, &record_type) ||
            !minic_type_is_record(record_type) ||
            record_type.record_id != expression->value.member.record_id) {
            return false;
        }
        expected_type = field->type;
        if (minic_type_is_const(record_type) &&
            !minic_type_add_const(expected_type, &expected_type)) {
            return false;
        }
        return expression->value_category == MINIC_VALUE_LVALUE &&
               minic_type_equal(expression->type, expected_type);
    }
'''
new_member_verify = r'''    case MINIC_EXPRESSION_MEMBER: {
        const MinicRecord *record;
        const MinicRecordField *field;
        MinicType record_type;
        MinicType expected_type;
        bool record_rvalue_base;

        operand = expression_before(program, expression->value.member.base, expression_index);
        record = minic_c0_program_record(program, expression->value.member.record_id);
        field = minic_c0_record_field(record, expression->value.member.field_index);
        if (operand == NULL || record == NULL || field == NULL) {
            return false;
        }
        record_rvalue_base = operand->value_category == MINIC_VALUE_RVALUE &&
                             minic_type_is_record(operand->type) &&
                             operand->type.record_id == expression->value.member.record_id;
        if (record_rvalue_base) {
            record_type = operand->type;
            if (!minic_c0_record_value_is_copy_source(
                    program, expression->value.member.base) ||
                field->is_array || minic_type_is_record(field->type) ||
                expression->value_category != MINIC_VALUE_RVALUE) {
                return false;
            }
        } else if (!minic_type_pointee(operand->type, &record_type) ||
                   !minic_type_is_record(record_type) ||
                   record_type.record_id != expression->value.member.record_id ||
                   expression->value_category != MINIC_VALUE_LVALUE) {
            return false;
        }
        expected_type = field->type;
        if (minic_type_is_const(record_type) &&
            !minic_type_add_const(expected_type, &expected_type)) {
            return false;
        }
        return minic_type_equal(expression->type, expected_type);
    }
'''
replace_once(verifier_path, old_member_verify, new_member_verify, "member verifier value category split")

codegen_path = "src/target/riscv64/codegen_expression.c"
record_copy_anchor = r'''bool minic_riscv64_emit_record_copy_value(FILE *file,
'''
rvalue_helper = r'''static bool minic_riscv64_emit_record_rvalue_member(FILE *file,
                                                     const MinicC0Program *program,
                                                     const MinicFunction *function,
                                                     const MinicExpression *expression,
                                                     MinicExpressionId expression_id) {
    const MinicExpression *base;
    const MinicRecord *record;
    const MinicRecordField *field;
    size_t storage_size;
    size_t temporary_size;

    if (file == NULL || program == NULL || expression == NULL ||
        expression->kind != MINIC_EXPRESSION_MEMBER ||
        expression->value_category != MINIC_VALUE_RVALUE) {
        return false;
    }
    base = minic_c0_program_expression(program, expression->value.member.base);
    record = minic_c0_program_record(program, expression->value.member.record_id);
    field = minic_c0_record_field(record, expression->value.member.field_index);
    if (base == NULL || record == NULL || field == NULL || !record->is_complete ||
        base->value_category != MINIC_VALUE_RVALUE || !minic_type_is_record(base->type) ||
        base->type.record_id != expression->value.member.record_id ||
        !minic_c0_record_value_is_copy_source(program, expression->value.member.base) ||
        field->is_array || minic_type_is_record(field->type) || record->storage_size == 0U ||
        record->storage_size > SIZE_MAX - 15U) {
        return false;
    }
    storage_size = record->storage_size;
    temporary_size = (storage_size + 15U) & ~(size_t)15U;
    if (!minic_riscv64_emit_record_value_temporary(file,
                                                   program,
                                                   function,
                                                   expression->value.member.base,
                                                   storage_size,
                                                   temporary_size)) {
        return false;
    }
    if (field->storage_offset == 0U) {
        if (fprintf(file, "  mv a0, sp\n") < 0) {
            return false;
        }
    } else if (field->storage_offset <= 2047U) {
        if (fprintf(file, "  addi a0, sp, %zu\n", field->storage_offset) < 0) {
            return false;
        }
    } else if (fprintf(file,
                       "  li t0, %zu\n"
                       "  add a0, sp, t0\n",
                       field->storage_offset) < 0) {
        return false;
    }
    return minic_riscv64_emit_lvalue_load_from_address(
               file, program, expression_id, expression->type, "a0", "a0") &&
           minic_riscv64_emit_stack_release(file, temporary_size);
}

'''
replace_once(codegen_path, record_copy_anchor, rvalue_helper + record_copy_anchor,
             "record rvalue member lowering helper")

old_member_codegen = r'''    case MINIC_EXPRESSION_MEMBER: {
        const MinicRecord *record;
        const MinicRecordField *field;

        record = minic_c0_program_record(program, expression->value.member.record_id);
        field = minic_c0_record_field(record, expression->value.member.field_index);
        if (field == NULL ||
            !minic_riscv64_emit_member_address(file, program, function, expression)) {
            return false;
        }
        if (field->is_array) {
            return expression->value_category == MINIC_VALUE_LVALUE;
        }
        return minic_riscv64_emit_lvalue_load_from_address(
            file, program, expression_id, expression->type, "a0", "a0");
    }
'''
new_member_codegen = r'''    case MINIC_EXPRESSION_MEMBER: {
        const MinicRecord *record;
        const MinicRecordField *field;

        record = minic_c0_program_record(program, expression->value.member.record_id);
        field = minic_c0_record_field(record, expression->value.member.field_index);
        if (field == NULL) {
            return false;
        }
        if (expression->value_category == MINIC_VALUE_RVALUE) {
            return minic_riscv64_emit_record_rvalue_member(
                file, program, function, expression, expression_id);
        }
        if (!minic_riscv64_emit_member_address(file, program, function, expression)) {
            return false;
        }
        if (field->is_array) {
            return expression->value_category == MINIC_VALUE_LVALUE;
        }
        return minic_riscv64_emit_lvalue_load_from_address(
            file, program, expression_id, expression->type, "a0", "a0");
    }
'''
replace_once(codegen_path, old_member_codegen, new_member_codegen, "member rvalue codegen dispatch")

Path("tests/compiler/c0/record_rvalue_member.c").write_text(r'''typedef struct {
    unsigned long pgprot;
} pgprot_t;

pgprot_t pgprot_noncached(pgprot_t oldprot)
{
    return oldprot;
}

unsigned long project(pgprot_t oldprot)
{
    return (pgprot_noncached(oldprot)).pgprot;
}

int main(void)
{
    pgprot_t value = { 17 };
    return project(value) == 17 ? 0 : 1;
}
''')

Path("tests/compiler/c0/invalid_assign_record_rvalue_member.c").write_text(r'''typedef struct {
    unsigned long value;
} sample_t;

sample_t make_sample(sample_t value)
{
    return value;
}

int main(void)
{
    sample_t value = { 1 };
    make_sample(value).value = 7;
    return 0;
}
''')

Path("tests/compiler/c0/run-record-rvalue-member.sh").write_text(r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-record-rvalue-member

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/record_rvalue_member.c" -o "$work/member.i"
"$minic" -S "$work/member.i" -o "$work/member.s"
test "$(grep -c -F '  call pgprot_noncached' "$work/member.s")" -eq 1
grep -F '  sd a0, 0(sp)' "$work/member.s" >/dev/null
grep -F '  ld a0, 0(a0)' "$work/member.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/record_rvalue_member call-result=1 materialized-temp=1 scalar-member=rvalue once-only-call=1'

"$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_assign_record_rvalue_member.c" \
    -o "$work/invalid.i"
if "$minic" -S "$work/invalid.i" -o "$work/invalid.s" \
    >"$work/invalid.stdout" 2>"$work/invalid.stderr"; then
    echo 'FAIL record rvalue member assignment unexpectedly compiled' >&2
    exit 1
fi
grep -F 'assignment expression requires a modifiable object lvalue' "$work/invalid.stderr" >/dev/null
printf '%s\n' 'PASS compiler/c0/invalid_assign_record_rvalue_member nonmodifiable=1'
''')

run = Path("tests/compiler/c0/run.sh")
text = run.read_text()
marker = '''MINIC="$minic" \\
HOST_CC="$host_cc" \\
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\
sh "$root/tests/compiler/c0/run-gnu-function-pointer-bridge-call.sh"\n'''
insert = marker + '''\nMINIC="$minic" \\
HOST_CC="$host_cc" \\
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\
sh "$root/tests/compiler/c0/run-record-rvalue-member.sh"\n'''
if text.count(marker) != 1:
    raise SystemExit("record rvalue member C0 runner insertion anchor is not unique")
run.write_text(text.replace(marker, insert, 1))
