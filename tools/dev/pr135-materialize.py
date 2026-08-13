from pathlib import Path

root = Path('.')


def replace_once(path, old, new, label):
    p = root / path
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label} mismatch: {count}')
    p.write_text(text.replace(old, new, 1))


# --- GlobalObject entity contract: a direct record may carry a constant base
# initializer and RECORD_FIELD symbolic relocations as zero-slot overlays. ---
ast_global = root / 'src/frontend/ast_global.c'
text = ast_global.read_text()
anchor = '''bool minic_c0_global_object_add_initializer(MinicC0Program *program,
                                            MinicGlobalObjectId global_object_id,
                                            int value) {
    MinicGlobalObject *object;

    if (program == NULL || global_object_id >= program->global_object_count) {
        return false;
    }
    object = &program->global_objects[global_object_id];
    if (object->is_tentative || object->is_zero_initialized || object->relocation_count != 0U) {
        return false;
    }
'''
replacement = '''bool minic_c0_global_object_add_initializer(MinicC0Program *program,
                                            MinicGlobalObjectId global_object_id,
                                            int value) {
    MinicGlobalObject *object;

    if (program == NULL || global_object_id >= program->global_object_count) {
        return false;
    }
    object = &program->global_objects[global_object_id];
    if (object->is_tentative || object->is_zero_initialized) {
        return false;
    }
    if (object->relocation_count != 0U) {
        size_t relocation_index;

        if (!minic_type_is_record(object->type)) {
            return false;
        }
        for (relocation_index = 0U; relocation_index < object->relocation_count;
             ++relocation_index) {
            const MinicGlobalRelocation *relocation;

            relocation = &object->relocations[relocation_index];
            if (relocation->location_kind != MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD ||
                (relocation->location_index == object->initializer_count && value != 0)) {
                return false;
            }
        }
    }
'''
if text.count(anchor) != 1:
    raise SystemExit(f'add initializer contract mismatch: {text.count(anchor)}')
text = text.replace(anchor, replacement, 1)

anchor = '''        (target_kind == MINIC_GLOBAL_RELOCATION_OBJECT && minic_type_is_function(slot_pointee)) ||
        object->is_tentative || object->initializer_count != 0U ||
        (object->relocation_count != 0U &&
'''
replacement = '''        (target_kind == MINIC_GLOBAL_RELOCATION_OBJECT && minic_type_is_function(slot_pointee)) ||
        object->is_tentative ||
        (object->initializer_count != 0U &&
         (location_kind != MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD ||
          !minic_type_is_record(object->type) || location_index >= object->initializer_count ||
          object->initializer_values[location_index] != 0)) ||
        (object->relocation_count != 0U &&
'''
if text.count(anchor) != 1:
    raise SystemExit(f'add relocation mixed-base contract mismatch: {text.count(anchor)}')
text = text.replace(anchor, replacement, 1)
ast_global.write_text(text)

# --- Parser: lazily materialize direct-record base values only when a nonzero
# scalar requires them; relocation-only all-zero records keep their old compact form. ---
parser = root / 'src/frontend/parser_global.c'
text = parser.read_text()
insert_anchor = '''static bool parse_static_record_field_initializer(MinicParser *parser,
                                                  MinicGlobalObjectId object_id,
                                                  size_t field_index,
                                                  const MinicRecordField *field) {
'''
helper = '''static bool ensure_static_record_base_value(MinicParser *parser,
                                            MinicGlobalObjectId object_id,
                                            size_t field_index,
                                            int value) {
    MinicGlobalObject *object;

    if (parser == NULL || object_id >= parser->program->global_object_count) {
        return false;
    }
    object = &parser->program->global_objects[object_id];
    while (object->initializer_count < field_index) {
        if (!minic_c0_global_object_add_initializer(parser->program, object_id, 0)) {
            return false;
        }
        object = &parser->program->global_objects[object_id];
    }
    return object->initializer_count == field_index &&
           minic_c0_global_object_add_initializer(parser->program, object_id, value);
}

'''
if text.count(insert_anchor) != 1:
    raise SystemExit(f'record field parser anchor mismatch: {text.count(insert_anchor)}')
text = text.replace(insert_anchor, helper + insert_anchor, 1)

old = '''        if (!minic_parser_advance(parser) || !minic_c0_global_object_add_function_relocation(
                                                 parser->program,
                                                 object_id,
                                                 MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD,
                                                 field_index,
                                                 function_id)) {
'''
new = '''        if (!minic_parser_advance(parser) ||
            (parser->program->global_objects[object_id].initializer_count != 0U &&
             !ensure_static_record_base_value(parser, object_id, field_index, 0)) ||
            !minic_c0_global_object_add_function_relocation(
                parser->program,
                object_id,
                MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD,
                field_index,
                function_id)) {
'''
if text.count(old) != 1:
    raise SystemExit(f'function field relocation producer mismatch: {text.count(old)}')
