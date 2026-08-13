from pathlib import Path

root = Path('.')

def replace_once(path, old, new, label):
    p = root / path
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label} mismatch: {count}')
    p.write_text(text.replace(old, new, 1))

# AST: add one semantic aggregate-scalar destination kind plus a target record-member path.
replace_once(
    'src/frontend/ast.h',
    '''typedef enum MinicGlobalRelocationLocationKind {\n    MINIC_GLOBAL_RELOCATION_LOCATION_SCALAR = 0,\n    MINIC_GLOBAL_RELOCATION_LOCATION_ARRAY_ELEMENT,\n    MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD\n} MinicGlobalRelocationLocationKind;\n''',
    '''#define MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH 8U\n\ntypedef enum MinicGlobalRelocationLocationKind {\n    MINIC_GLOBAL_RELOCATION_LOCATION_SCALAR = 0,\n    MINIC_GLOBAL_RELOCATION_LOCATION_ARRAY_ELEMENT,\n    MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD,\n    MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR\n} MinicGlobalRelocationLocationKind;\n''',
    'aggregate scalar relocation kind')
replace_once(
    'src/frontend/ast.h',
    '''typedef struct MinicGlobalRelocation {\n    MinicGlobalRelocationLocationKind location_kind;\n    size_t location_index;\n    MinicGlobalRelocationTargetKind target_kind;\n    size_t target_id;\n} MinicGlobalRelocation;\n''',
    '''typedef struct MinicGlobalRelocation {\n    MinicGlobalRelocationLocationKind location_kind;\n    size_t location_index;\n    MinicGlobalRelocationTargetKind target_kind;\n    size_t target_id;\n    size_t target_member_indices[MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH];\n    size_t target_member_depth;\n} MinicGlobalRelocation;\n''',
    'relocation target member path')
replace_once(
    'src/frontend/ast.h',
    '''bool minic_c0_global_object_add_object_relocation(MinicC0Program *program,\n                                                  MinicGlobalObjectId global_object_id,\n                                                  MinicGlobalRelocationLocationKind location_kind,\n                                                  size_t location_index,\n                                                  MinicGlobalObjectId target_object_id);\n''',
    '''bool minic_c0_global_object_add_object_relocation(MinicC0Program *program,\n                                                  MinicGlobalObjectId global_object_id,\n                                                  MinicGlobalRelocationLocationKind location_kind,\n                                                  size_t location_index,\n                                                  MinicGlobalObjectId target_object_id);\nbool minic_c0_global_object_add_object_relocation_path(\n    MinicC0Program *program,\n    MinicGlobalObjectId global_object_id,\n    MinicGlobalRelocationLocationKind location_kind,\n    size_t location_index,\n    MinicGlobalObjectId target_object_id,\n    const size_t *target_member_indices,\n    size_t target_member_depth);\n''',
    'object relocation path API')

# Entity semantic validation: flattened scalar slot type and target member path remain target-neutral.
astg = root / 'src/frontend/ast_global.c'
text = astg.read_text()
anchor = '''static bool global_relocation_location_type(const MinicC0Program *program,\n                                            const MinicGlobalObject *object,\n                                            MinicGlobalRelocationLocationKind location_kind,\n                                            size_t location_index,\n                                            MinicType *slot_type) {\n'''
helpers = r'''static bool aggregate_scalar_slot_type(const MinicC0Program *program,
                                       MinicType type,
                                       size_t *slot_index,
                                       MinicType *slot_type) {
    if (program == NULL || slot_index == NULL || slot_type == NULL) {
        return false;
    }
    if (minic_type_is_integer(type) || minic_type_is_pointer(type)) {
        if (*slot_index == 0U) {
            *slot_type = type;
            return true;
        }
        *slot_index -= 1U;
        return false;
    }
    if (minic_type_is_array(type)) {
        const MinicArrayType *array_type;
        size_t element_index;

        array_type = minic_c0_program_array_type(program, type.array_type_id);
        if (array_type == NULL || array_type->element_count == 0U) {
            return false;
        }
        for (element_index = 0U; element_index < array_type->element_count; ++element_index) {
            size_t before = *slot_index;
            if (aggregate_scalar_slot_type(program, array_type->element_type, slot_index, slot_type)) {
                return true;
            }
            if (*slot_index == before) {
                return false;
            }
        }
        return false;
    }
    if (minic_type_is_record(type)) {
        const MinicRecord *record;
        size_t field_index;
        size_t field_limit;

        record = minic_c0_program_record(program, type.record_id);
        if (record == NULL || !record->is_complete || record->field_count == 0U) {
            return false;
        }
        field_limit = record->is_union ? 1U : record->field_count;
        for (field_index = 0U; field_index < field_limit; ++field_index) {
            const MinicRecordField *field;
            size_t element_index;

            field = minic_c0_record_field(record, field_index);
            if (field == NULL || field->element_count == 0U || field->is_flexible_array) {
                return false;
            }
            for (element_index = 0U; element_index < field->element_count; ++element_index) {
                size_t before = *slot_index;
                if (aggregate_scalar_slot_type(program, field->type, slot_index, slot_type)) {
                    return true;
                }
                if (*slot_index == before) {
                    return false;
                }
            }
        }
    }
    return false;
}

static bool global_object_member_path_type(const MinicC0Program *program,
                                           const MinicGlobalObject *object,
                                           const size_t *member_indices,
                                           size_t member_depth,
                                           MinicType *result_type) {
    MinicType type;
    size_t depth;

    if (program == NULL || object == NULL || result_type == NULL ||
        member_depth > MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH ||
        (member_depth != 0U && member_indices == NULL)) {
        return false;
    }
    type = object->type;
    for (depth = 0U; depth < member_depth; ++depth) {
        const MinicRecord *record;
        const MinicRecordField *field;

        if (!minic_type_is_record(type)) {
            return false;
        }
        record = minic_c0_program_record(program, type.record_id);
        field = record == NULL ? NULL : minic_c0_record_field(record, member_indices[depth]);
        if (field == NULL || field->element_count != 1U || field->is_bit_field ||
            field->is_flexible_array) {
            return false;
        }
        type = field->type;
    }
    *result_type = type;
    return true;
}

'''
if text.count(anchor) != 1:
    raise SystemExit(f'ast global helper anchor mismatch: {text.count(anchor)}')
