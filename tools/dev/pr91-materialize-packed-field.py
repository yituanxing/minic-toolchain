#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one exact anchor, found {count}")
    p.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/ast.h",
    '''    bool is_array;\n    bool is_bit_field;\n''',
    '''    bool is_array;\n    bool is_packed;\n    bool is_bit_field;\n''',
    "record field packed identity",
)

replace_once(
    "src/frontend/attribute.c",
    '''    MINIC_ATTRIBUTE_ENTRY("packed",\n                          MINIC_ATTRIBUTE_PACKED,\n                          MINIC_ATTRIBUTE_CLASS_LAYOUT,\n                          MINIC_ATTRIBUTE_TARGET_TYPE),\n    MINIC_ATTRIBUTE_ENTRY("__packed__",\n                          MINIC_ATTRIBUTE_PACKED,\n                          MINIC_ATTRIBUTE_CLASS_LAYOUT,\n                          MINIC_ATTRIBUTE_TARGET_TYPE),\n''',
    '''    MINIC_ATTRIBUTE_ENTRY("packed",\n                          MINIC_ATTRIBUTE_PACKED,\n                          MINIC_ATTRIBUTE_CLASS_LAYOUT,\n                          MINIC_ATTRIBUTE_TARGET_TYPE | MINIC_ATTRIBUTE_TARGET_FIELD),\n    MINIC_ATTRIBUTE_ENTRY("__packed__",\n                          MINIC_ATTRIBUTE_PACKED,\n                          MINIC_ATTRIBUTE_CLASS_LAYOUT,\n                          MINIC_ATTRIBUTE_TARGET_TYPE | MINIC_ATTRIBUTE_TARGET_FIELD),\n''',
    "packed field target registration",
)

replace_once(
    "src/frontend/parser_record.c",
    '''typedef struct MinicRecordFieldAttributeContext {\n    size_t explicit_alignment;\n} MinicRecordFieldAttributeContext;\n''',
    '''typedef struct MinicRecordFieldAttributeContext {\n    size_t explicit_alignment;\n    bool is_packed;\n} MinicRecordFieldAttributeContext;\n''',
    "record field attribute state",
)

replace_once(
    "src/frontend/parser_record.c",
    '''    if (descriptor->kind == MINIC_ATTRIBUTE_ALIGNED) {\n        return minic_parser_apply_alignment_attribute(\n            parser, attribute, "record field", &context->explicit_alignment);\n    }\n    minic_parser_error(parser, "unsupported GNU record field attribute");\n''',
    '''    if (descriptor->kind == MINIC_ATTRIBUTE_ALIGNED) {\n        return minic_parser_apply_alignment_attribute(\n            parser, attribute, "record field", &context->explicit_alignment);\n    }\n    if (descriptor->kind == MINIC_ATTRIBUTE_PACKED &&\n        descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_LAYOUT) {\n        context->is_packed = true;\n        return true;\n    }\n    minic_parser_error(parser, "unsupported GNU record field attribute");\n''',
    "consume packed field attribute",
)

replace_once(
    "src/frontend/parser_record.c",
    '''static bool parse_record_field_attributes(MinicParser *parser, size_t *explicit_alignment) {\n    MinicRecordFieldAttributeContext context;\n\n    if (parser == NULL || explicit_alignment == NULL) {\n        return false;\n    }\n    context.explicit_alignment = *explicit_alignment;\n    if (!minic_parser_parse_gnu_attribute_lists(parser, consume_record_field_attribute, &context)) {\n        return false;\n    }\n    *explicit_alignment = context.explicit_alignment;\n    return true;\n}\n''',
    '''static bool parse_record_field_attributes(MinicParser *parser,\n                                          size_t *explicit_alignment,\n                                          bool *is_packed) {\n    MinicRecordFieldAttributeContext context;\n\n    if (parser == NULL || explicit_alignment == NULL || is_packed == NULL) {\n        return false;\n    }\n    context.explicit_alignment = *explicit_alignment;\n    context.is_packed = *is_packed;\n    if (!minic_parser_parse_gnu_attribute_lists(parser, consume_record_field_attribute, &context)) {\n        return false;\n    }\n    *explicit_alignment = context.explicit_alignment;\n    *is_packed = context.is_packed;\n    return true;\n}\n''',
    "field attribute parser outputs",
)

