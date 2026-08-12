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


def replace_regex_once(path: str, pattern: str, replacement: str, label: str) -> None:
    p = Path(path)
    text = p.read_text()
    updated, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if count != 1:
        raise SystemExit(f"{label}: expected one regex anchor, found {count}")
    p.write_text(updated)


# Program entity owns linkage/storage plus the narrow fact that no file-scope
# declaration has made this source name globally visible yet.
replace_once(
    "src/frontend/ast.h",
    '''    bool is_zero_initialized;\n    bool is_extern;\n} MinicGlobalObject;\n''',
    '''    bool is_zero_initialized;\n    bool is_extern;\n    bool is_block_scope_extern_only;\n} MinicGlobalObject;\n''',
    "global object block-scope visibility bit",
)

# Generalize the existing scope -> GlobalObjectId seam that static locals were
# already using.  No new binding representation is needed.
for path in ["src/frontend/parser_internal.h", "src/frontend/parser_core.c", "src/frontend/parser_global.c", "src/frontend/parser_statement.c"]:
    p = Path(path)
    text = p.read_text()
    text = text.replace("minic_parser_bind_static_local", "minic_parser_bind_scoped_global_object")
    text = text.replace("minic_parser_find_static_local", "minic_parser_find_scoped_global_object")
    p.write_text(text)

replace_once(
    "src/frontend/parser_internal.h",
    '''MinicGlobalObjectId minic_parser_find_scoped_global_object(const MinicParser *parser,\n                                                   MinicSourceSpan name_span);\n''',
    '''MinicGlobalObjectId minic_parser_find_scoped_global_object(const MinicParser *parser,\n                                                           MinicSourceSpan name_span);\nMinicGlobalObjectId minic_parser_find_scoped_global_object_in_current_scope(\n    const MinicParser *parser, MinicSourceSpan name_span);\n''',
    "current-scope global binding declaration",
)

replace_once(
    "src/frontend/parser_internal.h",
    '''MinicGlobalObjectId minic_parser_find_global_object(const MinicParser *parser,\n                                                    MinicSourceSpan name_span);\n''',
    '''MinicGlobalObjectId minic_parser_find_global_object(const MinicParser *parser,\n                                                    MinicSourceSpan name_span);\nMinicGlobalObjectId minic_parser_find_global_object_entity(const MinicParser *parser,\n                                                           MinicSourceSpan name_span);\nbool minic_parser_declare_block_scope_extern_object(MinicParser *parser,\n                                                    MinicSourceSpan name_span,\n                                                    MinicType object_type,\n                                                    MinicGlobalObjectId *object_id);\n''',
    "raw global entity and block extern declarations",
)

# Add current-scope global binding lookup next to the existing local lookup.
core = Path("src/frontend/parser_core.c")
text = core.read_text()
anchor = '''MinicLocalId minic_parser_find_local_in_current_scope(const MinicParser *parser,\n                                                      MinicSourceSpan name_span) {\n    size_t scope_begin;\n    size_t index;\n\n    if (parser->scope_count == 0U) {\n        return MINIC_LOCAL_INVALID;\n    }\n    scope_begin = parser->scopes[parser->scope_count - 1U].binding_begin;\n    for (index = parser->local_binding_count; index > scope_begin; --index) {\n        const MinicParserLocalBinding *binding;\n\n        binding = &parser->local_bindings[index - 1U];\n        if (binding->local_id != MINIC_LOCAL_INVALID &&\n            minic_parser_span_equals(parser, name_span, binding->name_span)) {\n            return binding->local_id;\n        }\n    }\n    return MINIC_LOCAL_INVALID;\n}\n'''
addition = anchor + '''\nMinicGlobalObjectId minic_parser_find_scoped_global_object_in_current_scope(\n    const MinicParser *parser, MinicSourceSpan name_span) {\n    size_t scope_begin;\n    size_t index;\n\n    if (parser == NULL || parser->scope_count == 0U) {\n        return MINIC_GLOBAL_OBJECT_INVALID;\n    }\n    scope_begin = parser->scopes[parser->scope_count - 1U].binding_begin;\n    for (index = parser->local_binding_count; index > scope_begin; --index) {\n        const MinicParserLocalBinding *binding;\n\n        binding = &parser->local_bindings[index - 1U];\n        if (binding->global_object_id != MINIC_GLOBAL_OBJECT_INVALID &&\n            minic_parser_span_equals(parser, name_span, binding->name_span)) {\n            return binding->global_object_id;\n        }\n    }\n    return MINIC_GLOBAL_OBJECT_INVALID;\n}\n'''
if text.count(anchor) != 1:
    raise SystemExit("current-scope local lookup anchor not unique")