text = text.replace(anchor, helpers + anchor, 1)
# Extend location type resolver.
old = '''    if (location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD) {\n        const MinicRecord *record;\n        const MinicRecordField *field;\n\n        if (!minic_type_is_record(object->type)) {\n            return false;\n        }\n        record = minic_c0_program_record(program, object->type.record_id);\n        field = record == NULL ? NULL : minic_c0_record_field(record, location_index);\n        if (field == NULL || field->element_count != 1U || field->is_bit_field ||\n            field->is_flexible_array || !minic_type_is_pointer(field->type)) {\n            return false;\n        }\n        *slot_type = field->type;\n        return true;\n    }\n    return false;\n}\n'''
new = '''    if (location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD) {\n        const MinicRecord *record;\n        const MinicRecordField *field;\n\n        if (!minic_type_is_record(object->type)) {\n            return false;\n        }\n        record = minic_c0_program_record(program, object->type.record_id);\n        field = record == NULL ? NULL : minic_c0_record_field(record, location_index);\n        if (field == NULL || field->element_count != 1U || field->is_bit_field ||\n            field->is_flexible_array || !minic_type_is_pointer(field->type)) {\n            return false;\n        }\n        *slot_type = field->type;\n        return true;\n    }\n    if (location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR) {\n        size_t remaining;\n\n        remaining = location_index;\n        return aggregate_scalar_slot_type(program, object->type, &remaining, slot_type) &&\n               minic_type_is_pointer(*slot_type);\n    }\n    return false;\n}\n'''
if text.count(old) != 1:
    raise SystemExit(f'aggregate location resolver anchor mismatch: {text.count(old)}')
text = text.replace(old, new, 1)
# Let raw base bits coexist with either direct record-field overlays or recursive aggregate slots.
old = '''            if (relocation->location_kind != MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD ||\n                (relocation->location_index == object->initializer_count && bits != 0U)) {\n                return false;\n            }\n'''
new = '''            if ((relocation->location_kind != MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD &&\n                 relocation->location_kind != MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR) ||\n                (relocation->location_index == object->initializer_count && bits != 0U)) {\n                return false;\n            }\n'''
if text.count(old) != 1:
    raise SystemExit(f'initializer relocation coexistence anchor mismatch: {text.count(old)}')
text = text.replace(old, new, 1)
# Replace the internal relocation appender with path-aware storage.
start = text.index('static bool add_global_symbol_relocation(')
end = text.index('\nbool minic_c0_global_object_add_function_relocation', start)
new_fn = r'''static bool add_global_symbol_relocation(MinicC0Program *program,
                                         MinicGlobalObjectId global_object_id,
                                         MinicGlobalRelocationLocationKind location_kind,
                                         size_t location_index,
                                         MinicGlobalRelocationTargetKind target_kind,
                                         size_t target_id,
                                         const size_t *target_member_indices,
                                         size_t target_member_depth) {
    MinicGlobalObject *object;
    MinicGlobalRelocation *relocation;
    MinicType slot_pointee;
    MinicType slot_type;
    MinicType target_type;
    size_t path_index;

    if (program == NULL || global_object_id >= program->global_object_count ||
        target_member_depth > MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH ||
        (target_member_depth != 0U && target_member_indices == NULL) ||
        (target_kind != MINIC_GLOBAL_RELOCATION_OBJECT &&
         target_kind != MINIC_GLOBAL_RELOCATION_FUNCTION) ||
        (target_kind == MINIC_GLOBAL_RELOCATION_OBJECT && target_id >= program->global_object_count) ||
        (target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION &&
         (target_id >= program->function_count || target_member_depth != 0U))) {
        return false;
    }
    object = &program->global_objects[global_object_id];
    if (!global_relocation_location_type(
            program, object, location_kind, location_index, &slot_type) ||
        !minic_type_pointee(slot_type, &slot_pointee) ||
        (target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION &&
         !minic_type_is_function(slot_pointee)) ||
        (target_kind == MINIC_GLOBAL_RELOCATION_OBJECT && minic_type_is_function(slot_pointee)) ||
        (target_kind == MINIC_GLOBAL_RELOCATION_OBJECT &&
         !global_object_member_path_type(program,
                                         &program->global_objects[target_id],
                                         target_member_indices,
                                         target_member_depth,
                                         &target_type)) ||
        object->is_tentative ||
        (object->initializer_count != 0U &&
         ((location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD &&
           (!minic_type_is_record(object->type) || location_index >= object->initializer_count ||
            object->initializer_values[location_index] != 0U)) ||
          (location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR &&
           (location_index >= object->initializer_count ||
            object->initializer_values[location_index] != 0U)) ||
          (location_kind != MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD &&
           location_kind != MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR))) ||
        (object->relocation_count != 0U &&
         (object->relocations[object->relocation_count - 1U].location_kind != location_kind ||
          object->relocations[object->relocation_count - 1U].location_index >= location_index)) ||
        !grow_array((void **)&object->relocations,
                    &object->relocation_capacity,
                    object->relocation_count,
                    sizeof(*object->relocations))) {
        return false;
    }
    (void)target_type;
    relocation = &object->relocations[object->relocation_count];
    (void)memset(relocation, 0, sizeof(*relocation));
    relocation->location_kind = location_kind;
    relocation->location_index = location_index;
    relocation->target_kind = target_kind;
    relocation->target_id = target_id;
    relocation->target_member_depth = target_member_depth;
    for (path_index = 0U; path_index < target_member_depth; ++path_index) {
        relocation->target_member_indices[path_index] = target_member_indices[path_index];
    }
    object->relocation_count += 1U;
    return true;
}
'''
text = text[:start] + new_fn + text[end:]
# Existing wrappers get zero-depth path; object wrapper plus new path-aware API.
text = text.replace('''                                        MINIC_GLOBAL_RELOCATION_FUNCTION,\n                                        function_id);\n''',
                    '''                                        MINIC_GLOBAL_RELOCATION_FUNCTION,\n                                        function_id,\n                                        NULL,\n                                        0U);\n''', 1)
