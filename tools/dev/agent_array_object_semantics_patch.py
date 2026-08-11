from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]

def read(path):
    return (ROOT / path).read_text()

def write(path, text):
    (ROOT / path).write_text(text)

def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one exact match, got {count}")
    return text.replace(old, new, 1)

def sub_once(text, pattern, replacement, label, flags=0):
    new, count = re.subn(pattern, replacement, text, count=1, flags=flags)
    if count != 1:
        raise SystemExit(f"{label}: expected one regex match, got {count}")
    return new

# AST semantic identity for all array-object representations.
path = "src/frontend/ast.h"
text = read(path)
text = replace_once(text, "typedef struct MinicLocal {\n", "typedef struct MinicArrayObjectInfo {\n    MinicType element_type;\n    size_t element_count;\n    bool is_incomplete;\n    bool is_zero_length;\n} MinicArrayObjectInfo;\n\ntypedef struct MinicLocal {\n", "array object info type")
text = replace_once(text, "const MinicExpression *minic_c0_program_expression(const MinicC0Program *program,\n                                                   MinicExpressionId expression_id);\n", "const MinicExpression *minic_c0_program_expression(const MinicC0Program *program,\n                                                   MinicExpressionId expression_id);\nbool minic_c0_expression_array_object_info(const MinicC0Program *program,\n                                           const MinicExpression *expression,\n                                           MinicArrayObjectInfo *info);\n", "array object query declaration")
write(path, text)

path = "src/frontend/ast.c"
text = read(path)
pattern = r"(const MinicExpression \*minic_c0_program_expression\(const MinicC0Program \*program,\n\s*MinicExpressionId expression_id\) \{.*?\n\}\n)"
helper = r'''

bool minic_c0_expression_array_object_info(const MinicC0Program *program,
                                           const MinicExpression *expression,
                                           MinicArrayObjectInfo *info) {
    MinicArrayObjectInfo resolved;

    if (program == NULL || expression == NULL || expression->value_category != MINIC_VALUE_LVALUE) {
        return false;
    }
    (void)memset(&resolved, 0, sizeof(resolved));
    if (minic_type_is_array(expression->type)) {
        const MinicArrayType *array_type;

        array_type = minic_c0_program_array_type(program, expression->type.array_type_id);
        if (array_type == NULL) {
            return false;
        }
        resolved.element_type = array_type->element_type;
        resolved.element_count = array_type->element_count;
        resolved.is_incomplete = array_type->element_count == 0U;
    } else if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        const MinicLocal *local;

        local = minic_c0_program_local(program, expression->value.local_id);
        if (local == NULL || !local->is_array) {
            return false;
        }
        resolved.element_type = expression->type;
        resolved.element_count = local->element_count;
    } else if (expression->kind == MINIC_EXPRESSION_MEMBER) {
        const MinicRecord *record;
        const MinicRecordField *field;

        record = minic_c0_program_record(program, expression->value.member.record_id);
        field = minic_c0_record_field(record, expression->value.member.field_index);
        if (field == NULL || !field->is_array) {
            return false;
        }
        resolved.element_type = expression->type;
        resolved.element_count = field->element_count;
        resolved.is_incomplete = field->is_flexible_array;
        resolved.is_zero_length = field->is_zero_length_array;
    } else {
        return false;
    }
    if (info != NULL) {
        *info = resolved;
    }
    return true;
}
'''
text = sub_once(text, pattern, r"\1" + helper, "array object query implementation", flags=re.S)
write(path, text)

path = "src/frontend/parser_internal.h"
text = read(path)
text = replace_once(text, "bool minic_parser_apply_array_decay(MinicParser *parser,\n                                    MinicExpressionId input_id,\n                                    MinicExpressionId *expression_id);", "bool minic_parser_apply_array_decay(MinicParser *parser,\n                                    MinicExpressionId input_id,\n                                    MinicExpressionId *expression_id);\nbool minic_parser_materialize_array_object_type(MinicParser *parser,\n                                                MinicExpressionId expression_id,\n                                                MinicType *array_type);", "array materializer declaration")
write(path, text)