text = text.replace(old, new, 1)

old = '''        if (minic_c0_expression_is_null_pointer_constant_v0(parser->program, initializer_id)) {
            return true;
        }
'''
new = '''        if (minic_c0_expression_is_null_pointer_constant_v0(parser->program, initializer_id)) {
            return parser->program->global_objects[object_id].initializer_count == 0U ||
                   ensure_static_record_base_value(parser, object_id, field_index, 0);
        }
'''
if text.count(old) != 1:
    raise SystemExit(f'record null pointer base producer mismatch: {text.count(old)}')
text = text.replace(old, new, 1)

old = '''        if (!minic_c0_global_object_add_object_relocation(
                parser->program,
                object_id,
                MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD,
                field_index,
                target_object_id)) {
'''
new = '''        if ((parser->program->global_objects[object_id].initializer_count != 0U &&
             !ensure_static_record_base_value(parser, object_id, field_index, 0)) ||
            !minic_c0_global_object_add_object_relocation(
                parser->program,
                object_id,
                MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD,
                field_index,
                target_object_id)) {
'''
if text.count(old) != 1:
    raise SystemExit(f'object field relocation producer mismatch: {text.count(old)}')
text = text.replace(old, new, 1)

old = '''    return parse_zero_initializer(parser, field->type);
}
'''
new = '''    if (minic_type_is_integer(field->type)) {
        int value;

        if (!minic_parser_parse_integer_initializer_value(parser, field->type, &value)) {
            return false;
        }
        if (value == 0 && parser->program->global_objects[object_id].initializer_count == 0U) {
            return true;
        }
        return ensure_static_record_base_value(parser, object_id, field_index, value);
    }
    if (!parse_zero_initializer(parser, field->type)) {
        return false;
    }
    return parser->program->global_objects[object_id].initializer_count == 0U ||
           ensure_static_record_base_value(parser, object_id, field_index, 0);
}
'''
if text.count(old) != 1:
    raise SystemExit(f'record scalar fallback mismatch: {text.count(old)}')
text = text.replace(old, new, 1)

old = '''    if (!minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}' after record initializer") ||
        !minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
            minic_parser_error(parser, "cannot record static record initializer");
        }
        return false;
    }
'''
new = '''    if (!minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}' after record initializer")) {
        return false;
    }
    if (parser->program->global_objects[object_id].initializer_count != 0U) {
        while (parser->program->global_objects[object_id].initializer_count < record->field_count) {
            size_t next_field;

            next_field = parser->program->global_objects[object_id].initializer_count;
            if (!ensure_static_record_base_value(parser, object_id, next_field, 0)) {
                minic_parser_error(parser, "cannot complete static record base initializer");
                return false;
            }
        }
        if (parser->program->global_objects[object_id].initializer_count != record->field_count) {
            minic_parser_error(parser, "invalid mixed static record initializer shape");
            return false;
        }
    } else if (!minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {
        minic_parser_error(parser, "cannot record static record initializer");
        return false;
    }
'''
if text.count(old) != 1:
    raise SystemExit(f'record finalization mismatch: {text.count(old)}')
text = text.replace(old, new, 1)
parser.write_text(text)

# --- Verifier: relocations have exactly two legal base modes:
# all-zero implicit storage, or a complete direct-record base value vector. ---
verifier = root / 'src/frontend/ast_verifier.c'
text = verifier.read_text()
old = '''            (object->is_zero_initialized && object->initializer_count != 0U) ||
            (object->relocation_count != 0U &&
             (!object->is_zero_initialized || object->initializer_count != 0U)) ||
            !storage_is_valid(object->initializer_values,
'''
new = '''            (object->is_zero_initialized && object->initializer_count != 0U) ||
            (object->relocation_count != 0U && !object->is_zero_initialized &&
             (!minic_type_is_record(object->type) || object->initializer_count == 0U)) ||
            !storage_is_valid(object->initializer_values,
'''
if text.count(old) != 1:
    raise SystemExit(f'verifier relocation/base mode mismatch: {text.count(old)}')
text = text.replace(old, new, 1)

# Insert strict mixed-record shape checks before relocation iteration.
anchor = '''        {
            size_t relocation_index;

            for (relocation_index = 0U; relocation_index < object->relocation_count;
'''
addition = '''        if (object->relocation_count != 0U && !object->is_zero_initialized) {
            const MinicRecord *record;
            size_t relocation_index;

            record = minic_c0_program_record(program, object->type.record_id);
            if (record == NULL || !record->is_complete || record->is_union ||
                object->initializer_count != record->field_count) {
                return false;
            }
            for (relocation_index = 0U; relocation_index < object->relocation_count;
                 ++relocation_index) {
                const MinicGlobalRelocation *relocation;

                relocation = &object->relocations[relocation_index];
                if (relocation->location_kind != MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD ||
                    relocation->location_index >= object->initializer_count ||
                    object->initializer_values[relocation->location_index] != 0) {
                    return false;
                }
            }
        }
'''
if text.count(anchor) != 1:
    raise SystemExit(f'verifier relocation iteration anchor mismatch: {text.count(anchor)}')