old = '''bool minic_c0_global_object_add_object_relocation(MinicC0Program *program,\n                                                  MinicGlobalObjectId global_object_id,\n                                                  MinicGlobalRelocationLocationKind location_kind,\n                                                  size_t location_index,\n                                                  MinicGlobalObjectId target_object_id) {\n    return add_global_symbol_relocation(program,\n                                        global_object_id,\n                                        location_kind,\n                                        location_index,\n                                        MINIC_GLOBAL_RELOCATION_OBJECT,\n                                        target_object_id);\n}\n'''
new = '''bool minic_c0_global_object_add_object_relocation_path(\n    MinicC0Program *program,\n    MinicGlobalObjectId global_object_id,\n    MinicGlobalRelocationLocationKind location_kind,\n    size_t location_index,\n    MinicGlobalObjectId target_object_id,\n    const size_t *target_member_indices,\n    size_t target_member_depth) {\n    return add_global_symbol_relocation(program,\n                                        global_object_id,\n                                        location_kind,\n                                        location_index,\n                                        MINIC_GLOBAL_RELOCATION_OBJECT,\n                                        target_object_id,\n                                        target_member_indices,\n                                        target_member_depth);\n}\n\nbool minic_c0_global_object_add_object_relocation(MinicC0Program *program,\n                                                  MinicGlobalObjectId global_object_id,\n                                                  MinicGlobalRelocationLocationKind location_kind,\n                                                  size_t location_index,\n                                                  MinicGlobalObjectId target_object_id) {\n    return minic_c0_global_object_add_object_relocation_path(program,\n                                                             global_object_id,\n                                                             location_kind,\n                                                             location_index,\n                                                             target_object_id,\n                                                             NULL,\n                                                             0U);\n}\n'''
if text.count(old) != 1:
    raise SystemExit(f'object relocation wrapper anchor mismatch: {text.count(old)}')
text = text.replace(old, new, 1)
astg.write_text(text)

# DataLayout resolves recursive destination scalar slots and semantic target member paths.
replace_once(
    'src/target/data_layout.h',
    '''bool minic_data_layout_global_relocation_offset(const MinicDataLayout *layout,\n                                                const MinicC0Program *program,\n                                                const MinicGlobalObject *object,\n                                                const MinicGlobalRelocation *relocation,\n                                                size_t *offset);\n''',
    '''bool minic_data_layout_global_relocation_offset(const MinicDataLayout *layout,\n                                                const MinicC0Program *program,\n                                                const MinicGlobalObject *object,\n                                                const MinicGlobalRelocation *relocation,\n                                                size_t *offset);\nbool minic_data_layout_global_relocation_target_addend(\n    const MinicDataLayout *layout,\n    const MinicC0Program *program,\n    const MinicGlobalRelocation *relocation,\n    size_t *addend);\n''',
    'target addend API')

dl = root / 'src/target/data_layout.c'
text = dl.read_text()
anchor = '''bool minic_data_layout_global_relocation_offset(const MinicDataLayout *layout,\n                                                const MinicC0Program *program,\n                                                const MinicGlobalObject *object,\n                                                const MinicGlobalRelocation *relocation,\n                                                size_t *offset) {\n'''
helpers = r'''static bool aggregate_scalar_slot_layout(const MinicDataLayout *layout,
                                         const MinicC0Program *program,
                                         MinicType type,
                                         size_t base_offset,
                                         size_t *slot_index,
                                         MinicType *slot_type,
                                         size_t *slot_offset) {
    size_t type_size;
    size_t type_alignment;

    if (layout == NULL || program == NULL || slot_index == NULL || slot_type == NULL ||
        slot_offset == NULL ||
        !minic_data_layout_type(layout, program, type, &type_size, &type_alignment)) {
        return false;
    }
    (void)type_alignment;
    if (minic_type_is_integer(type) || minic_type_is_pointer(type)) {
        if (*slot_index == 0U) {
            *slot_type = type;
            *slot_offset = base_offset;
            return true;
        }
        *slot_index -= 1U;
        return false;
    }
    if (minic_type_is_array(type)) {
        const MinicArrayType *array_type;
        size_t element_size;
        size_t element_alignment;
        size_t element_index;

        array_type = minic_c0_program_array_type(program, type.array_type_id);
        if (array_type == NULL || array_type->element_count == 0U ||
            !minic_data_layout_type(
                layout, program, array_type->element_type, &element_size, &element_alignment)) {
            return false;
        }
        (void)element_alignment;
        for (element_index = 0U; element_index < array_type->element_count; ++element_index) {
            size_t before = *slot_index;
            if (aggregate_scalar_slot_layout(layout,
                                             program,
                                             array_type->element_type,
                                             base_offset + element_index * element_size,
                                             slot_index,
                                             slot_type,
                                             slot_offset)) {
                return true;
            }
            if (*slot_index == before) {
                return false;
            }
        }
        return false;
    }
    if (minic_type_is_record(type)) {
        const MinicRecord *record;
        size_t field_index;
        size_t field_limit;

        record = minic_c0_program_record(program, type.record_id);
        if (record == NULL || !record->is_complete || record->field_count == 0U) {
            return false;
        }
        field_limit = record->is_union ? 1U : record->field_count;
        for (field_index = 0U; field_index < field_limit; ++field_index) {
            const MinicRecordField *field;
            size_t field_offset;
            size_t element_size;
            size_t element_alignment;
            size_t element_index;

            field = minic_c0_record_field(record, field_index);
            if (field == NULL || field->element_count == 0U || field->is_flexible_array ||
                !minic_data_layout_record_field_offset(
                    layout, program, record, field_index, &field_offset) ||
                !minic_data_layout_type(
                    layout, program, field->type, &element_size, &element_alignment)) {
                return false;
            }
            (void)element_alignment;
            for (element_index = 0U; element_index < field->element_count; ++element_index) {
                size_t before = *slot_index;
                if (aggregate_scalar_slot_layout(layout,
                                                 program,
                                                 field->type,
                                                 base_offset + field_offset +
                                                     element_index * element_size,
                                                 slot_index,
                                                 slot_type,
                                                 slot_offset)) {
                    return true;
                }
                if (*slot_index == before) {
                    return false;
                }
            }
        }
    }
    return false;
}

'''
if text.count(anchor) != 1:
    raise SystemExit(f'data-layout helper anchor mismatch: {text.count(anchor)}')
