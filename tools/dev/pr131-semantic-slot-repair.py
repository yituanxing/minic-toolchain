from pathlib import Path

root = Path('.')


def replace_once(path, old, new, label):
    p = root / path
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label} mismatch: {count}')
    p.write_text(text.replace(old, new, 1))


# The first materializer unified target kinds but encoded the relocation location
# as a target byte offset. Keep the unified target model, but make the location
# target-neutral: the parser records a semantic slot and DataLayout resolves it.
ast_h = root / 'src/frontend/ast.h'
text = ast_h.read_text()
old = '''typedef struct MinicGlobalRelocation {
    size_t storage_offset;
    MinicGlobalRelocationTargetKind target_kind;
    size_t target_id;
} MinicGlobalRelocation;
'''
new = '''typedef enum MinicGlobalRelocationLocationKind {
    MINIC_GLOBAL_RELOCATION_LOCATION_SCALAR = 0,
    MINIC_GLOBAL_RELOCATION_LOCATION_ARRAY_ELEMENT,
    MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD
} MinicGlobalRelocationLocationKind;

typedef struct MinicGlobalRelocation {
    MinicGlobalRelocationLocationKind location_kind;
    size_t location_index;
    MinicGlobalRelocationTargetKind target_kind;
    size_t target_id;
} MinicGlobalRelocation;
'''
if text.count(old) != 1:
    raise SystemExit(f'AST relocation location block mismatch: {text.count(old)}')
text = text.replace(old, new, 1)
text = text.replace('''bool minic_c0_global_object_add_function_relocation(MinicC0Program *program,
                                                    MinicGlobalObjectId global_object_id,
                                                    size_t storage_offset,
                                                    MinicFunctionId function_id);''',
                    '''bool minic_c0_global_object_add_function_relocation(
    MinicC0Program *program,
    MinicGlobalObjectId global_object_id,
    MinicGlobalRelocationLocationKind location_kind,
    size_t location_index,
    MinicFunctionId function_id);''',
                    1)
text = text.replace('''bool minic_c0_global_object_add_object_relocation(MinicC0Program *program,
                                                  MinicGlobalObjectId global_object_id,
                                                  size_t storage_offset,
                                                  MinicGlobalObjectId target_object_id);''',
                    '''bool minic_c0_global_object_add_object_relocation(
    MinicC0Program *program,
    MinicGlobalObjectId global_object_id,
    MinicGlobalRelocationLocationKind location_kind,
    size_t location_index,
    MinicGlobalObjectId target_object_id);''',
                    1)
ast_h.write_text(text)

# Entity ownership validates semantic slot shape and stores no layout result.
ast_global = root / 'src/frontend/ast_global.c'
text = ast_global.read_text()
start = text.find('static bool add_global_symbol_relocation(')
end = text.find('bool minic_c0_global_object_set_zero_initialized(', start)
if start < 0 or end < 0:
    raise SystemExit('AST global unified relocation region mismatch')
