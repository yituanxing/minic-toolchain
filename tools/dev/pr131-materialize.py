from pathlib import Path
import re

root = Path('.')

def replace_once(path, old, new, label):
    p = root / path
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label} mismatch: {count}')
    p.write_text(text.replace(old, new, 1))

# --- AST: one relocation model, one location unit (byte offset). ---
ast_h = root / 'src/frontend/ast.h'
text = ast_h.read_text()
old = '''typedef struct MinicGlobalFunctionRelocation {
    size_t field_index;
    MinicFunctionId function_id;
} MinicGlobalFunctionRelocation;

typedef struct MinicGlobalObjectRelocation {
    size_t element_index;
    MinicGlobalObjectId target_object_id;
} MinicGlobalObjectRelocation;
'''
new = '''typedef enum MinicGlobalRelocationTargetKind {
    MINIC_GLOBAL_RELOCATION_OBJECT = 0,
    MINIC_GLOBAL_RELOCATION_FUNCTION
} MinicGlobalRelocationTargetKind;

typedef struct MinicGlobalRelocation {
    size_t storage_offset;
    MinicGlobalRelocationTargetKind target_kind;
    size_t target_id;
} MinicGlobalRelocation;
'''
if text.count(old) != 1:
    raise SystemExit(f'ast relocation type block mismatch: {text.count(old)}')
text = text.replace(old, new, 1)
old = '''    MinicGlobalFunctionRelocation function_relocations[8];
    size_t function_relocation_count;
    MinicGlobalObjectRelocation *object_relocations;
    size_t object_relocation_count;
    size_t object_relocation_capacity;
'''
new = '''    MinicGlobalRelocation *relocations;
    size_t relocation_count;
    size_t relocation_capacity;
'''
if text.count(old) != 1:
    raise SystemExit(f'ast relocation storage block mismatch: {text.count(old)}')
text = text.replace(old, new, 1)
text = text.replace('size_t field_index,\n                                                    MinicFunctionId function_id);',
                    'size_t storage_offset,\n                                                    MinicFunctionId function_id);', 1)
text = text.replace('size_t element_index,\n                                                  MinicGlobalObjectId target_object_id);',
                    'size_t storage_offset,\n                                                  MinicGlobalObjectId target_object_id);', 1)
ast_h.write_text(text)

# --- Ownership teardown. ---
replace_once('src/frontend/ast.c',
             '        free(program->global_objects[index].object_relocations);',
             '        free(program->global_objects[index].relocations);',
             'ast relocation destroy')

# --- Entity API: unified dynamic symbolic relocation storage. ---
ast_global = root / 'src/frontend/ast_global.c'
text = ast_global.read_text()
text = text.replace('(object->initializer_count != 0U || object->function_relocation_count != 0U ||\n            object->object_relocation_count != 0U || object->is_zero_initialized)',
                    '(object->initializer_count != 0U || object->relocation_count != 0U ||\n            object->is_zero_initialized)', 1)
text = text.replace('object->function_relocation_count != 0U)', 'object->relocation_count != 0U)', 1)

start = text.find('bool minic_c0_global_object_add_function_relocation(')
end = text.find('bool minic_c0_global_object_set_zero_initialized(', start)
if start < 0 or end < 0:
    raise SystemExit('ast_global relocation API region mismatch')