text = text.replace(anchor, helpers + anchor, 1)
old = '''    } else if (relocation->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD) {\n        const MinicRecord *record;\n        const MinicRecordField *field;\n\n        if (!minic_type_is_record(object->type)) {\n            return false;\n        }\n        record = minic_c0_program_record(program, object->type.record_id);\n        field = record == NULL ? NULL : minic_c0_record_field(record, relocation->location_index);\n        if (field == NULL || field->element_count != 1U || field->is_bit_field ||\n            field->is_flexible_array || !minic_type_is_pointer(field->type) ||\n            !minic_data_layout_record_field_offset(\n                layout, program, record, relocation->location_index, &resolved_offset)) {\n            return false;\n        }\n    } else {\n'''
new = '''    } else if (relocation->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD) {\n        const MinicRecord *record;\n        const MinicRecordField *field;\n\n        if (!minic_type_is_record(object->type)) {\n            return false;\n        }\n        record = minic_c0_program_record(program, object->type.record_id);\n        field = record == NULL ? NULL : minic_c0_record_field(record, relocation->location_index);\n        if (field == NULL || field->element_count != 1U || field->is_bit_field ||\n            field->is_flexible_array || !minic_type_is_pointer(field->type) ||\n            !minic_data_layout_record_field_offset(\n                layout, program, record, relocation->location_index, &resolved_offset)) {\n            return false;\n        }\n    } else if (relocation->location_kind ==\n               MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR) {\n        MinicType slot_type;\n        size_t remaining;\n\n        remaining = relocation->location_index;\n        if (!aggregate_scalar_slot_layout(layout,\n                                          program,\n                                          object->type,\n                                          0U,\n                                          &remaining,\n                                          &slot_type,\n                                          &resolved_offset) ||\n            !minic_type_is_pointer(slot_type)) {\n            return false;\n        }\n    } else {\n'''
if text.count(old) != 1:
    raise SystemExit(f'data-layout aggregate relocation anchor mismatch: {text.count(old)}')
text = text.replace(old, new, 1)
# Append target semantic member path resolver.
text += r'''

bool minic_data_layout_global_relocation_target_addend(
    const MinicDataLayout *layout,
    const MinicC0Program *program,
    const MinicGlobalRelocation *relocation,
    size_t *addend) {
    const MinicGlobalObject *target;
    MinicType type;
    size_t result;
    size_t depth;

    if (layout == NULL || program == NULL || relocation == NULL || addend == NULL) {
        return false;
    }
    if (relocation->target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION) {
        if (relocation->target_member_depth != 0U) {
            return false;
        }
        *addend = 0U;
        return true;
    }
    if (relocation->target_kind != MINIC_GLOBAL_RELOCATION_OBJECT ||
        relocation->target_id >= program->global_object_count ||
        relocation->target_member_depth > MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH) {
        return false;
    }
    target = &program->global_objects[relocation->target_id];
    type = target->type;
    result = 0U;
    for (depth = 0U; depth < relocation->target_member_depth; ++depth) {
        const MinicRecord *record;
        const MinicRecordField *field;
        size_t field_offset;

        if (!minic_type_is_record(type)) {
            return false;
        }
        record = minic_c0_program_record(program, type.record_id);
        field = record == NULL
                    ? NULL
                    : minic_c0_record_field(record, relocation->target_member_indices[depth]);
        if (field == NULL || field->element_count != 1U || field->is_bit_field ||
            field->is_flexible_array ||
            !minic_data_layout_record_field_offset(layout,
                                                   program,
                                                   record,
                                                   relocation->target_member_indices[depth],
                                                   &field_offset) ||
            result > SIZE_MAX - field_offset) {
            return false;
        }
        result += field_offset;
        type = field->type;
    }
    *addend = result;
    return true;
}
'''
dl.write_text(text)