replacement = '''static bool global_relocation_location_type(
    const MinicC0Program *program,
    const MinicGlobalObject *object,
    MinicGlobalRelocationLocationKind location_kind,
    size_t location_index,
    MinicType *slot_type) {
    if (program == NULL || object == NULL || slot_type == NULL) {
        return false;
    }
    if (location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_SCALAR) {
        if (location_index != 0U || !minic_type_is_pointer(object->type)) {
            return false;
        }
        *slot_type = object->type;
        return true;
    }
    if (location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_ARRAY_ELEMENT) {
        const MinicArrayType *array_type;

        if (!minic_type_is_array(object->type)) {
            return false;
        }
        array_type = minic_c0_program_array_type(program, object->type.array_type_id);
        if (array_type == NULL || location_index >= array_type->element_count ||
            !minic_type_is_pointer(array_type->element_type)) {
            return false;
        }
        *slot_type = array_type->element_type;
        return true;
    }
    if (location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD) {
        const MinicRecord *record;
        const MinicRecordField *field;

        if (!minic_type_is_record(object->type)) {
            return false;
        }
        record = minic_c0_program_record(program, object->type.record_id);
        field = record == NULL ? NULL : minic_c0_record_field(record, location_index);
        if (field == NULL || field->element_count != 1U || field->is_bit_field ||
            field->is_flexible_array || !minic_type_is_pointer(field->type)) {
            return false;
        }
        *slot_type = field->type;
        return true;
    }
    return false;
}

static bool add_global_symbol_relocation(
    MinicC0Program *program,
    MinicGlobalObjectId global_object_id,
    MinicGlobalRelocationLocationKind location_kind,
    size_t location_index,
    MinicGlobalRelocationTargetKind target_kind,
    size_t target_id) {
    MinicGlobalObject *object;
    MinicGlobalRelocation *relocation;
    MinicType slot_pointee;
    MinicType slot_type;

    if (program == NULL || global_object_id >= program->global_object_count ||
        (target_kind != MINIC_GLOBAL_RELOCATION_OBJECT &&
         target_kind != MINIC_GLOBAL_RELOCATION_FUNCTION) ||
        (target_kind == MINIC_GLOBAL_RELOCATION_OBJECT &&
         (target_id >= program->global_object_count || global_object_id == target_id)) ||
        (target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION && target_id >= program->function_count)) {
        return false;
    }
    object = &program->global_objects[global_object_id];
    if (!global_relocation_location_type(
            program, object, location_kind, location_index, &slot_type) ||
        !minic_type_pointee(slot_type, &slot_pointee) ||
        (target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION &&
         !minic_type_is_function(slot_pointee)) ||
        (target_kind == MINIC_GLOBAL_RELOCATION_OBJECT && minic_type_is_function(slot_pointee)) ||
        object->is_tentative || object->initializer_count != 0U ||
        (object->relocation_count != 0U &&
         (object->relocations[object->relocation_count - 1U].location_kind != location_kind ||
          object->relocations[object->relocation_count - 1U].location_index >= location_index)) ||
        !grow_array((void **)&object->relocations,
                    &object->relocation_capacity,
                    object->relocation_count,
                    sizeof(*object->relocations))) {
        return false;
    }
    relocation = &object->relocations[object->relocation_count];
    relocation->location_kind = location_kind;
    relocation->location_index = location_index;
    relocation->target_kind = target_kind;
    relocation->target_id = target_id;
    object->relocation_count += 1U;
    return true;
}

bool minic_c0_global_object_add_function_relocation(
    MinicC0Program *program,
    MinicGlobalObjectId global_object_id,
    MinicGlobalRelocationLocationKind location_kind,
    size_t location_index,
    MinicFunctionId function_id) {
    return add_global_symbol_relocation(program,
                                        global_object_id,
                                        location_kind,
                                        location_index,
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

bool minic_c0_global_object_add_object_relocation(
    MinicC0Program *program,
    MinicGlobalObjectId global_object_id,
    MinicGlobalRelocationLocationKind location_kind,
    size_t location_index,
    MinicGlobalObjectId target_object_id) {
    return add_global_symbol_relocation(program,
                                        global_object_id,
                                        location_kind,
                                        location_index,
                                        MINIC_GLOBAL_RELOCATION_OBJECT,
                                        target_object_id);
}

'''
text = text[:start] + replacement + text[end:]
ast_global.write_text(text)