core.write_text(text.replace(anchor, addition, 1))

# Separate raw entity identity from source-name visibility.
global_path = Path("src/frontend/parser_global.c")
text = global_path.read_text()
pattern = r'''MinicGlobalObjectId minic_parser_find_global_object\(const MinicParser \*parser,\n                                                    MinicSourceSpan name_span\) \{.*?\n}\n\n(?=MinicFixedRegisterBindingId)'''
replacement = r'''MinicGlobalObjectId minic_parser_find_global_object_entity(const MinicParser *parser,
                                                           MinicSourceSpan name_span) {
    size_t name_length;
    size_t index;

    if (parser == NULL || parser->program == NULL) {
        return MINIC_GLOBAL_OBJECT_INVALID;
    }
    name_length = minic_parser_span_length(name_span);
    for (index = 0U; index < parser->program->global_object_count; ++index) {
        const MinicGlobalObject *object;

        object = minic_c0_program_global_object(parser->program, index);
        if (object != NULL && object->name_length == name_length &&
            memcmp(object->name, parser->source + name_span.begin.offset, name_length) == 0) {
            return index;
        }
    }
    return MINIC_GLOBAL_OBJECT_INVALID;
}

MinicGlobalObjectId minic_parser_find_global_object(const MinicParser *parser,
                                                    MinicSourceSpan name_span) {
    MinicGlobalObjectId object_id;
    const MinicGlobalObject *object;

    object_id = minic_parser_find_scoped_global_object(parser, name_span);
    if (object_id != MINIC_GLOBAL_OBJECT_INVALID) {
        return object_id;
    }
    object_id = minic_parser_find_global_object_entity(parser, name_span);
    object = object_id == MINIC_GLOBAL_OBJECT_INVALID
                 ? NULL
                 : minic_c0_program_global_object(parser->program, object_id);
    return object != NULL && !object->is_block_scope_extern_only
               ? object_id
               : MINIC_GLOBAL_OBJECT_INVALID;
}

'''
updated, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
if count != 1:
    raise SystemExit(f"global object visible/raw lookup replacement count={count}")
global_path.write_text(updated)