replacement = '''static bool add_global_symbol_relocation(MinicC0Program *program,
                                         MinicGlobalObjectId global_object_id,
                                         size_t storage_offset,
                                         MinicGlobalRelocationTargetKind target_kind,
                                         size_t target_id) {
    MinicGlobalObject *object;
    MinicGlobalRelocation *relocation;

    if (program == NULL || global_object_id >= program->global_object_count ||
        (target_kind != MINIC_GLOBAL_RELOCATION_OBJECT &&
         target_kind != MINIC_GLOBAL_RELOCATION_FUNCTION) ||
        (target_kind == MINIC_GLOBAL_RELOCATION_OBJECT &&
         (target_id >= program->global_object_count || global_object_id == target_id)) ||
        (target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION && target_id >= program->function_count)) {
        return false;
    }
    object = &program->global_objects[global_object_id];
    if (object->is_tentative || object->initializer_count != 0U ||
        (object->relocation_count != 0U &&
         object->relocations[object->relocation_count - 1U].storage_offset >= storage_offset) ||
        !grow_array((void **)&object->relocations,
                    &object->relocation_capacity,
                    object->relocation_count,
                    sizeof(*object->relocations))) {
        return false;
    }
    relocation = &object->relocations[object->relocation_count];
    relocation->storage_offset = storage_offset;
    relocation->target_kind = target_kind;
    relocation->target_id = target_id;
    object->relocation_count += 1U;
    return true;
}

bool minic_c0_global_object_add_function_relocation(MinicC0Program *program,
                                                    MinicGlobalObjectId global_object_id,
                                                    size_t storage_offset,
                                                    MinicFunctionId function_id) {
    return add_global_symbol_relocation(program,
                                        global_object_id,
                                        storage_offset,
                                        MINIC_GLOBAL_RELOCATION_FUNCTION,
                                        function_id);
}

bool minic_c0_global_object_set_extern(MinicC0Program *program,
                                       MinicGlobalObjectId global_object_id) {
    MinicGlobalObject *object;

    if (program == NULL || global_object_id >= program->global_object_count) {
        return false;
    }
    object = &program->global_objects[global_object_id];
    if (object->is_tentative || object->initializer_count != 0U ||
        object->relocation_count != 0U || object->is_zero_initialized || object->is_internal) {
        return false;
    }
    object->is_extern = true;
    return true;
}

bool minic_c0_global_object_add_object_relocation(MinicC0Program *program,
                                                  MinicGlobalObjectId global_object_id,
                                                  size_t storage_offset,
                                                  MinicGlobalObjectId target_object_id) {
    return add_global_symbol_relocation(program,
                                        global_object_id,
                                        storage_offset,
                                        MINIC_GLOBAL_RELOCATION_OBJECT,
                                        target_object_id);
}

'''
text = text[:start] + replacement + text[end:]
ast_global.write_text(text)

# --- Verifier: one relocation store and target-kind validation. ---
verifier = root / 'src/frontend/ast_verifier.c'
text = verifier.read_text()
old = '''            (object->is_extern &&
             (object->is_tentative || object->is_internal || object->is_zero_initialized ||
              object->initializer_count != 0U || object->function_relocation_count != 0U ||
              object->object_relocation_count != 0U)) ||
            (object->is_tentative &&
             (object->is_extern || object->is_zero_initialized || object->initializer_count != 0U ||
              object->function_relocation_count != 0U || object->object_relocation_count != 0U)) ||
            (object->is_zero_initialized && object->initializer_count != 0U) ||
            (object->object_relocation_count != 0U &&
             (!object->is_zero_initialized || object->function_relocation_count != 0U ||
              object->initializer_count != 0U)) ||
            !storage_is_valid(object->initializer_values,
                              object->initializer_count,
                              object->initializer_capacity) ||
            !storage_is_valid(object->object_relocations,
                              object->object_relocation_count,
                              object->object_relocation_capacity)) {
            return false;
        }
        {
            size_t relocation_index;

            for (relocation_index = 0U; relocation_index < object->object_relocation_count;
                 ++relocation_index) {
                if (object->object_relocations[relocation_index].target_object_id >=
                    program->global_object_count) {
                    return false;
                }
            }
        }
'''
new = '''            (object->is_extern &&
             (object->is_tentative || object->is_internal || object->is_zero_initialized ||
              object->initializer_count != 0U || object->relocation_count != 0U)) ||
            (object->is_tentative &&
             (object->is_extern || object->is_zero_initialized || object->initializer_count != 0U ||
              object->relocation_count != 0U)) ||
            (object->is_zero_initialized && object->initializer_count != 0U) ||
            (object->relocation_count != 0U &&
             (!object->is_zero_initialized || object->initializer_count != 0U)) ||
            !storage_is_valid(object->initializer_values,
                              object->initializer_count,
                              object->initializer_capacity) ||
            !storage_is_valid(
                object->relocations, object->relocation_count, object->relocation_capacity)) {
            return false;
        }
        {
            size_t relocation_index;

            for (relocation_index = 0U; relocation_index < object->relocation_count;
                 ++relocation_index) {
                const MinicGlobalRelocation *relocation;

                relocation = &object->relocations[relocation_index];
                if ((relocation_index != 0U &&
                     object->relocations[relocation_index - 1U].storage_offset >=
                         relocation->storage_offset) ||
                    (relocation->target_kind == MINIC_GLOBAL_RELOCATION_OBJECT &&
                     (relocation->target_id >= program->global_object_count ||
                      relocation->target_id == index)) ||
                    (relocation->target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION &&
                     relocation->target_id >= program->function_count) ||
                    (relocation->target_kind != MINIC_GLOBAL_RELOCATION_OBJECT &&
                     relocation->target_kind != MINIC_GLOBAL_RELOCATION_FUNCTION)) {
                    return false;
                }
            }
        }
'''
if text.count(old) != 1:
    raise SystemExit(f'verifier global relocation block mismatch: {text.count(old)}')