# Verifier checks semantic location ownership and target-kind compatibility.
verifier = root / 'src/frontend/ast_verifier.c'
text = verifier.read_text()
old = '''        {
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
new = '''        {
            size_t relocation_index;

            for (relocation_index = 0U; relocation_index < object->relocation_count;
                 ++relocation_index) {
                const MinicGlobalRelocation *relocation;
                MinicType slot_pointee;
                MinicType slot_type;

                relocation = &object->relocations[relocation_index];
                if (relocation->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_SCALAR) {
                    if (relocation->location_index != 0U || !minic_type_is_pointer(object->type)) {
                        return false;
                    }
                    slot_type = object->type;
                } else if (relocation->location_kind ==
                           MINIC_GLOBAL_RELOCATION_LOCATION_ARRAY_ELEMENT) {
                    const MinicArrayType *array_type;

                    array_type = minic_type_is_array(object->type)
                                     ? minic_c0_program_array_type(program, object->type.array_type_id)
                                     : NULL;
                    if (array_type == NULL || relocation->location_index >= array_type->element_count ||
                        !minic_type_is_pointer(array_type->element_type)) {
                        return false;
                    }
                    slot_type = array_type->element_type;
                } else if (relocation->location_kind ==
                           MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD) {
                    const MinicRecord *record;
                    const MinicRecordField *field;

                    record = minic_type_is_record(object->type)
                                 ? minic_c0_program_record(program, object->type.record_id)
                                 : NULL;
                    field = record == NULL
                                ? NULL
                                : minic_c0_record_field(record, relocation->location_index);
                    if (field == NULL || field->element_count != 1U || field->is_bit_field ||
                        field->is_flexible_array || !minic_type_is_pointer(field->type)) {
                        return false;
                    }
                    slot_type = field->type;
                } else {
                    return false;
                }
                if (!minic_type_pointee(slot_type, &slot_pointee) ||
                    (relocation_index != 0U &&
                     (object->relocations[relocation_index - 1U].location_kind !=
                          relocation->location_kind ||
                      object->relocations[relocation_index - 1U].location_index >=
                          relocation->location_index)) ||
                    (relocation->target_kind == MINIC_GLOBAL_RELOCATION_OBJECT &&
                     (relocation->target_id >= program->global_object_count ||
                      relocation->target_id == index || minic_type_is_function(slot_pointee))) ||
                    (relocation->target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION &&
                     (relocation->target_id >= program->function_count ||
                      !minic_type_is_function(slot_pointee))) ||
                    (relocation->target_kind != MINIC_GLOBAL_RELOCATION_OBJECT &&
                     relocation->target_kind != MINIC_GLOBAL_RELOCATION_FUNCTION)) {
                    return false;
                }
            }
        }
'''
if text.count(old) != 1:
    raise SystemExit(f'verifier byte-location block mismatch: {text.count(old)}')
verifier.write_text(text.replace(old, new, 1))

# DataLayout owns the mapping from semantic relocation slot to storage bytes.
data_h = root / 'src/target/data_layout.h'
text = data_h.read_text()
anchor = '''bool minic_data_layout_record_field_offset(const MinicDataLayout *layout,
                                           const MinicC0Program *program,
                                           const MinicRecord *record,
                                           size_t field_index,
                                           size_t *offset);
'''
addition = anchor + '''bool minic_data_layout_global_relocation_offset(const MinicDataLayout *layout,
                                                const MinicC0Program *program,
                                                const MinicGlobalObject *object,
                                                const MinicGlobalRelocation *relocation,
                                                size_t *offset);
'''
if text.count(anchor) != 1:
    raise SystemExit(f'data layout header anchor mismatch: {text.count(anchor)}')
data_h.write_text(text.replace(anchor, addition, 1))

data_c = root / 'src/target/data_layout.c'
text = data_c.read_text()
if 'bool minic_data_layout_global_relocation_offset(' in text:
    raise SystemExit('data layout relocation resolver already exists')
text += '''

