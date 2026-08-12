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


# Array type identity: count==0 alone no longer conflates incomplete and GNU zero-length.
replace_once(
    "src/frontend/ast.h",
    '''typedef struct MinicArrayType {\n    MinicType element_type;\n    size_t element_count;\n} MinicArrayType;\n''',
    '''typedef struct MinicArrayType {\n    MinicType element_type;\n    size_t element_count;\n    bool is_zero_length;\n} MinicArrayType;\n''',
    "array type zero-length identity",
)
replace_once(
    "src/frontend/ast.h",
    '''bool minic_c0_program_add_incomplete_array_type(MinicC0Program *program,\n                                                MinicType element_type,\n                                                MinicType *array_type);\nbool minic_c0_program_complete_array_type(MinicC0Program *program,\n''',
    '''bool minic_c0_program_add_incomplete_array_type(MinicC0Program *program,\n                                                MinicType element_type,\n                                                MinicType *array_type);\nbool minic_c0_program_add_zero_length_array_type(MinicC0Program *program,\n                                                 MinicType element_type,\n                                                 MinicType *array_type);\nbool minic_c0_program_complete_zero_length_array_type(MinicC0Program *program,\n                                                      MinicType array_type);\nbool minic_c0_program_complete_array_type(MinicC0Program *program,\n''',
    "zero-length array API",
)

# Program constructors and completion semantics.
ast = Path("src/frontend/ast.c")
text = ast.read_text()
anchor = '''bool minic_c0_program_add_incomplete_array_type(MinicC0Program *program,\n                                                MinicType element_type,\n                                                MinicType *array_type) {\n    return minic_c0_program_add_array_descriptor(program, element_type, 0U, array_type);\n}\n\n'''
addition = anchor + '''bool minic_c0_program_add_zero_length_array_type(MinicC0Program *program,\n                                                 MinicType element_type,\n                                                 MinicType *array_type) {\n    MinicType created;\n\n    if (program == NULL || array_type == NULL ||\n        !minic_c0_program_add_array_descriptor(program, element_type, 0U, &created)) {\n        return false;\n    }\n    program->array_types[created.array_type_id].is_zero_length = true;\n    *array_type = created;\n    return true;\n}\n\nbool minic_c0_program_complete_zero_length_array_type(MinicC0Program *program,\n                                                      MinicType array_type) {\n    MinicArrayType *descriptor;\n\n    if (program == NULL || !minic_type_is_array(array_type) ||\n        array_type.array_type_id >= program->array_type_count) {\n        return false;\n    }\n    descriptor = &program->array_types[array_type.array_type_id];\n    if (descriptor->is_zero_length) {\n        return descriptor->element_count == 0U;\n    }\n    if (descriptor->element_count != 0U) {\n        return false;\n    }\n    descriptor->is_zero_length = true;\n    return true;\n}\n\n'''
if text.count(anchor) != 1:
    raise SystemExit("incomplete-array constructor anchor not unique")
text = text.replace(anchor, addition, 1)
text = text.replace(
    '''    if (descriptor->element_count != 0U) {\n        return descriptor->element_count == element_count;\n    }\n''',
    '''    if (descriptor->is_zero_length) {\n        return false;\n    }\n    if (descriptor->element_count != 0U) {\n        return descriptor->element_count == element_count;\n    }\n''',
    1,
)
# Materialized array object info must distinguish zero-length from incomplete everywhere.
text = text.replace(
    '''            resolved.element_count = array_type->element_count;\n            resolved.is_incomplete = array_type->element_count == 0U;\n            resolved.has_materialized_type = true;\n''',
    '''            resolved.element_count = array_type->element_count;\n            resolved.is_zero_length = array_type->is_zero_length;\n            resolved.is_incomplete =\n                array_type->element_count == 0U && !array_type->is_zero_length;\n            resolved.has_materialized_type = true;\n'''
)
ast.write_text(text)