verifier.write_text(text.replace(old, new, 1))

# --- RV64: one storage-offset emitter. ---
codegen = root / 'src/target/riscv64/codegen_function.c'
text = codegen.read_text()
start = text.find('static bool\nemit_fn_relocs(')
end = text.find('static bool minic_riscv64_emit_direct_record_values(', start)
if start < 0 or end < 0:
    raise SystemExit('RV64 relocation emitter region mismatch')
replacement = '''static bool emit_symbol_relocs(FILE *file,
                               const MinicC0Program *program,
                               const MinicGlobalObject *object) {
    MinicType pointer_type;
    size_t pointer_width;
    size_t pointer_alignment;
    size_t cursor;
    size_t relocation_index;

    if (file == NULL || program == NULL || object == NULL || !object->is_zero_initialized ||
        object->relocation_count == 0U || object->initializer_count != 0U ||
        !minic_type_pointer_to(minic_type_void(), &pointer_type) ||
        !minic_riscv64_type_layout(program, pointer_type, &pointer_width, &pointer_alignment) ||
        pointer_width != 8U) {
        return false;
    }
    (void)pointer_alignment;

    cursor = 0U;
    for (relocation_index = 0U; relocation_index < object->relocation_count;
         ++relocation_index) {
        const MinicGlobalRelocation *relocation;
        const char *target_name;

        relocation = &object->relocations[relocation_index];
        target_name = NULL;
        if (relocation->target_kind == MINIC_GLOBAL_RELOCATION_OBJECT) {
            const MinicGlobalObject *target;

            target = minic_c0_program_global_object(program, relocation->target_id);
            if (target != NULL && target->name_length != 0U) {
                target_name = target->name;
            }
        } else if (relocation->target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION) {
            const MinicFunction *target;

            target = minic_c0_program_function(program, relocation->target_id);
            if (target != NULL && target->name_length != 0U) {
                target_name = minic_c0_function_symbol_name(target);
            }
        }
        if (target_name == NULL || target_name[0] == '\\0' ||
            relocation->storage_offset < cursor ||
            relocation->storage_offset > object->storage_size ||
            pointer_width > object->storage_size - relocation->storage_offset ||
            !minic_riscv64_emit_zero_bytes(file, relocation->storage_offset - cursor) ||
            fprintf(file, "  .dword %s\\n", target_name) < 0) {
            return false;
        }
        cursor = relocation->storage_offset + pointer_width;
    }
    return cursor <= object->storage_size &&
           minic_riscv64_emit_zero_bytes(file, object->storage_size - cursor);
}

'''
text = text[:start] + replacement + text[end:]
text = text.replace('object->function_relocation_count != 0U ||\n        object->object_relocation_count != 0U',
                    'object->relocation_count != 0U', 2)
text = text.replace('object->is_zero_initialized || object->function_relocation_count != 0U ||\n        object->object_relocation_count != 0U)',
                    'object->is_zero_initialized || object->relocation_count != 0U)', 1)
text = text.replace('object->function_relocation_count != 0U || object->object_relocation_count != 0U ||',
                    'object->relocation_count != 0U ||', 1)
text = text.replace('object->function_relocation_count != 0U ||\n            object->object_relocation_count != 0U || object->initializer_count == 0U',
                    'object->relocation_count != 0U || object->initializer_count == 0U', 1)
text = text.replace('object->function_relocation_count != 0U || object->object_relocation_count != 0U ||\n            object->initializer_count != array_type->element_count * record->field_count',
                    'object->relocation_count != 0U ||\n            object->initializer_count != array_type->element_count * record->field_count', 1)
text = text.replace('if (object->function_relocation_count != 0U) {\n        if (!emit_fn_relocs(file, program, object)) {\n            return false;\n        }\n    } else if (object->object_relocation_count != 0U) {\n        if (!emit_object_relocs(file, program, object)) {',
                    'if (object->relocation_count != 0U) {\n        if (!emit_symbol_relocs(file, program, object)) {', 1)
codegen.write_text(text)