text = text.replace(anchor, addition + anchor, 1)
verifier.write_text(text)

# --- RV64: direct-record emission merges constant base fields and symbolic overlays. ---
codegen = root / 'src/target/riscv64/codegen_function.c'
text = codegen.read_text()

# Helper for the symbolic target name shared by the zero-base and mixed-record emitters.
anchor = '''static bool
emit_symbol_relocs(FILE *file, const MinicC0Program *program, const MinicGlobalObject *object) {
'''
helper = '''static const char *minic_riscv64_global_relocation_target_name(
    const MinicC0Program *program, const MinicGlobalRelocation *relocation) {
    if (program == NULL || relocation == NULL) {
        return NULL;
    }
    if (relocation->target_kind == MINIC_GLOBAL_RELOCATION_OBJECT) {
        const MinicGlobalObject *target;

        target = minic_c0_program_global_object(program, relocation->target_id);
        return target != NULL && target->name_length != 0U ? target->name : NULL;
    }
    if (relocation->target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION) {
        const MinicFunction *target;

        target = minic_c0_program_function(program, relocation->target_id);
        return target != NULL && target->name_length != 0U
                   ? minic_c0_function_symbol_name(target)
                   : NULL;
    }
    return NULL;
}

'''
if text.count(anchor) != 1:
    raise SystemExit(f'RV64 relocation emitter anchor mismatch: {text.count(anchor)}')
text = text.replace(anchor, helper + anchor, 1)

old = '''        target_name = NULL;
        if (!minic_data_layout_global_relocation_offset(
                minic_default_data_layout(), program, object, relocation, &storage_offset)) {
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
'''
new = '''        target_name = minic_riscv64_global_relocation_target_name(program, relocation);
        if (!minic_data_layout_global_relocation_offset(
                minic_default_data_layout(), program, object, relocation, &storage_offset)) {
            return false;
        }
'''
if text.count(old) != 1:
    raise SystemExit(f'RV64 target-name duplication mismatch: {text.count(old)}')
text = text.replace(old, new, 1)

# Replace direct-record emitter with relocation-aware field walk.
start = text.find('static bool minic_riscv64_emit_direct_record_values(')
end = text.find('static bool minic_riscv64_emit_constant_value(', start)
if start < 0 or end < 0:
    raise SystemExit('RV64 direct record emitter region mismatch')
replacement = r'''static bool minic_riscv64_emit_direct_record_values(FILE *file,
                                                    const MinicC0Program *program,
                                                    const MinicGlobalObject *object,
                                                    const MinicRecord *record) {
    size_t cursor;
    size_t field_index;
    size_t relocation_index;

    if (file == NULL || program == NULL || object == NULL || record == NULL ||
        !record->is_complete || record->is_union ||
        object->initializer_count != record->field_count) {
        return false;
    }
    cursor = 0U;
    relocation_index = 0U;
    for (field_index = 0U; field_index < record->field_count; ++field_index) {
        const MinicRecordField *field;
        const MinicGlobalRelocation *relocation;
        size_t field_size;
        size_t field_alignment;
        size_t field_offset;
        int value;

        field = minic_c0_record_field(record, field_index);
        if (field == NULL || field->element_count != 1U || field->is_flexible_array ||
            !minic_riscv64_type_layout(program, field->type, &field_size, &field_alignment)) {
            return false;
        }
        (void)field_alignment;
        field_offset = field->storage_offset;
        if (field_offset < cursor || field_offset > object->storage_size ||
            field_size > object->storage_size - field_offset ||
            !minic_riscv64_emit_zero_bytes(file, field_offset - cursor)) {
            return false;
        }
        value = object->initializer_values[field_index];
        relocation = relocation_index < object->relocation_count
                         ? &object->relocations[relocation_index]
                         : NULL;
        if (relocation != NULL &&
            relocation->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD &&
            relocation->location_index == field_index) {
            const char *directive;
            const char *target_name;

            directive = minic_riscv64_integer_data_directive(field_size);
            target_name = minic_riscv64_global_relocation_target_name(program, relocation);
            if (!minic_type_is_pointer(field->type) || value != 0 || directive == NULL ||
                target_name == NULL || target_name[0] == '\0' ||
                fprintf(file, "  %s %s\n", directive, target_name) < 0) {
                return false;
            }
            relocation_index += 1U;
        } else if (minic_type_is_integer(field->type)) {
            const char *directive;

            directive = minic_riscv64_integer_data_directive(field_size);
            if (directive == NULL) {
                return false;
            }
            if (field_size == 1U) {
                unsigned int byte_value;

                byte_value = (unsigned int)value & 0xffU;
                if (fprintf(file, "  %s %u\n", directive, byte_value) < 0) {
                    return false;
                }
            } else if (fprintf(file, "  %s %d\n", directive, value) < 0) {
                return false;
            }
        } else {
            if (value != 0 ||
                (!minic_type_is_record(field->type) && !minic_type_is_pointer(field->type)) ||
                !minic_riscv64_emit_zero_bytes(file, field_size)) {
                return false;
            }
        }
        cursor = field_offset + field_size;
    }
    return relocation_index == object->relocation_count && cursor <= object->storage_size &&
           minic_riscv64_emit_zero_bytes(file, object->storage_size - cursor);
}

'''
text = text[:start] + replacement + text[end:]

