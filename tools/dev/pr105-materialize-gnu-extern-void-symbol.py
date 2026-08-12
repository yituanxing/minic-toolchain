#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def replace_once(path: str, old: str, new: str) -> None:
    p = ROOT / path
    text = p.read_text()
    if text.count(old) != 1:
        raise SystemExit(f"{path}: expected exactly one anchor, found {text.count(old)}")
    p.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/ast.h",
    """bool minic_c0_program_add_global_object(MinicC0Program *program,\n                                        const char *name,\n                                        size_t name_length,\n                                        MinicType type,\n                                        bool is_internal,\n                                        bool is_read_only,\n                                        MinicGlobalObjectId *global_object_id);\n""",
    """bool minic_c0_program_add_global_object(MinicC0Program *program,\n                                        const char *name,\n                                        size_t name_length,\n                                        MinicType type,\n                                        bool is_internal,\n                                        bool is_read_only,\n                                        MinicGlobalObjectId *global_object_id);\nbool minic_c0_program_add_extern_global_object(MinicC0Program *program,\n                                               const char *name,\n                                               size_t name_length,\n                                               MinicType type,\n                                               bool is_read_only,\n                                               MinicGlobalObjectId *global_object_id);\n""",
)

replace_once(
    "src/frontend/ast_global.c",
    """bool minic_c0_program_add_global_object(MinicC0Program *program,\n                                        const char *name,\n                                        size_t name_length,\n                                        MinicType type,\n                                        bool is_internal,\n                                        bool is_read_only,\n                                        MinicGlobalObjectId *global_object_id) {\n    MinicGlobalObject object;\n\n    if (program == NULL || name == NULL || global_object_id == NULL || minic_type_is_void(type) ||\n        name_conflicts(program, name, name_length)) {\n        return false;\n    }\n    if (!grow_array((void **)&program->global_objects,\n                    &program->global_object_capacity,\n                    program->global_object_count,\n                    sizeof(*program->global_objects))) {\n        return false;\n    }\n\n    (void)memset(&object, 0, sizeof(object));\n    object.name = copy_name(name, name_length);\n    if (object.name == NULL) {\n        return false;\n    }\n    object.name_length = name_length;\n    object.type = type;\n    object.is_internal = is_internal;\n    object.is_read_only = is_read_only;\n    *global_object_id = program->global_object_count;\n    program->global_objects[program->global_object_count] = object;\n    program->global_object_count += 1U;\n    return true;\n}\n""",
    """static bool add_global_object_entity(MinicC0Program *program,\n                                     const char *name,\n                                     size_t name_length,\n                                     MinicType type,\n                                     bool is_internal,\n                                     bool is_read_only,\n                                     bool is_extern,\n                                     MinicGlobalObjectId *global_object_id) {\n    MinicGlobalObject object;\n\n    if (program == NULL || name == NULL || global_object_id == NULL ||\n        (minic_type_is_void(type) && !is_extern) || name_conflicts(program, name, name_length)) {\n        return false;\n    }\n    if (!grow_array((void **)&program->global_objects,\n                    &program->global_object_capacity,\n                    program->global_object_count,\n                    sizeof(*program->global_objects))) {\n        return false;\n    }\n\n    (void)memset(&object, 0, sizeof(object));\n    object.name = copy_name(name, name_length);\n    if (object.name == NULL) {\n        return false;\n    }\n    object.name_length = name_length;\n    object.type = type;\n    object.is_internal = is_internal;\n    object.is_read_only = is_read_only;\n    object.is_extern = is_extern;\n    *global_object_id = program->global_object_count;\n    program->global_objects[program->global_object_count] = object;\n    program->global_object_count += 1U;\n    return true;\n}\n\nbool minic_c0_program_add_global_object(MinicC0Program *program,\n                                        const char *name,\n                                        size_t name_length,\n                                        MinicType type,\n                                        bool is_internal,\n                                        bool is_read_only,\n                                        MinicGlobalObjectId *global_object_id) {\n    return add_global_object_entity(program,\n                                    name,\n                                    name_length,\n                                    type,\n                                    is_internal,\n                                    is_read_only,\n                                    false,\n                                    global_object_id);\n}\n\nbool minic_c0_program_add_extern_global_object(MinicC0Program *program,\n                                               const char *name,\n                                               size_t name_length,\n                                               MinicType type,\n                                               bool is_read_only,\n                                               MinicGlobalObjectId *global_object_id) {\n    return add_global_object_entity(\n        program, name, name_length, type, false, is_read_only, true, global_object_id);\n}\n""",
)

replace_once(
    "src/frontend/parser_global.c",
    """        if (minic_type_is_void(object_type) || minic_type_is_function(object_type) ||\n            minic_type_is_array(object_type)) {\n            minic_parser_error(parser, \"unsupported extern object type\");\n            return false;\n        }\n""",
    """        if (minic_type_is_function(object_type) || minic_type_is_array(object_type)) {\n            minic_parser_error(parser, \"unsupported extern object type\");\n            return false;\n        }\n""",
)