path = "src/frontend/parser_postfix.c"
text = read(path)
text = sub_once(text, r"static bool postfix_element_type\(.*?\n}\n\nstatic bool array_object_element_type\(.*?\n}\n\n", r'''static bool postfix_element_type(const MinicParser *parser,
                                 MinicExpressionId base_id,
                                 MinicType *element_type) {
    const MinicExpression *base;
    MinicArrayObjectInfo array_info;

    if (parser == NULL || element_type == NULL) {
        return false;
    }
    base = minic_c0_program_expression(parser->program, base_id);
    if (base == NULL) {
        return false;
    }
    if (minic_c0_expression_array_object_info(parser->program, base, &array_info)) {
        *element_type = array_info.element_type;
        return true;
    }
    return minic_type_pointee(base->type, element_type);
}

bool minic_parser_materialize_array_object_type(MinicParser *parser,
                                                MinicExpressionId expression_id,
                                                MinicType *array_type) {
    const MinicExpression *expression;
    MinicArrayObjectInfo info;

    if (parser == NULL || array_type == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(parser->program, expression_id);
    if (expression == NULL || !minic_c0_expression_array_object_info(parser->program, expression, &info)) {
        return false;
    }
    if (minic_type_is_array(expression->type)) {
        *array_type = expression->type;
        return true;
    }
    if (info.is_zero_length) {
        minic_parser_error(parser, "zero-length legacy array type materialization is unsupported");
        return false;
    }
    if (info.is_incomplete) {
        return minic_c0_program_add_incomplete_array_type(parser->program, info.element_type, array_type);
    }
    return info.element_count != 0U && minic_c0_program_add_array_type(parser->program, info.element_type, info.element_count, array_type);
}

''', "converge parser array helpers", flags=re.S)
text = replace_once(text, "    MinicType element_type;\n\n    base = minic_c0_program_expression(parser->program, input_id);", "    MinicType element_type;\n    MinicArrayObjectInfo array_info;\n\n    base = minic_c0_program_expression(parser->program, input_id);", "decay info declaration")
text = replace_once(text, "    if (!array_object_element_type(parser, input_id, &element_type)) {\n        *expression_id = input_id;\n        return true;\n    }", "    if (!minic_c0_expression_array_object_info(parser->program, base, &array_info)) {\n        *expression_id = input_id;\n        return true;\n    }\n    element_type = array_info.element_type;", "decay shared query")
text = replace_once(text, "    MinicType array_element_type;\n", "    MinicArrayObjectInfo array_info;\n", "postfix update array info declaration")
text = replace_once(text, "    if (operand->value_category != MINIC_VALUE_LVALUE || minic_type_is_const(operand_type) ||\n        array_object_element_type(parser, operand_id, &array_element_type)) {", "    if (operand->value_category != MINIC_VALUE_LVALUE || minic_type_is_const(operand_type) ||\n        minic_c0_expression_array_object_info(parser->program, operand, &array_info)) {", "postfix update shared query")
write(path, text)

path = "src/frontend/parser_member.c"
text = read(path)
text = replace_once(text, "    if (field->is_array) {\n        if (!minic_type_pointer_to(member_type, &member_expression.type)) {\n            minic_parser_error(parser, \"record array member pointer depth is unsupported\");\n            return false;\n        }\n        member_expression.value_category = MINIC_VALUE_RVALUE;\n    } else {\n        member_expression.type = member_type;\n        member_expression.value_category = MINIC_VALUE_LVALUE;\n    }", "    member_expression.type = member_type;\n    member_expression.value_category = MINIC_VALUE_LVALUE;", "preserve record array object identity")
write(path, text)