# Shared array declarator parses GNU zero bounds as a distinct complete array type.
decl = Path("src/frontend/parser_declarator.c")
text = decl.read_text()
insert_anchor = '''bool minic_parser_parse_array_declarator_suffix(MinicParser *parser,\n'''
helper = '''static bool parse_array_bound_allow_zero(MinicParser *parser, size_t *element_count) {\n    int64_t value;\n\n    if (parser == NULL || element_count == NULL ||\n        !minic_parser_parse_integer_constant_expression(parser, &value)) {\n        return false;\n    }\n    if (value < 0) {\n        minic_parser_error(parser, "array bound must not be negative");\n        return false;\n    }\n    if ((uint64_t)value > (uint64_t)SIZE_MAX) {\n        minic_parser_error(parser, "array bound exceeds target object range");\n        return false;\n    }\n    if (!minic_parser_expect(parser, MINIC_TOKEN_RBRACKET, "expected ']'") ) {\n        return false;\n    }\n    *element_count = (size_t)value;\n    return true;\n}\n\n'''
if text.count(insert_anchor) != 1:
    raise SystemExit("array declarator helper insertion anchor not unique")
text = text.replace(insert_anchor, helper + insert_anchor, 1)
text = text.replace(
    '''    bool outermost_incomplete;\n    MinicType type;\n''',
    '''    bool zero_length[8];\n    bool outermost_incomplete;\n    MinicType type;\n''',
    1,
)
text = text.replace(
    '''    bound_count = 0U;\n    outermost_incomplete = false;\n''',
    '''    bound_count = 0U;\n    (void)memset(zero_length, 0, sizeof(zero_length));\n    outermost_incomplete = false;\n''',
    1,
)
text = text.replace(
    '''        } else if (!minic_parser_parse_fixed_array_bound(parser, &bounds[bound_count])) {\n            return false;\n        }\n        bound_count += 1U;\n''',
    '''        } else if (!parse_array_bound_allow_zero(parser, &bounds[bound_count])) {\n            return false;\n        } else {\n            zero_length[bound_count] = bounds[bound_count] == 0U;\n        }\n        bound_count += 1U;\n''',
    1,
)
text = text.replace(
    '''        if (dimension == 0U && outermost_incomplete) {\n            if (!minic_c0_program_add_incomplete_array_type(parser->program, type, &type)) {\n                minic_parser_error(parser, "cannot build incomplete array declarator type");\n                return false;\n            }\n        } else if (!minic_c0_program_add_array_type(\n                       parser->program, type, bounds[dimension], &type)) {\n''',
    '''        if (dimension == 0U && outermost_incomplete) {\n            if (!minic_c0_program_add_incomplete_array_type(parser->program, type, &type)) {\n                minic_parser_error(parser, "cannot build incomplete array declarator type");\n                return false;\n            }\n        } else if (zero_length[dimension]) {\n            if (!minic_c0_program_add_zero_length_array_type(parser->program, type, &type)) {\n                minic_parser_error(parser, "cannot build GNU zero-length array declarator type");\n                return false;\n            }\n        } else if (!minic_c0_program_add_array_type(\n                       parser->program, type, bounds[dimension], &type)) {\n''',
    1,
)
decl.write_text(text)

# DataLayout: zero-length is complete, size 0, element alignment preserved.
dl = Path("src/target/data_layout.c")
text = dl.read_text()
old = '''        array_type = minic_c0_program_array_type(program, type.array_type_id);\n        if (array_type == NULL || array_type->element_count == 0U ||\n            !minic_data_layout_type_depth(layout,\n                                          program,\n                                          array_type->element_type,\n                                          depth + 1U,\n                                          &element_size,\n                                          &element_alignment) ||\n            element_size > SIZE_MAX / array_type->element_count) {\n            return false;\n        }\n        *size = element_size * array_type->element_count;\n        *alignment = element_alignment;\n'''
new = '''        array_type = minic_c0_program_array_type(program, type.array_type_id);\n        if (array_type == NULL ||\n            (array_type->element_count == 0U && !array_type->is_zero_length) ||\n            !minic_data_layout_type_depth(layout,\n                                          program,\n                                          array_type->element_type,\n                                          depth + 1U,\n                                          &element_size,\n                                          &element_alignment)) {\n            return false;\n        }\n        if (array_type->is_zero_length) {\n            *size = 0U;\n            *alignment = element_alignment;\n            return minic_data_layout_apply_explicit_alignment(type, alignment);\n        }\n        if (element_size > SIZE_MAX / array_type->element_count) {\n            return false;\n        }\n        *size = element_size * array_type->element_count;\n        *alignment = element_alignment;\n'''
if text.count(old) != 1:
    raise SystemExit("DataLayout array anchor not unique")