replace_once(
    "src/frontend/parser_global.c",
    """        } else if (!minic_c0_program_add_global_object(parser->program,\n                                                       parser->source + name_span.begin.offset,\n                                                       minic_parser_span_length(name_span),\n                                                       object_type,\n                                                       false,\n                                                       minic_type_is_const(declarator_element_type),\n                                                       &object_id) ||\n                   !minic_c0_global_object_set_extern(parser->program, object_id) ||\n                   (declarator_has_section &&\n""",
    """        } else if (!minic_c0_program_add_extern_global_object(\n                       parser->program,\n                       parser->source + name_span.begin.offset,\n                       minic_parser_span_length(name_span),\n                       object_type,\n                       minic_type_is_const(declarator_element_type),\n                       &object_id) ||\n                   (declarator_has_section &&\n""",
)

replace_once(
    "src/frontend/ast_verifier.c",
    """        if (object->name == NULL || !type_is_valid(program, object->type) ||\n            minic_type_is_function(object->type) ||\n            (object->is_extern &&\n""",
    """        if (object->name == NULL || !type_is_valid(program, object->type) ||\n            minic_type_is_function(object->type) ||\n            (minic_type_is_void(object->type) && !object->is_extern) ||\n            (object->is_extern &&\n""",
)

(ROOT / "tests/compiler/c0/gnu_extern_void_symbol.c").write_text(
    """extern __attribute__((__externally_visible__)) const void __nosave_begin, __nosave_end;\n\nint main(void) {\n    return &__nosave_begin != &__nosave_end;\n}\n"""
)
(ROOT / "tests/compiler/c0/invalid_extern_void_sizeof.c").write_text(
    """extern const void opaque_symbol;\n\nint main(void) {\n    return (int)sizeof(opaque_symbol);\n}\n"""
)
(ROOT / "tests/compiler/c0/invalid_void_object_definition.c").write_text(
    """const void opaque_symbol;\n\nint main(void) {\n    return 0;\n}\n"""
)
(ROOT / "tests/compiler/c0/invalid_extern_void_redeclaration.c").write_text(
    """extern const void opaque_symbol;\nextern const int opaque_symbol;\n\nint main(void) {\n    return 0;\n}\n"""
)

(ROOT / "tests/compiler/c0/run-gnu-extern-void-symbol.sh").write_text(
    r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug/gnu-extern-void-symbol"}
mkdir -p "$work"

preprocess() {
    name=$1
    "$host_cc" -E -P -x c "$root/tests/compiler/c0/$name.c" -o "$work/$name.i"
}

expect_failure() {
    name=$1
    expected=${2:-}
    preprocess "$name"
    if "$minic" -S "$work/$name.i" -o "$work/$name.s" >"$work/$name.stdout" 2>"$work/$name.stderr"; then
        echo "FAIL compiler/c0/$name: compilation unexpectedly succeeded" >&2
        exit 1
    fi
    if test -n "$expected" && ! grep -F "$expected" "$work/$name.stderr" >/dev/null; then
        echo "FAIL compiler/c0/$name: diagnostic mismatch" >&2
        cat "$work/$name.stderr" >&2
        exit 1
    fi
}

preprocess gnu_extern_void_symbol
"$minic" -S "$work/gnu_extern_void_symbol.i" -o "$work/gnu_extern_void_symbol.s"
grep -F "  la a0, __nosave_begin" "$work/gnu_extern_void_symbol.s" >/dev/null
grep -F "  la a0, __nosave_end" "$work/gnu_extern_void_symbol.s" >/dev/null
if grep -F ".type __nosave_begin, @object" "$work/gnu_extern_void_symbol.s" >/dev/null || \
   grep -F ".type __nosave_end, @object" "$work/gnu_extern_void_symbol.s" >/dev/null || \
   grep -F "__nosave_begin:" "$work/gnu_extern_void_symbol.s" >/dev/null || \
   grep -F "__nosave_end:" "$work/gnu_extern_void_symbol.s" >/dev/null; then
    echo "FAIL compiler/c0/gnu_extern_void_symbol: extern-only symbol emitted storage" >&2
    exit 1
fi
expect_failure invalid_extern_void_sizeof "sizeof requires a supported complete type"
expect_failure invalid_void_object_definition
expect_failure invalid_extern_void_redeclaration "conflicting extern object redeclaration"
printf '%s\n' "PASS compiler/c0/gnu_extern_void_symbol extern-only=1 opaque-void=1 multi-declarator=2 address=rv64-la storage=none sizeof=reject definition=reject redeclaration=reject"
'''
)

run = ROOT / "tests/compiler/c0/run.sh"
run_text = run.read_text()
line = 'MINIC="$minic" BUILD_DIR="$work/gnu-extern-void-symbol" HOST_CC="$host_cc" sh "$root/tests/compiler/c0/run-gnu-extern-void-symbol.sh"\n'
if line not in run_text:
    if not run_text.endswith("\n"):
        run_text += "\n"
    run_text += "\n" + line
    run.write_text(run_text)