# Parser: resolve &global.member.member as semantic target path and let nested pointer
# leaves choose between raw bits and symbolic overlays.
g = root / 'src/frontend/parser_global.c'
text = g.read_text()
anchor = '''static bool static_pointer_integer_constant_bits(const MinicParser *parser,\n'''
helper = r'''typedef struct MinicStaticObjectRelocationTarget {
    MinicGlobalObjectId object_id;
    size_t member_indices[MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH];
    size_t member_depth;
} MinicStaticObjectRelocationTarget;

typedef struct MinicStaticPointerInitializer {
    bool has_relocation;
    uint64_t bits;
    MinicStaticObjectRelocationTarget relocation_target;
} MinicStaticPointerInitializer;

static bool static_object_address_relocation_path(const MinicC0Program *program,
                                                  MinicExpressionId expression_id,
                                                  MinicStaticObjectRelocationTarget *target) {
    const MinicExpression *expression;
    const MinicExpression *addressed;
    size_t reverse_path[MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH];
    size_t depth;
    size_t index;

    if (program == NULL || target == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(program, expression_id);
    while (expression != NULL && (expression->kind == MINIC_EXPRESSION_CAST ||
                                  expression->kind == MINIC_EXPRESSION_BITCAST ||
                                  expression->kind == MINIC_EXPRESSION_CONVERSION)) {
        expression_id = expression->value.unary.operand;
        expression = minic_c0_program_expression(program, expression_id);
    }
    if (expression == NULL || expression->kind != MINIC_EXPRESSION_ADDRESS_OF) {
        return false;
    }
    addressed = minic_c0_program_expression(program, expression->value.unary.operand);
    depth = 0U;
    while (addressed != NULL && addressed->kind == MINIC_EXPRESSION_MEMBER) {
        if (depth == MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH) {
            return false;
        }
        reverse_path[depth] = addressed->value.member.field_index;
        depth += 1U;
        addressed = minic_c0_program_expression(program, addressed->value.member.base);
    }
    if (addressed == NULL || addressed->kind != MINIC_EXPRESSION_GLOBAL_OBJECT) {
        return false;
    }
    target->object_id = addressed->value.global_object_id;
    if (target->object_id >= program->global_object_count) {
        return false;
    }
    target->member_depth = depth;
    for (index = 0U; index < depth; ++index) {
        target->member_indices[index] = reverse_path[depth - index - 1U];
    }
    return true;
}

'''
if text.count(anchor) != 1:
    raise SystemExit(f'parser relocation path helper anchor mismatch: {text.count(anchor)}')
text = text.replace(anchor, helper + anchor, 1)
# Add shared nested pointer parser after existing pointer-bits parser.
anchor = '''static bool parse_static_scalar(MinicParser *parser, MinicType type, MinicSourceSpan name_span) {\n'''
helper = r'''static bool parse_static_pointer_initializer(MinicParser *parser,
                                             MinicType target_type,
                                             MinicStaticPointerInitializer *initializer) {
    MinicExpressionId expression_id;

    if (parser == NULL || initializer == NULL || !minic_type_is_pointer(target_type) ||
        !minic_parser_parse_expression(parser, &expression_id, 0U)) {
        return false;
    }
    (void)memset(initializer, 0, sizeof(*initializer));
    initializer->relocation_target.object_id = MINIC_GLOBAL_OBJECT_INVALID;
    if (!minic_c0_assignment_compatible(parser->program, target_type, expression_id)) {
        minic_parser_error(parser, "static pointer initializer type mismatch");
        return false;
    }
    if (minic_c0_expression_is_null_pointer_constant_v0(parser->program, expression_id)) {
        return true;
    }
    if (static_object_address_relocation_path(
            parser->program, expression_id, &initializer->relocation_target)) {
        initializer->has_relocation = true;
        return true;
    }
    if (static_pointer_integer_constant_bits(parser, expression_id, &initializer->bits)) {
        return true;
    }
    minic_parser_error(parser,
                       "static pointer initializer requires null, symbolic address, or explicit "
                       "integer-to-pointer constant cast");
    return false;
}

'''
if text.count(anchor) != 1:
    raise SystemExit(f'nested pointer parser anchor mismatch: {text.count(anchor)}')
text = text.replace(anchor, helper + anchor, 1)
# Replace nested pointer leaf branch.
old = '''    } else if (minic_type_is_pointer(type)) {\n        uint64_t pointer_bits;\n\n        if (!parse_static_pointer_constant_bits(parser, type, &pointer_bits) ||\n            !minic_c0_global_object_add_initializer_bits(parser->program, object_id, pointer_bits)) {\n            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {\n                minic_parser_error(parser, "cannot record static pointer constant bits");\n            }\n            return false;\n        }\n'''
new = '''    } else if (minic_type_is_pointer(type)) {\n        MinicStaticPointerInitializer initializer;\n        size_t slot_index;\n\n        slot_index = parser->program->global_objects[object_id].initializer_count;\n        if (!parse_static_pointer_initializer(parser, type, &initializer)) {\n            return false;\n        }\n        if (initializer.has_relocation) {\n            if (!minic_c0_global_object_add_initializer_bits(parser->program, object_id, 0U) ||\n                !minic_c0_global_object_add_object_relocation_path(\n                    parser->program,\n                    object_id,\n                    MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR,\n                    slot_index,\n                    initializer.relocation_target.object_id,\n                    initializer.relocation_target.member_indices,\n                    initializer.relocation_target.member_depth)) {\n                minic_parser_error(parser, "cannot record nested static object relocation");\n                return false;\n            }\n        } else if (!minic_c0_global_object_add_initializer_bits(\n                       parser->program, object_id, initializer.bits)) {\n            minic_parser_error(parser, "cannot record static pointer constant bits");\n            return false;\n        }\n'''
if text.count(old) != 1:
    raise SystemExit(f'nested pointer relocation branch mismatch: {text.count(old)}')
text = text.replace(old, new, 1)
g.write_text(text)