path = "src/frontend/parser_expression.c"
text = read(path)
text = sub_once(text, r"static bool local_array_without_array_type\(.*?\n}\n\n(?=static bool current_is_sizeof)", "", "remove local-only array helper", flags=re.S)
text = replace_once(text, "        measured_type = operand->type;\n        if (local_array_without_array_type(parser, operand)) {\n            const MinicLocal *local;\n\n            local = minic_c0_program_local(parser->program, operand->value.local_id);\n            if (local == NULL ||\n                !minic_c0_program_add_array_type(\n                    parser->program, local->type, local->element_count, &measured_type)) {\n                minic_parser_error(parser, \"cannot preserve local array type for sizeof\");\n                return false;\n            }\n        }", "        measured_type = operand->type;\n        if (minic_c0_expression_array_object_info(parser->program, operand, NULL) &&\n            !minic_parser_materialize_array_object_type(parser, operand_id, &measured_type)) {\n            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {\n                minic_parser_error(parser, \"cannot preserve array object type for sizeof\");\n            }\n            return false;\n        }", "sizeof shared array materialization")
text = replace_once(text, "        if (operand_expression->value_category != MINIC_VALUE_LVALUE ||\n            minic_type_is_const(operand_expression->type)) {", "        if (operand_expression->value_category != MINIC_VALUE_LVALUE ||\n            minic_type_is_const(operand_expression->type) ||\n            minic_c0_expression_array_object_info(parser->program, operand_expression, NULL)) {", "prefix update array rejection")
old = """        } else {
            if (local_array_without_array_type(parser, operand_expression)) {
                minic_parser_error(parser, \"address-of local array object is not supported yet\");
                return false;
            }
            if (minic_c0_expression_bit_field(parser->program, operand) != NULL) {
                minic_parser_error(parser, \"cannot take the address of a bit-field\");
                return false;
            }
            if (operand_expression->value_category != MINIC_VALUE_LVALUE ||
                !minic_type_pointer_to(operand_expression->type, &expression.type)) {
                minic_parser_error(parser,
                                   \"address-of requires an lvalue object or function designator\");
                return false;
            }
        }
"""
new = """        } else if (minic_c0_expression_array_object_info(parser->program, operand_expression, NULL)) {
            MinicType array_type;

            if (!minic_parser_materialize_array_object_type(parser, operand, &array_type) ||
                !minic_type_pointer_to(array_type, &expression.type)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                    minic_parser_error(parser, \"cannot form pointer to array object\");
                }
                return false;
            }
        } else {
            if (minic_c0_expression_bit_field(parser->program, operand) != NULL) {
                minic_parser_error(parser, \"cannot take the address of a bit-field\");
                return false;
            }
            if (operand_expression->value_category != MINIC_VALUE_LVALUE ||
                !minic_type_pointer_to(operand_expression->type, &expression.type)) {
                minic_parser_error(parser,
                                   \"address-of requires an lvalue object or function designator\");
                return false;
            }
        }
"""
text = replace_once(text, old, new, "address-of array object")
text = replace_once(text, "            minic_type_is_const(target_expression->type) ||\n            minic_type_is_array(target_expression->type) ||\n            minic_type_is_function(target_expression->type) ||\n            minic_type_is_record(target_expression->type)) {", "            minic_type_is_const(target_expression->type) ||\n            minic_c0_expression_array_object_info(parser->program, target_expression, NULL) ||\n            minic_type_is_function(target_expression->type) ||\n            minic_type_is_record(target_expression->type)) {", "compound assignment array rejection")
text = replace_once(text, "            minic_type_is_const(target_expression->type) ||\n            minic_type_is_array(target_expression->type) ||\n            minic_type_is_function(target_expression->type)) {", "            minic_type_is_const(target_expression->type) ||\n            minic_c0_expression_array_object_info(parser->program, target_expression, NULL) ||\n            minic_type_is_function(target_expression->type)) {", "simple assignment array rejection")
write(path, text)

path = "src/frontend/parser_type.c"
text = read(path)
text = replace_once(text, "            parsed_type = operand->type;\n", "            parsed_type = operand->type;\n            if (minic_c0_expression_array_object_info(parser->program, operand, NULL) &&\n                !minic_parser_materialize_array_object_type(parser, operand_id, &parsed_type)) {\n                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {\n                    minic_parser_error(parser, \"cannot preserve GNU typeof array operand\");\n                }\n                return false;\n            }\n", "typeof array materialization")
write(path, text)