# Reuse the existing extern redeclaration/composite-type merge owner for block
# declarations instead of duplicating type rules in parser_statement.c.
text = global_path.read_text()
merge_end = '''    return true;\n}\n\nbool minic_parser_parse_extern_global_after_head(MinicParser *parser,\n'''
helper = '''    return true;\n}\n\nbool minic_parser_declare_block_scope_extern_object(MinicParser *parser,\n                                                    MinicSourceSpan name_span,\n                                                    MinicType object_type,\n                                                    MinicGlobalObjectId *object_id) {\n    MinicGlobalObjectId existing_id;\n\n    if (parser == NULL || object_id == NULL || minic_type_is_void(object_type) ||\n        minic_type_is_function(object_type)) {\n        if (parser != NULL) {\n            minic_parser_error(parser, "invalid block-scope extern object type");\n        }\n        return false;\n    }\n    existing_id = minic_parser_find_global_object_entity(parser, name_span);\n    if (existing_id != MINIC_GLOBAL_OBJECT_INVALID) {\n        if (!merge_extern_object_declaration(parser,\n                                             existing_id,\n                                             object_type,\n                                             NULL,\n                                             0U,\n                                             false,\n                                             0U,\n                                             MINIC_SYMBOL_VISIBILITY_DEFAULT,\n                                             false)) {\n            return false;\n        }\n        *object_id = existing_id;\n        return true;\n    }\n    if (!minic_c0_program_add_global_object(parser->program,\n                                            parser->source + name_span.begin.offset,\n                                            minic_parser_span_length(name_span),\n                                            object_type,\n                                            false,\n                                            minic_type_is_const(object_type),\n                                            object_id) ||\n        !minic_c0_global_object_set_extern(parser->program, *object_id)) {\n        minic_parser_error(parser, "cannot declare block-scope extern object");\n        return false;\n    }\n    parser->program->global_objects[*object_id].is_block_scope_extern_only = true;\n    return true;\n}\n\nbool minic_parser_parse_extern_global_after_head(MinicParser *parser,\n'''
if text.count(merge_end) != 1:
    raise SystemExit("extern merge helper insertion anchor not unique")
text = text.replace(merge_end, helper, 1)

# File-scope extern declarations must find a block-only entity by identity and
# promote its source visibility rather than creating a duplicate symbol.
old_lookup = '''        object_id = minic_parser_find_global_object(parser, name_span);\n        if (object_id != MINIC_GLOBAL_OBJECT_INVALID) {\n'''
new_lookup = '''        object_id = minic_parser_find_global_object_entity(parser, name_span);\n        if (object_id != MINIC_GLOBAL_OBJECT_INVALID) {\n'''
if text.count(old_lookup) != 1:
    raise SystemExit(f"file extern entity lookup anchor count={text.count(old_lookup)}")
text = text.replace(old_lookup, new_lookup, 1)
promote_anchor = '''            return false;\n        }\n\n        if (parser->current.kind != MINIC_TOKEN_COMMA) {\n'''
promote = '''            return false;\n        }\n        parser->program->global_objects[object_id].is_block_scope_extern_only = false;\n\n        if (parser->current.kind != MINIC_TOKEN_COMMA) {\n'''
# This anchor can occur elsewhere; restrict to the extern-after-head suffix.
pos = text.find("bool minic_parser_parse_extern_global_after_head")
if pos < 0:
    raise SystemExit("extern-after-head function not found")
prefix, suffix = text[:pos], text[pos:]
if suffix.count(promote_anchor) < 1:
    raise SystemExit("extern promotion anchor not found")
suffix = suffix.replace(promote_anchor, promote, 1)
global_path.write_text(prefix + suffix)

# Turn the existing block-scope extern function parser into a declaration
# parser.  Function syntax remains on its old path; non-'(' after the direct
# name becomes a scoped extern object declaration.
statement = Path("src/frontend/parser_statement.c")
text = statement.read_text()
text = text.replace("parse_block_scope_extern_function_declaration", "parse_block_scope_extern_declaration")
object_branch_anchor = '''    if (!minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '('") ||\n        !minic_parser_parse_parameter_list(\n'''
object_branch = '''    if (parser->current.kind != MINIC_TOKEN_LPAREN) {\n        MinicGlobalObjectId current_scope_object;\n        MinicGlobalObjectId object_id;\n        MinicType object_type;\n        bool is_array;\n\n        object_type = return_type;\n        if (!minic_parser_parse_array_declarator_suffix(\n                parser, object_type, true, &object_type, &is_array)) {\n            return false;\n        }\n        (void)is_array;\n        if (parser->current.kind != MINIC_TOKEN_SEMICOLON) {\n            minic_parser_error(\n                parser, "block-scope extern object declaration must end with ';'");\n            return false;\n        }\n        if (!minic_parser_declare_block_scope_extern_object(\n                parser, name_span, object_type, &object_id)) {\n            return false;\n        }\n        current_scope_object =\n            minic_parser_find_scoped_global_object_in_current_scope(parser, name_span);\n        if (minic_parser_name_bound_in_current_scope(parser, name_span)) {\n            if (current_scope_object != object_id) {\n                minic_parser_error(parser,\n                                   "block-scope extern object conflicts with local declaration");\n                return false;\n            }\n        } else if (!minic_parser_bind_scoped_global_object(parser, name_span, object_id)) {\n            return false;\n        }\n        return minic_parser_advance(parser);\n    }\n\n    if (!minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '('") ||\n        !minic_parser_parse_parameter_list(\n'''
if text.count(object_branch_anchor) != 1:
    raise SystemExit(f"block extern function/object split anchor count={text.count(object_branch_anchor)}")