replace_once(
    "src/frontend/parser_record.c",
    '''static bool parse_record_field_declarator(MinicParser *parser,\n                                          MinicRecordId record_id,\n                                          MinicType base_type,\n                                          size_t declaration_alignment) {\n''',
    '''static bool parse_record_field_declarator(MinicParser *parser,\n                                          MinicRecordId record_id,\n                                          MinicType base_type,\n                                          size_t declaration_alignment,\n                                          bool declaration_packed) {\n''',
    "field declarator packed input",
)

replace_once(
    "src/frontend/parser_record.c",
    '''    bool is_array;\n    bool is_flexible_array;\n''',
    '''    bool is_array;\n    bool is_packed;\n    bool is_flexible_array;\n''',
    "field declarator packed local",
)

replace_once(
    "src/frontend/parser_record.c",
    '''    element_count = 1U;\n    explicit_alignment = declaration_alignment;\n    is_array = false;\n''',
    '''    element_count = 1U;\n    explicit_alignment = declaration_alignment;\n    is_packed = declaration_packed;\n    is_array = false;\n''',
    "initialize field packed state",
)

replace_once(
    "src/frontend/parser_record.c",
    '''    if (!parse_record_field_attributes(parser, &explicit_alignment)) {\n        return false;\n    }\n\n    if (!minic_c0_record_add_field(parser->program,\n''',
    '''    if (!parse_record_field_attributes(parser, &explicit_alignment, &is_packed)) {\n        return false;\n    }\n\n    if (!minic_c0_record_add_field(parser->program,\n''',
    "parse declarator suffix packed attribute",
)

replace_once(
    "src/frontend/parser_record.c",
    '''    mutable_record->fields[mutable_record->field_count - 1U].explicit_alignment =\n        explicit_alignment;\n    mutable_record->fields[mutable_record->field_count - 1U].is_array = is_array;\n''',
    '''    mutable_record->fields[mutable_record->field_count - 1U].explicit_alignment =\n        explicit_alignment;\n    mutable_record->fields[mutable_record->field_count - 1U].is_packed = is_packed;\n    mutable_record->fields[mutable_record->field_count - 1U].is_array = is_array;\n''',
    "persist field packed identity",
)

replace_once(
    "src/frontend/parser_record.c",
    '''    size_t declaration_alignment;\n''',
    '''    size_t declaration_alignment;\n    bool declaration_packed;\n''',
    "record declaration packed state",
)

replace_once(
    "src/frontend/parser_record.c",
    '''    declaration_alignment = 0U;\n    if (!parse_record_field_attributes(parser, &declaration_alignment)) {\n        return false;\n    }\n''',
    '''    declaration_alignment = 0U;\n    declaration_packed = false;\n    if (!parse_record_field_attributes(parser, &declaration_alignment, &declaration_packed)) {\n        return false;\n    }\n''',
    "parse declaration-level field attrs",
)

replace_once(
    "src/frontend/parser_record.c",
    '''        if (declaration_alignment != 0U) {\n            minic_parser_error(parser, "GNU alignment on anonymous record members is unsupported");\n            return false;\n        }\n''',
    '''        if (declaration_alignment != 0U || declaration_packed) {\n            minic_parser_error(parser,\n                               "GNU layout attributes on anonymous record members are unsupported");\n            return false;\n        }\n''',
    "anonymous record layout attrs fail closed",
)