path = "src/frontend/ast_verifier.c"
text = read(path)
text = sub_once(text, r"static bool verify_subscript_type\(.*?\n}\n\n(?=static bool verify_call_arguments)", r'''static bool array_object_type_matches(const MinicC0Program *program,
                                      const MinicArrayObjectInfo *info,
                                      MinicType array_type) {
    const MinicArrayType *materialized;

    if (program == NULL || info == NULL || !minic_type_is_array(array_type) || info->is_zero_length) {
        return false;
    }
    materialized = minic_c0_program_array_type(program, array_type.array_type_id);
    if (materialized == NULL || !minic_type_equal(materialized->element_type, info->element_type)) {
        return false;
    }
    return info->is_incomplete ? materialized->element_count == 0U
                               : materialized->element_count == info->element_count;
}

static bool verify_subscript_type(const MinicC0Program *program,
                                  const MinicExpression *base,
                                  MinicType result_type) {
    MinicArrayObjectInfo array_info;
    MinicType pointee_type;

    if (minic_c0_expression_array_object_info(program, base, &array_info)) {
        return minic_type_equal(array_info.element_type, result_type);
    }
    if (minic_type_pointee(base->type, &pointee_type)) {
        return minic_type_equal(pointee_type, result_type);
    }
    return false;
}

''', "verifier array helpers", flags=re.S)
text = replace_once(text, "        return operand->value_category == MINIC_VALUE_LVALUE &&\n               minic_type_pointee(expression->type, &pointee_type) &&\n               minic_type_equal(pointee_type, operand->type);", "        if (operand->value_category != MINIC_VALUE_LVALUE ||\n            !minic_type_pointee(expression->type, &pointee_type)) {\n            return false;\n        }\n        {\n            MinicArrayObjectInfo array_info;\n\n            if (minic_c0_expression_array_object_info(program, operand, &array_info)) {\n                return array_object_type_matches(program, &array_info, pointee_type);\n            }\n        }\n        return minic_type_equal(pointee_type, operand->type);", "verifier address-of array")
text = replace_once(text, "        if (field->is_array) {\n            return expression->value_category == MINIC_VALUE_RVALUE &&\n                   minic_type_pointer_to(expected_type, &expected_type) &&\n                   minic_type_equal(expression->type, expected_type);\n        }\n        return expression->value_category == MINIC_VALUE_LVALUE &&\n               minic_type_equal(expression->type, expected_type);", "        return expression->value_category == MINIC_VALUE_LVALUE &&\n               minic_type_equal(expression->type, expected_type);", "verifier member array lvalue")
text = replace_once(text, "        if (left == NULL || right == NULL || left->value_category != MINIC_VALUE_LVALUE ||\n            minic_type_is_const(left->type) || minic_type_is_array(left->type) ||\n            minic_type_is_function(left->type) ||", "        if (left == NULL || right == NULL || left->value_category != MINIC_VALUE_LVALUE ||\n            minic_type_is_const(left->type) ||\n            minic_c0_expression_array_object_info(program, left, NULL) ||\n            minic_type_is_function(left->type) ||", "verifier assignment array rejection")
text = replace_once(text, "        if (left == NULL || right == NULL || left->value_category != MINIC_VALUE_LVALUE ||\n            expression->value_category != MINIC_VALUE_RVALUE ||\n            !minic_type_equal(expression->type, left->type) || minic_type_is_const(left->type)) {", "        if (left == NULL || right == NULL || left->value_category != MINIC_VALUE_LVALUE ||\n            expression->value_category != MINIC_VALUE_RVALUE ||\n            !minic_type_equal(expression->type, left->type) || minic_type_is_const(left->type) ||\n            minic_c0_expression_array_object_info(program, left, NULL)) {", "verifier compound array rejection")
write(path, text)

path = "src/target/riscv64/codegen_expression.c"
text = read(path)
text = sub_once(text, r'''    base_is_array_object = false;
    if \(base->kind == MINIC_EXPRESSION_LOCAL.*?\n    \}

    if \(base_is_array_object\) \{''', r'''    {
        MinicArrayObjectInfo array_info;

        base_is_array_object = minic_c0_expression_array_object_info(program, base, &array_info);
        if (base_is_array_object && !minic_type_equal(array_info.element_type, expression->type)) {
            return false;
        }
    }

    if (base_is_array_object) {''', "RV64 shared array-object query", flags=re.S)
text = replace_once(text, "        if (field->is_array) {\n            return minic_type_is_pointer(expression->type);\n        }", "        if (field->is_array) {\n            return expression->value_category == MINIC_VALUE_LVALUE;\n        }", "RV64 member array lvalue")
write(path, text)