bool minic_data_layout_global_relocation_offset(const MinicDataLayout *layout,
                                                const MinicC0Program *program,
                                                const MinicGlobalObject *object,
                                                const MinicGlobalRelocation *relocation,
                                                size_t *offset) {
    size_t object_alignment;
    size_t object_size;
    size_t resolved_offset;

    if (layout == NULL || program == NULL || object == NULL || relocation == NULL ||
        offset == NULL ||
        !minic_data_layout_type(layout, program, object->type, &object_size, &object_alignment)) {
        return false;
    }
    (void)object_alignment;
    if (relocation->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_SCALAR) {
        if (relocation->location_index != 0U || !minic_type_is_pointer(object->type)) {
            return false;
        }
        resolved_offset = 0U;
    } else if (relocation->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_ARRAY_ELEMENT) {
        const MinicArrayType *array_type;
        size_t element_alignment;
        size_t element_size;

        if (!minic_type_is_array(object->type)) {
            return false;
        }
        array_type = minic_c0_program_array_type(program, object->type.array_type_id);
        if (array_type == NULL || relocation->location_index >= array_type->element_count ||
            !minic_type_is_pointer(array_type->element_type) ||
            !minic_data_layout_type(layout,
                                    program,
                                    array_type->element_type,
                                    &element_size,
                                    &element_alignment) ||
            element_size == 0U || relocation->location_index > SIZE_MAX / element_size) {
            return false;
        }
        (void)element_alignment;
        resolved_offset = relocation->location_index * element_size;
    } else if (relocation->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD) {
        const MinicRecord *record;
        const MinicRecordField *field;

        if (!minic_type_is_record(object->type)) {
            return false;
        }
        record = minic_c0_program_record(program, object->type.record_id);
        field = record == NULL ? NULL : minic_c0_record_field(record, relocation->location_index);
        if (field == NULL || field->element_count != 1U || field->is_bit_field ||
            field->is_flexible_array || !minic_type_is_pointer(field->type) ||
            !minic_data_layout_record_field_offset(
                layout, program, record, relocation->location_index, &resolved_offset)) {
            return false;
        }
    } else {
        return false;
    }
    if (resolved_offset > object_size || layout->pointer_size > object_size - resolved_offset) {
        return false;
    }
    *offset = resolved_offset;
    return true;
}
'''
data_c.write_text(text)

# RV64 consumes resolved storage locations; it never interprets array/record indices itself.
codegen = root / 'src/target/riscv64/codegen_function.c'
text = codegen.read_text()
include_anchor = '#include "target/riscv64/codegen_internal.h"\n'
if '#include "target/data_layout.h"' not in text:
    if text.count(include_anchor) != 1:
        raise SystemExit(f'codegen include anchor mismatch: {text.count(include_anchor)}')
    text = text.replace(include_anchor,
                        include_anchor + '#include "target/data_layout.h"\n',
                        1)
start = text.find('static bool emit_symbol_relocs(')
end = text.find('static bool minic_riscv64_emit_direct_record_values(', start)
if start < 0 or end < 0:
    raise SystemExit('RV64 unified relocation emitter region mismatch')
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
        size_t storage_offset;

        relocation = &object->relocations[relocation_index];
        target_name = NULL;
        if (!minic_data_layout_global_relocation_offset(minic_default_data_layout(),
                                                        program,
                                                        object,
                                                        relocation,
                                                        &storage_offset)) {
            return false;
        }
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
        if (target_name == NULL || target_name[0] == '\\0' || storage_offset < cursor ||
            storage_offset > object->storage_size ||
            pointer_width > object->storage_size - storage_offset ||
            !minic_riscv64_emit_zero_bytes(file, storage_offset - cursor) ||
            fprintf(file, "  .dword %s\\n", target_name) < 0) {
            return false;
        }
        cursor = storage_offset + pointer_width;
    }
    return cursor <= object->storage_size &&
           minic_riscv64_emit_zero_bytes(file, object->storage_size - cursor);
}

'''
text = text[:start] + replacement + text[end:]
codegen.write_text(text)