# --- Parser producers: use byte offsets, and permit mixed object/function fields. ---
global_c = root / 'src/frontend/parser_global.c'
text = global_c.read_text()
# static pointer array: compute pointer element width once after object creation.
old = '''    {
        size_t index;

        for (index = 0U; index < target_count; ++index) {
            if (targets[index] != MINIC_GLOBAL_OBJECT_INVALID &&
                !minic_c0_global_object_add_object_relocation(
                    parser->program, object_id, index, targets[index])) {
                minic_parser_error(parser, "cannot record static object relocation");
                goto done;
            }
        }
    }'''
new = '''    {
        size_t element_size;
        size_t index;

        if (!minic_target_info_sizeof_type(
                parser->target_info, parser->program, element_type, &element_size) ||
            element_size == 0U) {
            minic_parser_error(parser, "cannot size static pointer array relocation slots");
            goto done;
        }
        for (index = 0U; index < target_count; ++index) {
            if (targets[index] != MINIC_GLOBAL_OBJECT_INVALID &&
                (index > SIZE_MAX / element_size ||
                 !minic_c0_global_object_add_object_relocation(
                     parser->program, object_id, index * element_size, targets[index]))) {
                minic_parser_error(parser, "cannot record static object relocation");
                goto done;
            }
        }
    }'''
if text.count(old) != 1:
    raise SystemExit(f'static pointer array relocation producer mismatch: {text.count(old)}')
text = text.replace(old, new, 1)

# Replace record field initializer as a whole.
start = text.find('static bool parse_static_record_field_initializer(')
end = text.find('static bool static_record_array_append_value(', start)
if start < 0 or end < 0:
    raise SystemExit('static record field initializer region mismatch')
replacement = '''static bool parse_static_record_field_initializer(MinicParser *parser,
                                                  MinicGlobalObjectId object_id,
                                                  size_t field_index,
                                                  const MinicRecordField *field) {
    MinicType pointee_type;
    bool function_pointer_field;

    if (field == NULL || field->element_count != 1U) {
        minic_parser_error(parser, "unsupported static record initializer field");
        return false;
    }
    function_pointer_field = minic_type_is_pointer(field->type) &&
                             minic_type_pointee(field->type, &pointee_type) &&
                             minic_type_is_function(pointee_type);
    if (function_pointer_field && parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        MinicFunctionId function_id;
        MinicType designator_type;

        function_id = minic_parser_find_function(parser, parser->current.span);
        if (function_id == MINIC_FUNCTION_INVALID ||
            !function_designator_type(parser, function_id, &designator_type)) {
            minic_parser_error(parser, "static function initializer requires a declared function");
            return false;
        }
        if (!minic_type_assignment_compatible(field->type, designator_type)) {
            minic_parser_error(parser, "static function initializer type does not match field");
            return false;
        }
        if (!minic_parser_advance(parser) ||
            !minic_c0_global_object_add_function_relocation(
                parser->program, object_id, field->storage_offset, function_id)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                minic_parser_error(parser, "cannot record static function relocation");
            }
            return false;
        }
        return true;
    }
    if (minic_type_is_pointer(field->type) && !function_pointer_field) {
        MinicExpressionId initializer_id;
        MinicGlobalObjectId target_object_id;

        if (!minic_parser_parse_expression(parser, &initializer_id, 0U)) {
            return false;
        }
        if (!minic_c0_assignment_compatible(parser->program, field->type, initializer_id)) {
            minic_parser_error(parser, "static record pointer initializer type mismatch");
            return false;
        }
        if (minic_c0_expression_is_null_pointer_constant_v0(parser->program, initializer_id)) {
            return true;
        }
        if (!static_object_address_relocation_target(
                parser->program, initializer_id, &target_object_id)) {
            minic_parser_error(parser,
                               "static record pointer initializer requires a null or zero-addend "
                               "object address constant");
            return false;
        }
        if (!minic_c0_global_object_add_object_relocation(
                parser->program, object_id, field->storage_offset, target_object_id)) {
            minic_parser_error(parser, "cannot record static record object relocation");
            return false;
        }
        return true;
    }
    return parse_zero_initializer(parser, field->type);
}

'''
text = text[:start] + replacement + text[end:]
# scalar function pointer uses offset 0 already semantically correct; no edit required.
global_c.write_text(text)

# external pointer arrays: index -> target-aware byte offset.
func = root / 'src/frontend/parser_function.c'
text = func.read_text()
old = '''            if (has_relocation && !minic_c0_global_object_add_object_relocation(
                                      parser->program, object_id, initializer_count, target_id)) {
                minic_parser_error(parser, "cannot record external pointer array relocation");
                return false;
            }'''