write("tests/compiler/c0/gnu_array_object_identity.c", r'''struct CpuMask {
    unsigned long bits[1];
};
struct FixedArrayHolder {
    int head;
    unsigned long values[4];
};
struct FlexibleArrayHolder {
    int head;
    unsigned long values[];
};
unsigned long fixed_member_size(struct FixedArrayHolder *holder) { return sizeof(holder->values); }
unsigned long fixed_member_address_pointee_size(struct FixedArrayHolder *holder) { return sizeof(*(&holder->values)); }
unsigned long fixed_member_typeof_size(struct FixedArrayHolder *holder) { return sizeof(typeof(holder->values)); }
unsigned long fixed_member_index(struct FixedArrayHolder *holder) { return holder->values[2]; }
unsigned long *fixed_member_decay(struct FixedArrayHolder *holder) { return holder->values; }
struct CpuMask *linux_flexible_array_shape(struct FlexibleArrayHolder *holder) { return (struct CpuMask *)&holder->values; }
unsigned long local_array_address_pointee_size(void) { unsigned long values[3]; return sizeof(*(&values)); }
unsigned long local_array_typeof_size(void) { unsigned long values[3]; return sizeof(typeof(values)); }
''')
write("tests/compiler/c0/invalid_record_array_assignment.c", "struct Holder { int values[4]; };\nvoid bad(struct Holder *holder) { holder->values = 0; }\n")
write("tests/compiler/c0/invalid_record_array_update.c", "struct Holder { int values[4]; };\nvoid bad(struct Holder *holder) { holder->values++; }\n")
write("tests/compiler/c0/invalid_flexible_array_sizeof.c", "struct Holder { int head; unsigned long values[]; };\nunsigned long bad(struct Holder *holder) { return sizeof(holder->values); }\n")
write("tests/compiler/c0/run-gnu-array-object-identity.sh", r'''#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-array-object-identity
assembly="$work/gnu_array_object_identity.s"
rm -rf "$work"; mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_array_object_identity.c" -o "$work/gnu_array_object_identity.i"
"$minic" -S "$work/gnu_array_object_identity.i" -o "$assembly"
test -s "$assembly"
check_li() { symbol=$1; value=$2; sed -n "/^${symbol}:/,/^\\.size/p" "$assembly" | grep -F "  li a0, $value" >/dev/null; }
check_li fixed_member_size 32
check_li fixed_member_address_pointee_size 32
check_li fixed_member_typeof_size 32
check_li local_array_address_pointee_size 24
check_li local_array_typeof_size 24
sed -n '/linux_flexible_array_shape:/,/^\.size/p' "$assembly" | grep -F '  addi a0, a0, 8' >/dev/null
sed -n '/fixed_member_index:/,/^\.size/p' "$assembly" | grep -F '  slli a0, a0, 3' >/dev/null
for invalid in invalid_record_array_assignment invalid_record_array_update invalid_flexible_array_sizeof; do
  "$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/$invalid.c" -o "$work/$invalid.i"
  if "$minic" -S "$work/$invalid.i" -o "$work/$invalid.s" >"$work/$invalid.out" 2>"$work/$invalid.err"; then echo "expected $invalid to fail" >&2; exit 1; fi
done
grep -F 'assignment expression requires a modifiable object lvalue' "$work/invalid_record_array_assignment.err" >/dev/null
grep -F 'postfix update requires a modifiable scalar lvalue' "$work/invalid_record_array_update.err" >/dev/null
grep -F 'sizeof requires a supported complete type' "$work/invalid_flexible_array_sizeof.err" >/dev/null
printf '%s\n' 'PASS compiler/c0/gnu_array_object_identity record=fixed+flexible local=legacy decay=shared address-of=pointer-to-array sizeof=no-decay typeof=no-decay subscript=shared mutation=reject'
''')

path = "tools/dev/pr76-focused.sh"
text = read(path)
text = replace_once(text, "sh tests/compiler/c0/run-gnu-auto-type-local.sh\n", "sh tests/compiler/c0/run-gnu-auto-type-local.sh\nsh tests/compiler/c0/run-gnu-array-object-identity.sh\n", "register array-object focused")
write(path, text)
print("PASS generated array-object semantic identity slice")