statement.write_text(text.replace(object_branch_anchor, object_branch, 1))

# Focused semantic boundary.
Path("tests/compiler/c0/block_scope_extern_object.c").write_text(r'''unsigned long first(unsigned long pfn)
{
    extern unsigned long zero_pfn;
    extern unsigned long zero_pfn;
    return pfn == zero_pfn;
}

unsigned long second(void)
{
    extern unsigned long zero_pfn;
    return zero_pfn;
}

unsigned long before_promotion(void)
{
    extern unsigned long promoted;
    return promoted;
}

extern unsigned long promoted;

unsigned long after_promotion(void)
{
    return promoted;
}
''')

Path("tests/compiler/c0/invalid_block_scope_extern_leak.c").write_text(r'''unsigned long declaring_function(void)
{
    extern unsigned long hidden;
    return hidden;
}

unsigned long outside_scope(void)
{
    return hidden;
}
''')

Path("tests/compiler/c0/invalid_block_scope_extern_conflict.c").write_text(r'''int conflict(void)
{
    extern unsigned long value;
    extern int value;
    return 0;
}
''')

Path("tests/compiler/c0/run-block-scope-extern-object.sh").write_text(r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-block-scope-extern-object

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/block_scope_extern_object.c" -o "$work/extern.i"
"$minic" -S "$work/extern.i" -o "$work/extern.s"
grep -F 'zero_pfn' "$work/extern.s" >/dev/null
grep -F 'promoted' "$work/extern.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/block_scope_extern_object scoped-global=1 repeated-compatible=1 cross-function-same-entity=1 file-scope-promotion=1'

"$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_block_scope_extern_leak.c" -o "$work/leak.i"
if "$minic" -S "$work/leak.i" -o "$work/leak.s" >"$work/leak.stdout" 2>"$work/leak.stderr"; then
    echo 'FAIL block-scope extern name leaked outside scope' >&2
    exit 1
fi
grep -F 'undeclared' "$work/leak.stderr" >/dev/null
printf '%s\n' 'PASS compiler/c0/invalid_block_scope_extern_leak visibility=scoped'

"$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_block_scope_extern_conflict.c" -o "$work/conflict.i"
if "$minic" -S "$work/conflict.i" -o "$work/conflict.s" >"$work/conflict.stdout" 2>"$work/conflict.stderr"; then
    echo 'FAIL conflicting block-scope extern redeclaration unexpectedly compiled' >&2
    exit 1
fi
grep -F 'conflicting extern object redeclaration' "$work/conflict.stderr" >/dev/null
printf '%s\n' 'PASS compiler/c0/invalid_block_scope_extern_conflict'
''')

run = Path("tests/compiler/c0/run.sh")
run_text = run.read_text()
needle = 'run-block-scope-extern-object.sh'
if needle not in run_text:
    run_text += '''\nMINIC="$minic" \\
HOST_CC="$host_cc" \\
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\
sh "$root/tests/compiler/c0/run-block-scope-extern-object.sh"\n'''
run.write_text(run_text)