replace_once(
    "src/frontend/parser_record.c",
    '''        if (declaration_alignment != 0U) {\n            minic_parser_error(parser, "GNU alignment on unnamed bit-fields is unsupported");\n            return false;\n        }\n''',
    '''        if (declaration_alignment != 0U || declaration_packed) {\n            minic_parser_error(parser, "GNU layout attributes on unnamed bit-fields are unsupported");\n            return false;\n        }\n''',
    "unnamed bitfield layout attrs fail closed",
)

replace_once(
    "src/frontend/parser_record.c",
    '''        if (!parse_record_field_declarator(parser, record_id, base_type, declaration_alignment)) {\n            return false;\n        }\n''',
    '''        if (!parse_record_field_declarator(\n                parser, record_id, base_type, declaration_alignment, declaration_packed)) {\n            return false;\n        }\n''',
    "pass declaration packed state",
)

replace_once(
    "src/target/data_layout.c",
    '''            field_size = (field->is_flexible_array || field->is_zero_length_array)\n                             ? 0U\n                             : element_size * field->element_count;\n            if (field->explicit_alignment != 0U) {\n''',
    '''            field_size = (field->is_flexible_array || field->is_zero_length_array)\n                             ? 0U\n                             : element_size * field->element_count;\n            if (field->is_packed) {\n                field_alignment = 1U;\n            }\n            if (field->explicit_alignment != 0U) {\n''',
    "lower packed field natural alignment",
)

Path("tests/compiler/c0/gnu_packed_record_field.c").write_text(
    '''struct PackedMiddle {
    unsigned char lead;
    unsigned long value __attribute__((__packed__));
    unsigned char tail;
};

struct PackedThenNatural {
    unsigned char lead;
    unsigned long packed_value __attribute__((packed));
    unsigned long natural_value;
};

struct PackedAligned {
    unsigned char lead;
    unsigned long value __attribute__((packed, aligned(4)));
    unsigned char tail;
};

_Static_assert(__builtin_offsetof(struct PackedMiddle, lead) == 0, "lead");
_Static_assert(__builtin_offsetof(struct PackedMiddle, value) == 1, "packed field offset");
_Static_assert(__builtin_offsetof(struct PackedMiddle, tail) == 9, "tail after packed field");
_Static_assert(sizeof(struct PackedMiddle) == 10, "packed field does not pack entire record");

_Static_assert(__builtin_offsetof(struct PackedThenNatural, packed_value) == 1, "packed value");
_Static_assert(__builtin_offsetof(struct PackedThenNatural, natural_value) == 16, "next field natural alignment");
_Static_assert(sizeof(struct PackedThenNatural) == 24, "record keeps natural alignment from normal field");

_Static_assert(__builtin_offsetof(struct PackedAligned, value) == 4, "aligned raises packed field alignment");
_Static_assert(__builtin_offsetof(struct PackedAligned, tail) == 12, "aligned packed field size");
_Static_assert(sizeof(struct PackedAligned) == 16, "explicit field alignment contributes to record");

int main(void)
{
    struct PackedMiddle value = {1, 2, 3};
    return value.lead == 1 && value.value == 2 && value.tail == 3 ? 0 : 1;
}
'''
)

Path("tests/compiler/c0/run-gnu-packed-record-field.sh").write_text(
    '''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-packed-record-field

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_packed_record_field.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"
grep -F 'main:' "$work/output.s" >/dev/null

printf '%s\n' \
  'PASS compiler/c0/gnu_packed_record_field field-identity=1 natural-alignment=lowered next-field=natural packed+aligned=explicit-raises record-layout=DataLayout'
'''
)

run_path = Path("tests/compiler/c0/run.sh")
run_text = run_path.read_text()
needle = 'sh "$root/tests/compiler/c0/run-function-parameter-adjustment.sh"\n'
if needle not in run_text:
    raise SystemExit("C0 runner insertion anchor missing")
insert = needle + '''\nMINIC="$minic" \\
HOST_CC="$host_cc" \\
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\
sh "$root/tests/compiler/c0/run-gnu-packed-record-field.sh"\n'''
run_path.write_text(run_text.replace(needle, insert, 1))