# Verifier: validate aggregate scalar slots, allow self-object targets, and validate target paths.
v = root / 'src/frontend/ast_verifier.c'
text = v.read_text()
# Relax mixed-record-only shape gate for recursive aggregate scalar overlays.
old = '''        if (object->relocation_count != 0U && !object->is_zero_initialized) {\n            const MinicRecord *record;\n            size_t relocation_index;\n\n            record = minic_c0_program_record(program, object->type.record_id);\n            if (record == NULL || !record->is_complete || record->is_union ||\n                object->initializer_count != record->field_count) {\n                return false;\n            }\n            for (relocation_index = 0U; relocation_index < object->relocation_count;\n                 ++relocation_index) {\n                const MinicGlobalRelocation *relocation;\n\n                relocation = &object->relocations[relocation_index];\n                if (relocation->location_kind != MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD ||\n                    relocation->location_index >= object->initializer_count ||\n                    object->initializer_values[relocation->location_index] != 0) {\n                    return false;\n                }\n            }\n        }\n'''
new = '''        if (object->relocation_count != 0U && !object->is_zero_initialized) {\n            size_t relocation_index;\n\n            for (relocation_index = 0U; relocation_index < object->relocation_count;\n                 ++relocation_index) {\n                const MinicGlobalRelocation *relocation;\n\n                relocation = &object->relocations[relocation_index];\n                if ((relocation->location_kind != MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD &&\n                     relocation->location_kind !=\n                         MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR) ||\n                    relocation->location_index >= object->initializer_count ||\n                    object->initializer_values[relocation->location_index] != 0U) {\n                    return false;\n                }\n                if (relocation->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD) {\n                    const MinicRecord *record;\n\n                    record = minic_type_is_record(object->type)\n                                 ? minic_c0_program_record(program, object->type.record_id)\n                                 : NULL;\n                    if (record == NULL || !record->is_complete || record->is_union ||\n                        object->initializer_count != record->field_count) {\n                        return false;\n                    }\n                }\n            }\n        }\n'''
if text.count(old) != 1:
    raise SystemExit(f'verifier mixed overlay gate mismatch: {text.count(old)}')
text = text.replace(old, new, 1)
# Rather than duplicate target layout logic in verifier, accept persisted aggregate scalar kind here;
# DataLayout/target verification below will validate its resolved pointer slot. Add kind shape.
old = '''                } else if (relocation->location_kind ==\n                           MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD) {\n                    const MinicRecord *record;\n                    const MinicRecordField *field;\n\n                    record = minic_type_is_record(object->type)\n                                 ? minic_c0_program_record(program, object->type.record_id)\n                                 : NULL;\n                    field = record == NULL\n                                ? NULL\n                                : minic_c0_record_field(record, relocation->location_index);\n                    if (field == NULL || field->element_count != 1U || field->is_bit_field ||\n                        field->is_flexible_array || !minic_type_is_pointer(field->type)) {\n                        return false;\n                    }\n                    slot_type = field->type;\n                } else {\n                    return false;\n                }\n'''
new = '''                } else if (relocation->location_kind ==\n                           MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD) {\n                    const MinicRecord *record;\n                    const MinicRecordField *field;\n\n                    record = minic_type_is_record(object->type)\n                                 ? minic_c0_program_record(program, object->type.record_id)\n                                 : NULL;\n                    field = record == NULL\n                                ? NULL\n                                : minic_c0_record_field(record, relocation->location_index);\n                    if (field == NULL || field->element_count != 1U || field->is_bit_field ||\n                        field->is_flexible_array || !minic_type_is_pointer(field->type)) {\n                        return false;\n                    }\n                    slot_type = field->type;\n                } else if (relocation->location_kind ==\n                           MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR) {\n                    size_t resolved_offset;\n\n                    if (!minic_data_layout_global_relocation_offset(\n                            minic_target_info_data_layout(target),\n                            program,\n                            object,\n                            relocation,\n                            &resolved_offset)) {\n                        return false;\n                    }\n                    (void)resolved_offset;\n                    slot_type = minic_type_pointer_to(minic_type_void(), &slot_type)\n                                    ? slot_type\n                                    : minic_type_void();\n                } else {\n                    return false;\n                }\n'''
if text.count(old) != 1:
    raise SystemExit(f'verifier aggregate slot branch mismatch: {text.count(old)}')
text = text.replace(old, new, 1)
# Remove self-target rejection and validate target member addend/path via DataLayout.
text = text.replace('''                      relocation->target_id == index || minic_type_is_function(slot_pointee))) ||\n''',
                    '''                      minic_type_is_function(slot_pointee))) ||\n''', 1)
old = '''                    (relocation->target_kind != MINIC_GLOBAL_RELOCATION_OBJECT &&\n                     relocation->target_kind != MINIC_GLOBAL_RELOCATION_FUNCTION)) {\n                    return false;\n                }\n'''
new = '''                    (relocation->target_kind != MINIC_GLOBAL_RELOCATION_OBJECT &&\n                     relocation->target_kind != MINIC_GLOBAL_RELOCATION_FUNCTION)) {\n                    return false;\n                }\n                {\n                    size_t target_addend;\n\n                    if (!minic_data_layout_global_relocation_target_addend(\n                            minic_target_info_data_layout(target),\n                            program,\n                            relocation,\n                            &target_addend)) {\n                        return false;\n                    }\n                    (void)target_addend;\n                }\n'''
if text.count(old) != 1:
    raise SystemExit(f'verifier target addend insertion mismatch: {text.count(old)}')
text = text.replace(old, new, 1)
v.write_text(text)

# RV64: common symbolic value emitter with target addend; recursive aggregate emitter overlays by scalar slot.
cg = root / 'src/target/riscv64/codegen_function.c'
text = cg.read_text()
anchor = '''static bool\nemit_symbol_relocs(FILE *file, const MinicC0Program *program, const MinicGlobalObject *object) {\n'''
helper = r'''static bool minic_riscv64_emit_symbol_value(FILE *file,
                                              const MinicC0Program *program,
                                              const MinicGlobalRelocation *relocation,
                                              size_t width) {
    const char *directive;
    const char *target_name;
    size_t target_addend;

    directive = minic_riscv64_integer_data_directive(width);
    target_name = minic_riscv64_global_relocation_target_name(program, relocation);
    if (file == NULL || directive == NULL || target_name == NULL || target_name[0] == '\0' ||
        !minic_data_layout_global_relocation_target_addend(
            minic_default_data_layout(), program, relocation, &target_addend)) {
        return false;
    }
    if (target_addend == 0U) {
        return fprintf(file, "  %s %s\n", directive, target_name) >= 0;
    }
    return fprintf(file, "  %s %s+%zu\n", directive, target_name, target_addend) >= 0;
}

'''
if text.count(anchor) != 1:
    raise SystemExit(f'codegen symbol helper anchor mismatch: {text.count(anchor)}')