new = '''            if (has_relocation) {
                size_t element_size;

                if (!minic_target_info_sizeof_type(
                        parser->target_info, parser->program, element_type, &element_size) ||
                    element_size == 0U || initializer_count > SIZE_MAX / element_size ||
                    !minic_c0_global_object_add_object_relocation(parser->program,
                                                                  object_id,
                                                                  initializer_count * element_size,
                                                                  target_id)) {
                    minic_parser_error(parser, "cannot record external pointer array relocation");
                    return false;
                }
            }'''
if text.count(old) != 1:
    raise SystemExit(f'external pointer array relocation producer mismatch: {text.count(old)}')
func.write_text(text.replace(old, new, 1))

# --- Permanent regression: existing real program becomes a mixed relocation record. ---
program = root / 'tests/programs/c0/static_function_relocations.c'
program.write_text('''static const char hook_name[] = "hooks";\n\ntypedef struct Hooks {\n    const char *name;\n    int (*first)(int value);\n    int (*second)(int value);\n    int early;\n} Hooks;\n\nstatic int add_one(int value)\n{\n    return value + 1;\n}\n\nstatic int add_two(int value)\n{\n    return value + 2;\n}\n\nstatic Hooks hooks = { hook_name, add_one, add_two, 0 };\n\nint main(void)\n{\n    if (hooks.name == 0 || hooks.name[0] != 'h') {\n        return 1;\n    }\n    if (hooks.first == 0 || hooks.first(3) != 4) {\n        return 2;\n    }\n    if (hooks.second == 0 || hooks.second(3) != 5) {\n        return 3;\n    }\n    if (hooks.early != 0) {\n        return 4;\n    }\n    return 0;\n}\n''')

# Focused compiler gate with Linux-shaped mixed record and a type negative.
fixture = root / 'tests/compiler/c0/static_mixed_symbol_relocations.c'
fixture.write_text('''static const char setup_name[] = "reset_devices";\n\nstatic int setup_fn(char *value)\n{\n    return value != 0;\n}\n\nstruct setup_entry {\n    const char *name;\n    int (*fn)(char *value);\n    int early;\n};\n\nstatic struct setup_entry entry = { setup_name, setup_fn, 0 };\n\nint read_static_mixed_symbol_relocations(void)\n{\n    return entry.name[0] + (entry.fn != 0) + entry.early;\n}\n''')
negative = root / 'tests/compiler/c0/invalid_static_record_object_relocation_type.c'
negative.write_text('''static int target;\nstatic int setup_fn(char *value) { return value != 0; }\nstruct setup_entry { char *name; int (*fn)(char *value); int early; };\nstatic struct setup_entry entry = { &target, setup_fn, 0 };\n''')
runner = root / 'tests/compiler/c0/run-static-mixed-symbol-relocations.sh'
runner.write_text('''#!/bin/sh\nset -eu\n\nroot=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)\nminic=${MINIC:-"$root/build/debug/bin/minic"}\nwork=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-mixed-symbol-relocations\n\nrm -rf "$work"\nmkdir -p "$work"\n\n"$minic" -S "$root/tests/compiler/c0/static_mixed_symbol_relocations.c" \\\n    -o "$work/static_mixed_symbol_relocations.s"\ntest -s "$work/static_mixed_symbol_relocations.s"\ngrep -F 'entry:' "$work/static_mixed_symbol_relocations.s" >/dev/null\ngrep -F '.dword setup_name' "$work/static_mixed_symbol_relocations.s" >/dev/null\ngrep -F '.dword setup_fn' "$work/static_mixed_symbol_relocations.s" >/dev/null\n\nif "$minic" -S "$root/tests/compiler/c0/invalid_static_record_object_relocation_type.c" \\\n    -o "$work/invalid-type.s" >"$work/invalid-type.stdout" 2>"$work/invalid-type.stderr"; then\n    printf '%s\\n' 'FAIL compiler/c0/static-mixed-symbol-relocations: incompatible record pointer field accepted' >&2\n    exit 1\nfi\ngrep -F 'static record pointer initializer type mismatch' "$work/invalid-type.stderr" >/dev/null\n\nprintf '%s\\n' 'PASS compiler/c0/static-mixed-symbol-relocations location=storage-byte-offset target=object+function mixed-record=accepted type=checked'\n''')