old = '''    if (file == NULL || program == NULL || object == NULL || !minic_type_is_record(object->type) ||
        object->is_zero_initialized || object->relocation_count != 0U) {
        return false;
    }
'''
new = '''    if (file == NULL || program == NULL || object == NULL || !minic_type_is_record(object->type) ||
        object->is_zero_initialized) {
        return false;
    }
'''
if text.count(old) != 1:
    raise SystemExit(f'record values relocation gate mismatch: {text.count(old)}')
text = text.replace(old, new, 1)

old = '''        record = minic_c0_program_record(program, object->type.record_id);
        if (record == NULL || !record->is_complete || object->relocation_count != 0U ||
            object->initializer_count == 0U) {
            return false;
        }
'''
new = '''        record = minic_c0_program_record(program, object->type.record_id);
        if (record == NULL || !record->is_complete || object->initializer_count == 0U) {
            return false;
        }
'''
if text.count(old) != 1:
    raise SystemExit(f'global record classification mismatch: {text.count(old)}')
text = text.replace(old, new, 1)

old = '''    if (object->relocation_count != 0U) {
        if (!emit_symbol_relocs(file, program, object)) {
            return false;
        }
    } else if (object->is_zero_initialized || object->is_tentative) {
'''
new = '''    if (minic_type_is_record(object->type) && object->initializer_count != 0U) {
        if (!minic_riscv64_emit_record_values(file, program, object)) {
            return false;
        }
    } else if (object->relocation_count != 0U) {
        if (!emit_symbol_relocs(file, program, object)) {
            return false;
        }
    } else if (object->is_zero_initialized || object->is_tentative) {
'''
if text.count(old) != 1:
    raise SystemExit(f'global mixed record emission dispatch mismatch: {text.count(old)}')
text = text.replace(old, new, 1)
codegen.write_text(text)

# --- Focused owner: same mixed relocation fixture now carries a nonzero scalar base field. ---
fixture = root / 'tests/compiler/c0/static_mixed_symbol_relocations.c'
text = fixture.read_text()
old = 'static struct setup_entry entry = {setup_name, setup_fn, 0};\n'
new = 'static struct setup_entry entry = {setup_name, setup_fn, 1};\n'
if text.count(old) != 1:
    raise SystemExit(f'mixed fixture scalar anchor mismatch: {text.count(old)}')
fixture.write_text(text.replace(old, new, 1))

script = root / 'tests/compiler/c0/run-static-mixed-symbol-relocations.sh'
text = script.read_text()
old = '''grep -F '.dword setup_name' "$work/static_mixed_symbol_relocations.s" >/dev/null
grep -F '.dword setup_fn' "$work/static_mixed_symbol_relocations.s" >/dev/null
'''
new = '''grep -F '.dword setup_name' "$work/static_mixed_symbol_relocations.s" >/dev/null
grep -F '.dword setup_fn' "$work/static_mixed_symbol_relocations.s" >/dev/null
grep -F '  .word 1' "$work/static_mixed_symbol_relocations.s" >/dev/null
'''
if text.count(old) != 1:
    raise SystemExit(f'mixed gate output anchor mismatch: {text.count(old)}')
text = text.replace(old, new, 1)
old = "printf '%s\\n' 'PASS compiler/c0/static-mixed-symbol-relocations location=storage-byte-offset target=object+function mixed-record=accepted type=checked'\n"
new = "printf '%s\\n' 'PASS compiler/c0/static-mixed-symbol-relocations location=semantic-record-field target=object+function base=constant-overlay scalar=nonzero type=checked'\n"
if text.count(old) != 1:
    raise SystemExit(f'mixed gate PASS anchor mismatch: {text.count(old)}')
script.write_text(text.replace(old, new, 1))