# Parser producers record semantic locations. No parser target-size query remains.
parser = root / 'src/frontend/parser_global.c'
text = parser.read_text()
old = '''    {
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
new = '''    {
        size_t index;

        for (index = 0U; index < target_count; ++index) {
            if (targets[index] != MINIC_GLOBAL_OBJECT_INVALID &&
                !minic_c0_global_object_add_object_relocation(
                    parser->program,
                    object_id,
                    MINIC_GLOBAL_RELOCATION_LOCATION_ARRAY_ELEMENT,
                    index,
                    targets[index])) {
                minic_parser_error(parser, "cannot record static object relocation");
                goto done;
            }
        }
    }'''
if text.count(old) != 1:
    raise SystemExit(f'static pointer array byte-location producer mismatch: {text.count(old)}')
text = text.replace(old, new, 1)
old = '''static bool parse_static_record_field_initializer(MinicParser *parser,
                                                  MinicGlobalObjectId object_id,
                                                  const MinicRecordField *field) {'''
new = '''static bool parse_static_record_field_initializer(MinicParser *parser,
                                                  MinicGlobalObjectId object_id,
                                                  size_t field_index,
                                                  const MinicRecordField *field) {'''
if text.count(old) != 1:
    raise SystemExit(f'static record field signature semantic-slot repair mismatch: {text.count(old)}')
text = text.replace(old, new, 1)
old = '        if (!parse_static_record_field_initializer(parser, object_id, field)) {'
new = '        if (!parse_static_record_field_initializer(parser, object_id, field_index, field)) {'
if text.count(old) != 1:
    raise SystemExit(f'static record field call semantic-slot repair mismatch: {text.count(old)}')
text = text.replace(old, new, 1)
text = text.replace('''!minic_c0_global_object_add_function_relocation(
                parser->program, object_id, field->storage_offset, function_id)''',
                    '''!minic_c0_global_object_add_function_relocation(
                parser->program,
                object_id,
                MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD,
                field_index,
                function_id)''',
                    1)
text = text.replace('''!minic_c0_global_object_add_object_relocation(
                parser->program, object_id, field->storage_offset, target_object_id)''',
                    '''!minic_c0_global_object_add_object_relocation(
                parser->program,
                object_id,
                MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD,
                field_index,
                target_object_id)''',
                    1)
# Existing scalar producer paths from #126/#129 and function-pointer support.
text = text.replace('''!minic_c0_global_object_add_function_relocation(
                    parser->program, object_id, 0U, function_id)''',
                    '''!minic_c0_global_object_add_function_relocation(
                    parser->program,
                    object_id,
                    MINIC_GLOBAL_RELOCATION_LOCATION_SCALAR,
                    0U,
                    function_id)''')
text = text.replace('''!minic_c0_global_object_add_object_relocation(
                        parser->program, object_id, 0U, target_object_id)''',
                    '''!minic_c0_global_object_add_object_relocation(
                        parser->program,
                        object_id,
                        MINIC_GLOBAL_RELOCATION_LOCATION_SCALAR,
                        0U,
                        target_object_id)''')
parser.write_text(text)

func = root / 'src/frontend/parser_function.c'
text = func.read_text()
old = '''            if (has_relocation) {
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
new = '''            if (has_relocation &&
                !minic_c0_global_object_add_object_relocation(
                    parser->program,
                    object_id,
                    MINIC_GLOBAL_RELOCATION_LOCATION_ARRAY_ELEMENT,
                    initializer_count,
                    target_id)) {
                minic_parser_error(parser, "cannot record external pointer array relocation");
                return false;
            }'''
if text.count(old) != 1:
    raise SystemExit(f'external pointer array byte-location producer mismatch: {text.count(old)}')
text = text.replace(old, new, 1)
text = text.replace('''!minic_c0_global_object_add_object_relocation(parser->program, object_id, 0U, target_id)''',
                    '''!minic_c0_global_object_add_object_relocation(
            parser->program,
            object_id,
            MINIC_GLOBAL_RELOCATION_LOCATION_SCALAR,
            0U,
            target_id)''')
func.write_text(text)

# Migration invariants: relocation schema must remain semantic and target-neutral.
for path in (root / 'src').rglob('*'):
    if path.suffix not in {'.c', '.h'}:
        continue
    source = path.read_text()
    if 'relocation->storage_offset' in source or '.storage_offset = storage_offset' in source:
        raise SystemExit(f'byte-offset relocation state remains in {path}')

# The parser must not calculate relocation byte positions from target size.
for path in (root / 'src/frontend').glob('parser_*.c'):
    source = path.read_text()
    if 'cannot size static pointer array relocation slots' in source:
        raise SystemExit(f'parser target-sized relocation logic remains in {path}')
