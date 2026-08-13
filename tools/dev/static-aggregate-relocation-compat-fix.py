from pathlib import Path

root = Path('.')

def replace_once(path, old, new, label):
    p = root / path
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label} mismatch: {count}')
    p.write_text(text.replace(old, new, 1))

replace_once(
    'src/frontend/ast.h',
    '''bool minic_c0_global_relocation_object_target_type(const MinicC0Program *program,\n                                                   const MinicGlobalRelocation *relocation,\n                                                   MinicType *target_type);\n''',
    '''bool minic_c0_global_relocation_object_target_type(const MinicC0Program *program,\n                                                   const MinicGlobalRelocation *relocation,\n                                                   MinicType *target_type);\nbool minic_c0_global_relocation_object_target_compatible(\n    const MinicC0Program *program,\n    const MinicGlobalRelocation *relocation,\n    MinicType slot_type);\n''',
    'relocation target compatibility declaration')

astg = root / 'src/frontend/ast_global.c'
text = astg.read_text()
anchor = '''bool minic_c0_global_relocation_object_target_type(const MinicC0Program *program,\n                                                   const MinicGlobalRelocation *relocation,\n                                                   MinicType *target_type) {\n    if (program == NULL || relocation == NULL || target_type == NULL ||\n        relocation->target_kind != MINIC_GLOBAL_RELOCATION_OBJECT ||\n        relocation->target_id >= program->global_object_count) {\n        return false;\n    }\n    return global_object_member_path_type(program,\n                                          &program->global_objects[relocation->target_id],\n                                          relocation->target_member_indices,\n                                          relocation->target_member_depth,\n                                          target_type);\n}\n'''
addition = anchor + r'''

static bool global_relocation_object_target_type_compatible(const MinicC0Program *program,
                                                            MinicType slot_type,
                                                            MinicType target_type) {
    MinicType source_pointer_type;

    if (program == NULL || !minic_type_is_pointer(slot_type)) {
        return false;
    }
    /* A symbolic object address can denote the object itself (`&object`). */
    if (minic_type_pointer_to(target_type, &source_pointer_type) &&
        minic_type_assignment_compatible(slot_type, source_pointer_type)) {
        return true;
    }
    /* Array-to-pointer decay and `&array[0]` have the same symbol/addend as
     * `&array`, but their C type is pointer-to-element rather than pointer-to-array.
     * Preserve that semantic alternative in the persisted relocation contract. */
    if (minic_type_is_array(target_type)) {
        const MinicArrayType *array_type;

        array_type = minic_c0_program_array_type(program, target_type.array_type_id);
        if (array_type != NULL &&
            minic_type_pointer_to(array_type->element_type, &source_pointer_type) &&
            minic_type_assignment_compatible(slot_type, source_pointer_type)) {
            return true;
        }
    }
    return false;
}

bool minic_c0_global_relocation_object_target_compatible(
    const MinicC0Program *program,
    const MinicGlobalRelocation *relocation,
    MinicType slot_type) {
    MinicType target_type;

    return minic_c0_global_relocation_object_target_type(program, relocation, &target_type) &&
           global_relocation_object_target_type_compatible(program, slot_type, target_type);
}
'''
if text.count(anchor) != 1:
    raise SystemExit(f'target compatibility insertion mismatch: {text.count(anchor)}')
text = text.replace(anchor, addition, 1)

# Entity construction uses the same semantic rule before persisting the relocation.
old = '''    MinicType slot_pointee;\n    MinicType slot_type;\n    MinicType target_pointer_type;\n    MinicType target_type;\n    size_t path_index;\n'''
new = '''    MinicType slot_pointee;\n    MinicType slot_type;\n    MinicType target_type;\n    size_t path_index;\n'''
if text.count(old) != 1:
    raise SystemExit(f'entity target pointer local cleanup mismatch: {text.count(old)}')
text = text.replace(old, new, 1)
old = '''        (target_kind == MINIC_GLOBAL_RELOCATION_OBJECT &&\n         (!global_object_member_path_type(program,\n                                          &program->global_objects[target_id],\n                                          target_member_indices,\n                                          target_member_depth,\n                                          &target_type) ||\n          !minic_type_pointer_to(target_type, &target_pointer_type) ||\n          !minic_type_assignment_compatible(slot_type, target_pointer_type))) ||\n'''
new = '''        (target_kind == MINIC_GLOBAL_RELOCATION_OBJECT &&\n         (!global_object_member_path_type(program,\n                                          &program->global_objects[target_id],\n                                          target_member_indices,\n                                          target_member_depth,\n                                          &target_type) ||\n          !global_relocation_object_target_type_compatible(\n              program, slot_type, target_type))) ||\n'''
if text.count(old) != 1:
    raise SystemExit(f'entity target compatibility replacement mismatch: {text.count(old)}')
text = text.replace(old, new, 1)
astg.write_text(text)

# Verifier shares the exact same persisted-relocation compatibility rule.
v = root / 'src/frontend/ast_verifier.c'
text = v.read_text()
old = '''                if (relocation->target_kind == MINIC_GLOBAL_RELOCATION_OBJECT) {\n                    MinicType target_pointer_type;\n                    MinicType target_type;\n\n                    if (!minic_c0_global_relocation_object_target_type(\n                            program, relocation, &target_type) ||\n                        !minic_type_pointer_to(target_type, &target_pointer_type) ||\n                        !minic_type_assignment_compatible(slot_type, target_pointer_type)) {\n                        return false;\n                    }\n                }\n'''
new = '''                if (relocation->target_kind == MINIC_GLOBAL_RELOCATION_OBJECT &&\n                    !minic_c0_global_relocation_object_target_compatible(\n                        program, relocation, slot_type)) {\n                    return false;\n                }\n'''
if text.count(old) != 1:
    raise SystemExit(f'verifier target compatibility replacement mismatch: {text.count(old)}')
v.write_text(text.replace(old, new, 1))

# Freeze the exact class that regressed Linux: const pointer array initialized
# from array/string objects through array-to-pointer decay.
program = root / 'tests/programs/c0/static_record_compound_literal.c'
text = program.read_text()
insert = r'''
static const char backing_name[] = "backing";
static const char *const relocation_names[] = { backing_name, "literal" };
'''
anchor = 'static Outer value = {'
if text.count(anchor) != 1:
    raise SystemExit(f'array-decay regression program anchor mismatch: {text.count(anchor)}')
text = text.replace(anchor, insert + '\n' + anchor, 1)
old = '''    return value.tag == 3 && value.inner.first == 0 && value.inner.magic == 0xdead4eadU &&\n                   value.inner.second == 7 && value.inner.owner == (void *)-1L &&\n                   value.inner.link.next == &value.inner.link &&\n                   value.inner.link.prev == &value.inner.link\n               ? 0\n               : 1;\n'''
new = '''    return value.tag == 3 && value.inner.first == 0 && value.inner.magic == 0xdead4eadU &&\n                   value.inner.second == 7 && value.inner.owner == (void *)-1L &&\n                   value.inner.link.next == &value.inner.link &&\n                   value.inner.link.prev == &value.inner.link &&\n                   relocation_names[0] == backing_name && relocation_names[0][0] == 'b' &&\n                   relocation_names[1][0] == 'l'\n               ? 0\n               : 1;\n'''
if text.count(old) != 1:
    raise SystemExit(f'array-decay regression return anchor mismatch: {text.count(old)}')
program.write_text(text.replace(old, new, 1))
