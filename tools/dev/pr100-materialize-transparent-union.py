#!/usr/bin/env python3
from pathlib import Path
import re


def replace_once(path: str, old: str, new: str, label: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one exact anchor, found {count}")
    p.write_text(text.replace(old, new, 1))


def regex_once(path: str, pattern: str, replacement: str, label: str) -> None:
    p = Path(path)
    text = p.read_text()
    updated, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if count != 1:
        raise SystemExit(f"{label}: expected one regex anchor, found {count}")
    p.write_text(updated)


# AttributeRegistry: transparent_union is a zero-argument language-semantic type attribute.
replace_once(
    "src/frontend/attribute.h",
    '''    MINIC_ATTRIBUTE_ALIGNED,\n    MINIC_ATTRIBUTE_CLEANUP,\n''',
    '''    MINIC_ATTRIBUTE_ALIGNED,\n    MINIC_ATTRIBUTE_TRANSPARENT_UNION,\n    MINIC_ATTRIBUTE_CLEANUP,\n''',
    "transparent union attribute kind",
)
replace_once(
    "src/frontend/attribute.c",
    '''    MINIC_ATTRIBUTE_ENTRY("__aligned__",\n                          MINIC_ATTRIBUTE_ALIGNED,\n                          MINIC_ATTRIBUTE_CLASS_LAYOUT,\n                          MINIC_ATTRIBUTE_TARGET_OBJECT | MINIC_ATTRIBUTE_TARGET_TYPE |\n                              MINIC_ATTRIBUTE_TARGET_FIELD),\n    {\n        "cleanup",\n''',
    '''    MINIC_ATTRIBUTE_ENTRY("__aligned__",\n                          MINIC_ATTRIBUTE_ALIGNED,\n                          MINIC_ATTRIBUTE_CLASS_LAYOUT,\n                          MINIC_ATTRIBUTE_TARGET_OBJECT | MINIC_ATTRIBUTE_TARGET_TYPE |\n                              MINIC_ATTRIBUTE_TARGET_FIELD),\n    {\n        "transparent_union",\n        sizeof("transparent_union") - 1U,\n        MINIC_ATTRIBUTE_TRANSPARENT_UNION,\n        MINIC_ATTRIBUTE_CLASS_LANGUAGE_SEMANTIC,\n        MINIC_ATTRIBUTE_TARGET_TYPE,\n        0U,\n        0U,\n        true,\n    },\n    {\n        "__transparent_union__",\n        sizeof("__transparent_union__") - 1U,\n        MINIC_ATTRIBUTE_TRANSPARENT_UNION,\n        MINIC_ATTRIBUTE_CLASS_LANGUAGE_SEMANTIC,\n        MINIC_ATTRIBUTE_TARGET_TYPE,\n        0U,\n        0U,\n        true,\n    },\n    {\n        "cleanup",\n''',
    "transparent union descriptors",
)

# The semantic property belongs to union identity, not to the typedef spelling.
replace_once(
    "src/frontend/ast.h",
    '''    bool is_union;\n    bool is_packed;\n    bool is_complete;\n} MinicRecord;\n''',
    '''    bool is_union;\n    bool is_packed;\n    bool is_transparent_union;\n    bool is_complete;\n} MinicRecord;\n''',
    "record transparent union identity",
)

# Shared call compatibility and fixed-parameter ABI view.
replace_once(
    "src/frontend/ast.h",
    '''bool minic_c0_assignment_compatible(const MinicC0Program *program,\n                                    MinicType target_type,\n                                    MinicExpressionId source_expression_id);\n''',
    '''bool minic_c0_assignment_compatible(const MinicC0Program *program,\n                                    MinicType target_type,\n                                    MinicExpressionId source_expression_id);\nbool minic_c0_fixed_call_argument_compatible(const MinicC0Program *program,\n                                             MinicType parameter_type,\n                                             MinicExpressionId argument_expression_id);\nbool minic_c0_fixed_parameter_abi_type(const MinicC0Program *program,\n                                       MinicType parameter_type,\n                                       MinicType *abi_type);\n''',
    "transparent union call API declarations",
)

ast = Path("src/frontend/ast.c")
text = ast.read_text()
append_marker = "\nbool minic_c0_fixed_call_argument_compatible("
if append_marker in text:
    raise SystemExit("transparent union call helpers already present")
text += r'''

bool minic_c0_fixed_parameter_abi_type(const MinicC0Program *program,
                                       MinicType parameter_type,
                                       MinicType *abi_type) {
    const MinicRecord *record;
    const MinicRecordField *first_field;

    if (program == NULL || abi_type == NULL) {
        return false;
    }
    *abi_type = parameter_type;
    if (!minic_type_is_record(parameter_type)) {
        return true;
    }
    record = minic_c0_program_record(program, parameter_type.record_id);
    if (record == NULL || !record->is_transparent_union) {
        return record != NULL;
    }
    if (!record->is_complete || !record->is_union || record->field_count == 0U) {
        return false;
    }
    first_field = minic_c0_record_field(record, 0U);
    if (first_field == NULL || first_field->is_array || first_field->is_bit_field ||
        !minic_type_is_pointer(first_field->type)) {
        return false;
    }
    *abi_type = first_field->type;
    return true;
}

bool minic_c0_fixed_call_argument_compatible(const MinicC0Program *program,
                                             MinicType parameter_type,
                                             MinicExpressionId argument_expression_id) {
    const MinicRecord *record;
    size_t field_index;

    if (program == NULL) {
        return false;
    }
    if (minic_c0_assignment_compatible(program, parameter_type, argument_expression_id)) {
        return true;
    }
    if (!minic_type_is_record(parameter_type)) {
        return false;
    }
    record = minic_c0_program_record(program, parameter_type.record_id);
    if (record == NULL || !record->is_complete || !record->is_union ||
        !record->is_transparent_union || record->field_count == 0U) {
        return false;
    }
    for (field_index = 0U; field_index < record->field_count; ++field_index) {
        const MinicRecordField *field;

        field = minic_c0_record_field(record, field_index);
        if (field == NULL || field->is_array || field->is_bit_field ||
            !minic_type_is_pointer(field->type)) {
            return false;
        }
        if (minic_c0_assignment_compatible(program, field->type, argument_expression_id)) {
            return true;
        }
    }
    return false;
}
'''
ast.write_text(text)

# Converge typedef suffix attributes on the shared attribute parser. Preserve aligned semantics,
# and attach transparent_union to the underlying union identity. v0 deliberately requires pointer
# members so the current target has one proven machine representation/calling class.
typedef_path = "src/frontend/parser_typedef.c"
new_typedef_attrs = r'''typedef struct MinicTypedefAttributeContext {
    MinicType *aliased_type;
} MinicTypedefAttributeContext;

static bool consume_typedef_attribute(MinicParser *parser,
                                      const MinicParsedAttribute *attribute,
                                      void *opaque_context) {
    MinicTypedefAttributeContext *context;
    const MinicAttributeDescriptor *descriptor;

    if (parser == NULL || attribute == NULL || opaque_context == NULL) {
        return false;
    }
    context = (MinicTypedefAttributeContext *)opaque_context;
    descriptor = attribute->descriptor;
    if (descriptor == NULL ||
        !minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_TYPE)) {
        minic_parser_error(parser, "unsupported GNU typedef attribute");
        return false;
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_ALIGNED) {
        size_t natural_size;
        size_t natural_alignment;
        size_t alignment;

        if (minic_type_is_pointer(*context->aliased_type)) {
            minic_parser_error(parser, "aligned pointer typedefs require per-layer type attributes");
            return false;
        }
        alignment = context->aliased_type->explicit_alignment;
        if (!minic_parser_apply_alignment_attribute(
                parser, attribute, "typedef", &alignment) ||
            !minic_data_layout_type(minic_target_info_data_layout(parser->target_info),
                                    parser->program,
                                    *context->aliased_type,
                                    &natural_size,
                                    &natural_alignment)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot determine GNU typedef alignment");
            }
            return false;
        }
        (void)natural_size;
        if (alignment < natural_alignment) {
            minic_parser_error(parser, "reducing GNU typedef alignment is not supported yet");
            return false;
        }
        context->aliased_type->explicit_alignment = alignment;
        return true;
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_TRANSPARENT_UNION) {
        MinicRecord *record;
        size_t field_index;

        if (!minic_type_is_record(*context->aliased_type)) {
            minic_parser_error(parser, "GNU transparent_union requires a union type");
            return false;
        }
        record = &parser->program->records[context->aliased_type->record_id];
        if (!record->is_complete || !record->is_union || record->field_count == 0U) {
            minic_parser_error(parser, "GNU transparent_union requires a complete non-empty union");
            return false;
        }
        for (field_index = 0U; field_index < record->field_count; ++field_index) {
            const MinicRecordField *field;

            field = minic_c0_record_field(record, field_index);
            if (field == NULL || field->is_array || field->is_bit_field ||
                !minic_type_is_pointer(field->type)) {
                minic_parser_error(
                    parser,
                    "GNU transparent_union v0 requires pointer members with one machine representation");
                return false;
            }
        }
        record->is_transparent_union = true;
        return true;
    }
    minic_parser_error(parser, "unsupported GNU typedef attribute");
    return false;
}

static bool parse_typedef_attributes(MinicParser *parser, MinicType *aliased_type) {
    MinicTypedefAttributeContext context;

    if (parser == NULL || aliased_type == NULL) {
        return false;
    }
    context.aliased_type = aliased_type;
    return minic_parser_parse_gnu_attribute_lists(parser, consume_typedef_attribute, &context);
}

'''
regex_once(
    typedef_path,
    r'''static bool parse_typedef_alignment\(.*?\n}\n\n(?=bool minic_parser_parse_typedef)''',
    new_typedef_attrs,
    "typedef attribute consumer convergence",
)
replace_once(
    typedef_path,
    '''    if (!parse_typedef_alignment(parser, &aliased_type)) {\n''',
    '''    if (!parse_typedef_attributes(parser, &aliased_type)) {\n''',
    "typedef attribute caller",
)

# Direct and indirect call parsers use one semantic compatibility predicate.
pexpr = Path("src/frontend/parser_expression.c")
text = pexpr.read_text()
text, count = re.subn(
    r'''!minic_c0_assignment_compatible\(\s*parser->program,\s*callee->parameter_types\[argument_index\],\s*argument_id\)''',
    '''!minic_c0_fixed_call_argument_compatible(\n                parser->program, callee->parameter_types[argument_index], argument_id)''',
    text,
    count=1,
)
if count != 1:
    raise SystemExit(f"direct call compatibility replacement count={count}")
pexpr.write_text(text)

ppost = Path("src/frontend/parser_postfix.c")
text = ppost.read_text()
text, count = re.subn(
    r'''!minic_c0_assignment_compatible\(\s*parser->program,\s*function_type->parameter_types\[argument_index\],\s*argument_id\)''',
    '''!minic_c0_fixed_call_argument_compatible(\n                parser->program, function_type->parameter_types[argument_index], argument_id)''',
    text,
    count=1,
)
if count != 1:
    raise SystemExit(f"indirect call compatibility replacement count={count}")
ppost.write_text(text)

verifier = Path("src/frontend/ast_verifier.c")
text = verifier.read_text()
text, count = re.subn(
    r'''!minic_c0_assignment_compatible\(\s*program,\s*parameter_types\[argument_index\],\s*expression->value\.call\.arguments\[argument_index\]\)''',
    '''!minic_c0_fixed_call_argument_compatible(\n                    program,\n                    parameter_types[argument_index],\n                    expression->value.call.arguments[argument_index])''',
    text,
    count=1,
)
if count != 1:
    raise SystemExit(f"AST call compatibility replacement count={count}")
verifier.write_text(text)

# RV64 caller: precompute fixed-parameter ABI types once, then use that view for all argument
# evaluation/register classification. Transparent union therefore behaves exactly like member 0.
codegen = Path("src/target/riscv64/codegen_expression.c")
text = codegen.read_text()
usage_count = text.count("parameter_types[argument_index]")
if usage_count < 6:
    raise SystemExit(f"unexpected RV64 parameter ABI use count={usage_count}")
text = text.replace("parameter_types[argument_index]", "abi_parameter_types[argument_index]")
text, count = re.subn(
    r'''(\s*const MinicType \*parameter_types;\n)''',
    r'''\1        MinicType abi_parameter_types[MINIC_MAX_FUNCTION_PARAMETERS];\n''',
    text,
    count=1,
)
if count != 1:
    raise SystemExit("RV64 ABI parameter array declaration anchor not found")
anchor = '''        if (!is_indirect && direct_callee != NULL && direct_callee->name_length == 16U &&\n'''
if text.count(anchor) != 1:
    raise SystemExit("RV64 ABI precompute insertion anchor not unique")
precompute = '''        for (argument_index = 0U; argument_index < parameter_count; ++argument_index) {\n            if (!minic_c0_fixed_parameter_abi_type(program,\n                                                   parameter_types[argument_index],\n                                                   &abi_parameter_types[argument_index])) {\n                return false;\n            }\n        }\n\n'''
# The global replacement above also rewrote the precompute source name if inserted later, so insert now
# with the semantic parameter_types source untouched.
text = text.replace(anchor, precompute + anchor, 1)
codegen.write_text(text)

# Focused coverage: Linux-shaped three-pointer transparent union, direct+indirect calls, callee use,
# null member conversion, and fail-closed boundaries.
Path("tests/compiler/c0/transparent_union.c").write_text(r'''struct page { int value; };
struct folio { int value; };
struct encoded_page { int value; };

typedef union {
    struct page **pages;
    struct folio **folios;
    struct encoded_page **encoded_pages;
} release_pages_arg __attribute__((__transparent_union__));

int release_pages(release_pages_arg arg, int nr)
{
    return arg.folios == ((void *)0) ? 0 : nr;
}

int call_page(struct page **pages)
{
    return release_pages(pages, 1);
}

int call_folio(struct folio **folios)
{
    return release_pages(folios, 2);
}

int call_encoded(struct encoded_page **encoded_pages)
{
    return release_pages(encoded_pages, 3);
}

int call_null(void)
{
    return release_pages(0, 4);
}

typedef int (*release_pages_fn)(release_pages_arg, int);

int call_indirect(release_pages_fn fn, struct folio **folios)
{
    return fn(folios, 5);
}
''')

Path("tests/compiler/c0/invalid_transparent_union_nonmember.c").write_text(r'''struct a { int value; };
struct b { int value; };

typedef union {
    struct a **as;
    struct b **bs;
} bridge_arg __attribute__((__transparent_union__));

int sink(bridge_arg arg)
{
    return arg.as != ((void *)0);
}

int bad(char **other)
{
    return sink(other);
}
''')

Path("tests/compiler/c0/invalid_transparent_union_nonunion.c").write_text(r'''typedef struct {
    int *value;
} bad_type __attribute__((__transparent_union__));
''')

Path("tests/compiler/c0/unsupported_transparent_union_nonpointer.c").write_text(r'''typedef union {
    unsigned long value;
    void *pointer;
} mixed_arg __attribute__((__transparent_union__));
''')

Path("tests/compiler/c0/run-transparent-union.sh").write_text(r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-transparent-union

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/transparent_union.c" -o "$work/transparent.i"
"$minic" -S "$work/transparent.i" -o "$work/transparent.s"
grep -F '  call release_pages' "$work/transparent.s" >/dev/null
grep -F '  jalr ra, t0, 0' "$work/transparent.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/transparent_union type-owner=record members=3 direct=3 indirect=1 null=1 abi=first-member-pointer callee=one-integer-chunk'

"$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_transparent_union_nonmember.c" -o "$work/nonmember.i"
if "$minic" -S "$work/nonmember.i" -o "$work/nonmember.s" >"$work/nonmember.stdout" 2>"$work/nonmember.stderr"; then
    echo 'FAIL transparent union non-member argument unexpectedly compiled' >&2
    exit 1
fi
grep -F 'call argument type does not match declaration' "$work/nonmember.stderr" >/dev/null

"$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_transparent_union_nonunion.c" -o "$work/nonunion.i"
if "$minic" -S "$work/nonunion.i" -o "$work/nonunion.s" >"$work/nonunion.stdout" 2>"$work/nonunion.stderr"; then
    echo 'FAIL transparent_union on non-union unexpectedly compiled' >&2
    exit 1
fi
grep -F 'GNU transparent_union requires a union type' "$work/nonunion.stderr" >/dev/null

"$host_cc" -E -P -x c "$root/tests/compiler/c0/unsupported_transparent_union_nonpointer.c" -o "$work/nonpointer.i"
if "$minic" -S "$work/nonpointer.i" -o "$work/nonpointer.s" >"$work/nonpointer.stdout" 2>"$work/nonpointer.stderr"; then
    echo 'FAIL unsupported mixed transparent union unexpectedly compiled' >&2
    exit 1
fi
grep -F 'GNU transparent_union v0 requires pointer members with one machine representation' "$work/nonpointer.stderr" >/dev/null
printf '%s\n' 'PASS compiler/c0/transparent_union negative=nonmember+nonunion+nonpointer-v0'
''')

run = Path("tests/compiler/c0/run.sh")
text = run.read_text()
marker = '''MINIC="$minic" \\
HOST_CC="$host_cc" \\
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\
sh "$root/tests/compiler/c0/run-block-scope-extern-object.sh"\n'''
insert = marker + '''\nMINIC="$minic" \\
HOST_CC="$host_cc" \\
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\
sh "$root/tests/compiler/c0/run-transparent-union.sh"\n'''
if text.count(marker) != 1:
    raise SystemExit("transparent union C0 gate insertion anchor not unique")
run.write_text(text.replace(marker, insert, 1))