text = text.replace(anchor, helper + anchor, 1)
# Zero-base relocation emitter uses common symbol value.
old = '''        const char *target_name;\n        size_t storage_offset;\n\n        relocation = &object->relocations[relocation_index];\n        target_name = minic_riscv64_global_relocation_target_name(program, relocation);\n        if (!minic_data_layout_global_relocation_offset(\n                minic_default_data_layout(), program, object, relocation, &storage_offset)) {\n            return false;\n        }\n        if (target_name == NULL || target_name[0] == '\\0' || storage_offset < cursor ||\n            storage_offset > object->storage_size ||\n            pointer_width > object->storage_size - storage_offset ||\n            !minic_riscv64_emit_zero_bytes(file, storage_offset - cursor) ||\n            fprintf(file, "  .dword %s\\n", target_name) < 0) {\n            return false;\n        }\n'''
new = '''        size_t storage_offset;\n\n        relocation = &object->relocations[relocation_index];\n        if (!minic_data_layout_global_relocation_offset(\n                minic_default_data_layout(), program, object, relocation, &storage_offset)) {\n            return false;\n        }\n        if (storage_offset < cursor || storage_offset > object->storage_size ||\n            pointer_width > object->storage_size - storage_offset ||\n            !minic_riscv64_emit_zero_bytes(file, storage_offset - cursor) ||\n            !minic_riscv64_emit_symbol_value(file, program, relocation, pointer_width)) {\n            return false;\n        }\n'''
if text.count(old) != 1:
    raise SystemExit(f'zero-base symbol emitter mismatch: {text.count(old)}')
text = text.replace(old, new, 1)
# Direct record overlay uses common symbol value.
old = '''            const char *directive;\n            const char *target_name;\n\n            directive = minic_riscv64_integer_data_directive(field_size);\n            target_name = minic_riscv64_global_relocation_target_name(program, relocation);\n            if (!minic_type_is_pointer(field->type) || value != 0 || directive == NULL ||\n                target_name == NULL || target_name[0] == '\\0' ||\n                fprintf(file, "  %s %s\\n", directive, target_name) < 0) {\n                return false;\n            }\n'''
new = '''            if (!minic_type_is_pointer(field->type) || value != 0U ||\n                !minic_riscv64_emit_symbol_value(file, program, relocation, field_size)) {\n                return false;\n            }\n'''
if text.count(old) != 1:
    raise SystemExit(f'direct symbol emitter mismatch: {text.count(old)}')
text = text.replace(old, new, 1)
# Replace recursive constant emitter entirely with relocation-index-aware version.
start = text.index('static bool minic_riscv64_emit_constant_value(')
end = text.index('\nstatic bool minic_riscv64_emit_record_values', start)
new_fn = r'''static bool minic_riscv64_emit_constant_value(FILE *file,
                                              const MinicC0Program *program,
                                              const MinicGlobalObject *object,
                                              MinicType type,
                                              size_t *initializer_index,
                                              size_t *relocation_index,
                                              size_t *emitted_size) {
    size_t type_size;
    size_t type_alignment;

    if (file == NULL || program == NULL || object == NULL || initializer_index == NULL ||
        relocation_index == NULL || emitted_size == NULL ||
        !minic_riscv64_type_layout(program, type, &type_size, &type_alignment)) {
        return false;
    }
    (void)type_alignment;
    if (minic_type_is_integer(type) || minic_type_is_pointer(type)) {
        const MinicGlobalRelocation *relocation;
        uint64_t bits;
        size_t slot_index;

        if (*initializer_index >= object->initializer_count) {
            return false;
        }
        slot_index = *initializer_index;
        bits = object->initializer_values[slot_index];
        *initializer_index += 1U;
        relocation = *relocation_index < object->relocation_count
                         ? &object->relocations[*relocation_index]
                         : NULL;
        if (relocation != NULL &&
            relocation->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR &&
            relocation->location_index == slot_index) {
            if (!minic_type_is_pointer(type) || bits != 0U ||
                !minic_riscv64_emit_symbol_value(file, program, relocation, type_size)) {
                return false;
            }
            *relocation_index += 1U;
        } else if (!minic_riscv64_emit_integer_bits(file, type_size, bits)) {
            return false;
        }
        *emitted_size = type_size;
        return true;
    }
    if (minic_type_is_array(type)) {
        const MinicArrayType *array_type;
        size_t cursor;
        size_t element_index;

        array_type = minic_c0_program_array_type(program, type.array_type_id);
        if (array_type == NULL || array_type->element_count == 0U) {
            return false;
        }
        cursor = 0U;
        for (element_index = 0U; element_index < array_type->element_count; ++element_index) {
            size_t element_emitted;

            if (!minic_riscv64_emit_constant_value(file,
                                                   program,
                                                   object,
                                                   array_type->element_type,
                                                   initializer_index,
                                                   relocation_index,
                                                   &element_emitted) ||
                cursor > type_size - element_emitted) {
                return false;
            }
            cursor += element_emitted;
        }
        if (cursor > type_size || !minic_riscv64_emit_zero_bytes(file, type_size - cursor)) {
            return false;
        }
        *emitted_size = type_size;
        return true;
    }
    if (minic_type_is_record(type)) {
        const MinicRecord *record;
        size_t cursor;
        size_t field_index;
        size_t field_limit;

        record = minic_c0_program_record(program, type.record_id);
        if (record == NULL || !record->is_complete || record->field_count == 0U) {
            return false;
        }
        cursor = 0U;
        field_limit = record->is_union ? 1U : record->field_count;
        for (field_index = 0U; field_index < field_limit; ++field_index) {
            const MinicRecordField *field;
            size_t element_index;
            size_t field_offset;

            field = minic_c0_record_field(record, field_index);
            if (field == NULL || field->element_count == 0U || field->is_flexible_array) {
                return false;
            }
            field_offset = record->is_union ? 0U : field->storage_offset;
            if (field_offset < cursor || field_offset > type_size ||
                !minic_riscv64_emit_zero_bytes(file, field_offset - cursor)) {
                return false;
            }
            cursor = field_offset;
            for (element_index = 0U; element_index < field->element_count; ++element_index) {
                size_t element_emitted;

                if (!minic_riscv64_emit_constant_value(file,
                                                       program,
                                                       object,
                                                       field->type,
                                                       initializer_index,
                                                       relocation_index,
                                                       &element_emitted) ||
                    cursor > type_size - element_emitted) {
                    return false;
                }
                cursor += element_emitted;
            }
            if (record->is_union) {
                break;
            }
        }
        if (cursor > type_size || !minic_riscv64_emit_zero_bytes(file, type_size - cursor)) {
            return false;
        }
        *emitted_size = type_size;
        return true;
    }
    return false;
}
'''
text = text[:start] + new_fn + text[end:]
# Record-values caller tracks recursive relocation consumption.
old = '''    size_t emitted_size;\n    size_t initializer_index;\n'''
new = '''    size_t emitted_size;\n    size_t initializer_index;\n    size_t relocation_index;\n'''
if text.count(old) < 1:
    raise SystemExit('record values locals anchor missing')