dl.write_text(text.replace(old, new, 1))

# Extern composite type: only incomplete arrays are wildcard; GNU zero-length is a real bound.
glob = Path("src/frontend/parser_global.c")
text = glob.read_text()
old = '''    return existing_array->element_count == 0U || declared_array->element_count == 0U ||\n           existing_array->element_count == declared_array->element_count;\n'''
new = '''    if ((existing_array->element_count == 0U && !existing_array->is_zero_length) ||\n        (declared_array->element_count == 0U && !declared_array->is_zero_length)) {\n        return true;\n    }\n    return existing_array->is_zero_length == declared_array->is_zero_length &&\n           existing_array->element_count == declared_array->element_count;\n'''
if text.count(old) != 1:
    raise SystemExit("extern array compatibility anchor not unique")
text = text.replace(old, new, 1)
old = '''    if (existing_array->element_count == 0U && declared_count != 0U) {\n        return minic_c0_program_complete_array_type(program, existing_type, declared_count);\n    }\n    return declared_count == 0U || existing_array->element_count == declared_count;\n'''
new = '''    if (existing_array->element_count == 0U && !existing_array->is_zero_length) {\n        if (declared_array->is_zero_length) {\n            return minic_c0_program_complete_zero_length_array_type(program, existing_type);\n        }\n        if (declared_count != 0U) {\n            return minic_c0_program_complete_array_type(program, existing_type, declared_count);\n        }\n        return true;\n    }\n    if (declared_count == 0U && !declared_array->is_zero_length) {\n        return true;\n    }\n    return existing_array->is_zero_length == declared_array->is_zero_length &&\n           existing_array->element_count == declared_count;\n'''
if text.count(old) != 1:
    raise SystemExit("extern array composite anchor not unique")
glob.write_text(text.replace(old, new, 1))

# Focused regression.
Path("tests/compiler/c0/gnu_zero_length_array.c").write_text(r'''typedef long atomic_long_t;
typedef int zero_ints[0];

_Static_assert(sizeof(zero_ints) == 0, "GNU zero-length array sizeof");

extern atomic_long_t vm_numa_event[];
extern atomic_long_t vm_numa_event[0];

int *decay_zero(zero_ints *holder)
{
    return *holder;
}

int main(void)
{
    return sizeof(zero_ints) == 0 ? 0 : 1;
}
''')
Path("tests/compiler/c0/invalid_zero_length_array_redeclaration.c").write_text(r'''extern int clash[0];
extern int clash[1];
int main(void) { return 0; }
''')
Path("tests/compiler/c0/run-gnu-zero-length-array.sh").write_text(r'''#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-zero-length-array
rm -rf "$work"; mkdir -p "$work"
"$host_cc" -E -P -x c "$root/tests/compiler/c0/gnu_zero_length_array.c" -o "$work/zero.i"
"$minic" -S "$work/zero.i" -o "$work/zero.s"
grep -F 'vm_numa_event' "$work/zero.s" >/dev/null || true
printf '%s\n' 'PASS compiler/c0/gnu_zero_length_array extern=1 incomplete-to-zero=1 sizeof=0 decay=1 type-identity=complete-zero'
"$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_zero_length_array_redeclaration.c" -o "$work/conflict.i"
if "$minic" -S "$work/conflict.i" -o "$work/conflict.s" >"$work/conflict.stdout" 2>"$work/conflict.stderr"; then
  echo 'FAIL zero-length vs positive-bound redeclaration unexpectedly compiled' >&2
  exit 1
fi
grep -F 'conflicting extern object redeclaration' "$work/conflict.stderr" >/dev/null
printf '%s\n' 'PASS compiler/c0/invalid_zero_length_array_redeclaration zero-vs-one=conflict'
''')
run = Path("tests/compiler/c0/run.sh")
text = run.read_text()
text += '''\nMINIC="$minic" \\
HOST_CC="$host_cc" \\
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\
sh "$root/tests/compiler/c0/run-gnu-zero-length-array.sh"\n'''
run.write_text(text)