text = text.replace(old, new, 1)
old = '''    initializer_index = 0U;\n    emitted_size = 0U;\n    return minic_riscv64_emit_constant_value(\n               file, program, object, object->type, &initializer_index, &emitted_size) &&\n           initializer_index == object->initializer_count && emitted_size == object->storage_size;\n'''
new = '''    initializer_index = 0U;\n    relocation_index = 0U;\n    emitted_size = 0U;\n    return minic_riscv64_emit_constant_value(file,\n                                             program,\n                                             object,\n                                             object->type,\n                                             &initializer_index,\n                                             &relocation_index,\n                                             &emitted_size) &&\n           initializer_index == object->initializer_count &&\n           relocation_index == object->relocation_count && emitted_size == object->storage_size;\n'''
if text.count(old) != 1:
    raise SystemExit(f'record values call anchor mismatch: {text.count(old)}')
text = text.replace(old, new, 1)
# Clean remaining raw-bit warnings in record arrays and scalar globals.
old = '''            int value;\n'''
# The first remaining int value after direct emitter belongs record-array emitter.
pos = text.find(old, text.find('static bool minic_riscv64_emit_record_array_values'))
if pos != -1:
    text = text[:pos] + '            uint64_t value;\n' + text[pos + len(old):]
old_block = '''            directive = minic_riscv64_integer_data_directive(field_size);\n            if (directive == NULL) {\n                return false;\n            }\n            if (field_size == 1U) {\n                unsigned int byte_value;\n\n                byte_value = (unsigned int)value & 0xffU;\n                if (fprintf(file, "  %s %u\\n", directive, byte_value) < 0) {\n                    return false;\n                }\n            } else if (fprintf(file, "  %s %d\\n", directive, value) < 0) {\n                return false;\n            }\n'''
new_block = '''            directive = minic_riscv64_integer_data_directive(field_size);\n            if (directive == NULL || !minic_riscv64_emit_integer_bits(file, field_size, value)) {\n                return false;\n            }\n'''
if text.count(old_block) != 1:
    raise SystemExit(f'record array raw bits emitter mismatch: {text.count(old_block)}')
text = text.replace(old_block, new_block, 1)
old = '''        for (initializer_index = 0U; initializer_index < object->initializer_count;\n             ++initializer_index) {\n            if (scalar_width == 1U) {\n                unsigned int value;\n\n                value = (unsigned int)object->initializer_values[initializer_index] & 0xffU;\n                if (fprintf(file, "  %s %u\\n", directive, value) < 0) {\n                    return false;\n                }\n            } else if (fprintf(file,\n                               "  %s %d\\n",\n                               directive,\n                               object->initializer_values[initializer_index]) < 0) {\n                return false;\n            }\n        }\n'''
new = '''        for (initializer_index = 0U; initializer_index < object->initializer_count;\n             ++initializer_index) {\n            if (!minic_riscv64_emit_integer_bits(\n                    file, scalar_width, object->initializer_values[initializer_index])) {\n                return false;\n            }\n        }\n'''
if text.count(old) != 1:
    raise SystemExit(f'scalar global raw bits emitter mismatch: {text.count(old)}')
text = text.replace(old, new, 1)
cg.write_text(text)

# Focused Linux-shaped self-address nested relocation.
test = root / 'tests/compiler/c0/static_record_compound_literal.c'
test.write_text(r'''typedef struct Link {
    struct Link *next;
    struct Link *prev;
} Link;

typedef struct Inner {
    int first;
    unsigned int magic;
    int second;
    void *owner;
    Link link;
} Inner;

typedef struct Outer {
    int tag;
    Inner inner;
} Outer;

static Outer value = {
    3,
    (Inner) {
        .magic = 0xdead4ead,
        .second = 7,
        .owner = (void *)-1L,
        .link = { &value.inner.link, &value.inner.link },
    },
};

int main(void)
{
    return value.tag == 3 && value.inner.first == 0 && value.inner.magic == 0xdead4eadU &&
                   value.inner.second == 7 && value.inner.owner == (void *)-1L &&
                   value.inner.link.next == &value.inner.link &&
                   value.inner.link.prev == &value.inner.link
               ? 0
               : 1;
}
''')
script = root / 'tests/compiler/c0/run-static-aggregate-family-discovery.sh'
text = script.read_text()
needle = "grep -F '.dword -1' \"$build_dir/static_record_compound_literal.s\" >/dev/null\n"
if text.count(needle) != 1:
    raise SystemExit(f'focused nested relocation anchor mismatch: {text.count(needle)}')
text = text.replace(
    needle,
    needle + "test \"$(grep -c '  .dword value+' \"$build_dir/static_record_compound_literal.s\")\" -eq 2\n",
    1)
script.write_text(text)
