from pathlib import Path

root = Path('.')

def replace_once(path, old, new, label):
    p = root / path
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label} mismatch: {count}')
    p.write_text(text.replace(old, new, 1))

# Expose the entity-owned semantic type queries so producers and the verifier
# share exactly one interpretation of relocation location/target semantics.
replace_once(
    'src/frontend/ast.h',
    '''bool minic_c0_global_object_add_object_relocation_path(\n    MinicC0Program *program,\n    MinicGlobalObjectId global_object_id,\n    MinicGlobalRelocationLocationKind location_kind,\n    size_t location_index,\n    MinicGlobalObjectId target_object_id,\n    const size_t *target_member_indices,\n    size_t target_member_depth);\n''',
    '''bool minic_c0_global_object_add_object_relocation_path(\n    MinicC0Program *program,\n    MinicGlobalObjectId global_object_id,\n    MinicGlobalRelocationLocationKind location_kind,\n    size_t location_index,\n    MinicGlobalObjectId target_object_id,\n    const size_t *target_member_indices,\n    size_t target_member_depth);\nbool minic_c0_global_relocation_slot_type(const MinicC0Program *program,\n                                          const MinicGlobalObject *object,\n                                          MinicGlobalRelocationLocationKind location_kind,\n                                          size_t location_index,\n                                          MinicType *slot_type);\nbool minic_c0_global_relocation_object_target_type(const MinicC0Program *program,\n                                                   const MinicGlobalRelocation *relocation,\n                                                   MinicType *target_type);\n''',
    'relocation semantic type declarations')

astg = root / 'src/frontend/ast_global.c'
text = astg.read_text()
text = text.replace('static bool global_object_member_path_type(',
                    'static bool global_object_member_path_type(', 1)
# Promote location resolver.
old_sig = '''static bool global_relocation_location_type(const MinicC0Program *program,\n                                            const MinicGlobalObject *object,\n                                            MinicGlobalRelocationLocationKind location_kind,\n                                            size_t location_index,\n                                            MinicType *slot_type) {\n'''
new_sig = '''bool minic_c0_global_relocation_slot_type(const MinicC0Program *program,\n                                          const MinicGlobalObject *object,\n                                          MinicGlobalRelocationLocationKind location_kind,\n                                          size_t location_index,\n                                          MinicType *slot_type) {\n'''
if text.count(old_sig) != 1:
    raise SystemExit(f'location type promotion mismatch: {text.count(old_sig)}')
text = text.replace(old_sig, new_sig, 1)
text = text.replace('global_relocation_location_type(',
                    'minic_c0_global_relocation_slot_type(')
# Add target object type query after the private path walker.
anchor = '''    *result_type = type;\n    return true;\n}\n\nbool minic_c0_global_relocation_slot_type'''
addition = '''    *result_type = type;\n    return true;\n}\n\nbool minic_c0_global_relocation_object_target_type(const MinicC0Program *program,\n                                                   const MinicGlobalRelocation *relocation,\n                                                   MinicType *target_type) {\n    if (program == NULL || relocation == NULL || target_type == NULL ||\n        relocation->target_kind != MINIC_GLOBAL_RELOCATION_OBJECT ||\n        relocation->target_id >= program->global_object_count) {\n        return false;\n    }\n    return global_object_member_path_type(program,\n                                          &program->global_objects[relocation->target_id],\n                                          relocation->target_member_indices,\n                                          relocation->target_member_depth,\n                                          target_type);\n}\n\nbool minic_c0_global_relocation_slot_type'''
if text.count(anchor) != 1:
    raise SystemExit(f'target type query insertion mismatch: {text.count(anchor)}')
text = text.replace(anchor, addition, 1)
# Entity construction must verify the symbolic object address has a type that
# can actually initialize the destination pointer slot.
old = '''    MinicType slot_pointee;\n    MinicType slot_type;\n    MinicType target_type;\n    size_t path_index;\n'''
new = '''    MinicType slot_pointee;\n    MinicType slot_type;\n    MinicType target_pointer_type;\n    MinicType target_type;\n    size_t path_index;\n'''
if text.count(old) != 1:
    raise SystemExit(f'entity target pointer local mismatch: {text.count(old)}')
text = text.replace(old, new, 1)
old = '''        (target_kind == MINIC_GLOBAL_RELOCATION_OBJECT &&\n         !global_object_member_path_type(program,\n                                         &program->global_objects[target_id],\n                                         target_member_indices,\n                                         target_member_depth,\n                                         &target_type)) ||\n        object->is_tentative ||\n'''
new = '''        (target_kind == MINIC_GLOBAL_RELOCATION_OBJECT &&\n         (!global_object_member_path_type(program,\n                                          &program->global_objects[target_id],\n                                          target_member_indices,\n                                          target_member_depth,\n                                          &target_type) ||\n          !minic_type_pointer_to(target_type, &target_pointer_type) ||\n          !minic_type_assignment_compatible(slot_type, target_pointer_type))) ||\n        object->is_tentative ||\n'''
if text.count(old) != 1:
    raise SystemExit(f'entity target compatibility mismatch: {text.count(old)}')
text = text.replace(old, new, 1)
text = text.replace('    (void)target_type;\n', '', 1)
astg.write_text(text)

# Verifier reuses the entity semantic type queries instead of fabricating void*.
v = root / 'src/frontend/ast_verifier.c'
text = v.read_text()
start_marker = '''                relocation = &object->relocations[relocation_index];\n                if (relocation->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_SCALAR) {\n'''
end_marker = '''                if (!minic_type_pointee(slot_type, &slot_pointee) ||\n'''
start = text.find(start_marker)
if start < 0:
    raise SystemExit('verifier location branch start missing')
end = text.find(end_marker, start)
if end < 0:
    raise SystemExit('verifier location branch end missing')
replacement = '''                relocation = &object->relocations[relocation_index];\n                if (!minic_c0_global_relocation_slot_type(program,\n                                                          object,\n                                                          relocation->location_kind,\n                                                          relocation->location_index,\n                                                          &slot_type)) {\n                    return false;\n                }\n'''
text = text[:start] + replacement + text[end:]
# Independently verify object-target type compatibility from persisted semantic path.
needle = '''                {\n                    size_t target_addend;\n\n                    if (!minic_data_layout_global_relocation_target_addend(\n                            minic_target_info_data_layout(target),\n                            program,\n                            relocation,\n                            &target_addend)) {\n                        return false;\n                    }\n                    (void)target_addend;\n                }\n'''
replacement = '''                if (relocation->target_kind == MINIC_GLOBAL_RELOCATION_OBJECT) {\n                    MinicType target_pointer_type;\n                    MinicType target_type;\n\n                    if (!minic_c0_global_relocation_object_target_type(\n                            program, relocation, &target_type) ||\n                        !minic_type_pointer_to(target_type, &target_pointer_type) ||\n                        !minic_type_assignment_compatible(slot_type, target_pointer_type)) {\n                        return false;\n                    }\n                }\n                {\n                    size_t target_addend;\n\n                    if (!minic_data_layout_global_relocation_target_addend(\n                            minic_target_info_data_layout(target),\n                            program,\n                            relocation,\n                            &target_addend)) {\n                        return false;\n                    }\n                    (void)target_addend;\n                }\n'''
if text.count(needle) != 1:
    raise SystemExit(f'verifier target semantic check mismatch: {text.count(needle)}')
text = text.replace(needle, replacement, 1)
v.write_text(text)

# Productize the discovery gate and run the positive as a normal C0 program too.
old_script = root / 'tests/compiler/c0/run-static-aggregate-family-discovery.sh'
new_script = root / 'tests/compiler/c0/run-static-aggregate-initializers.sh'
if not old_script.exists() or new_script.exists():
    raise SystemExit('focused script rename state mismatch')
old_script.rename(new_script)
text = new_script.read_text().replace(
    'static-aggregate-family-discovery', 'static-aggregate-initializers')
text = text.replace('PASS compiler/c0/static-aggregate-family ',
                    'PASS compiler/c0/static-aggregate-initializers ')
new_script.write_text(text)

old_positive = root / 'tests/compiler/c0/static_record_compound_literal.c'
new_positive = root / 'tests/programs/c0/static_record_compound_literal.c'
if not old_positive.exists() or new_positive.exists():
    raise SystemExit('positive program move state mismatch')
old_positive.rename(new_positive)
text = new_script.read_text().replace(
    '"$root/tests/compiler/c0/static_record_compound_literal.c"',
    '"$root/tests/programs/c0/static_record_compound_literal.c"')
new_script.write_text(text)

manifest = root / 'tests/programs/c0/manifest.txt'
text = manifest.read_text()
if '\nstatic_record_compound_literal\n' not in '\n' + text:
    if not text.endswith('\n'):
        text += '\n'
    text += 'static_record_compound_literal\n'
manifest.write_text(text)

# Permanent full compiler focused gate.
gate = root / '.github/scripts/compiler-c0-full-gate.sh'
text = gate.read_text()
anchor = '''predefined_func_name_focused() {\n    MINIC="$root/build/ci-debug/bin/minic" \\\n    BUILD_DIR="$root/build/ci-predefined-func-name" \\\n        sh tests/compiler/c0/run-predefined-func-name.sh\n}\n'''
addition = anchor + '''\nstatic_aggregate_initializer_focused() {\n    MINIC="$root/build/ci-debug/bin/minic" \\\n    BUILD_DIR="$root/build/ci-static-aggregate-initializers" \\\n        sh tests/compiler/c0/run-static-aggregate-initializers.sh\n}\n'''
if text.count(anchor) != 1:
    raise SystemExit(f'focused gate function anchor mismatch: {text.count(anchor)}')
text = text.replace(anchor, addition, 1)
anchor = 'start_gate predefined-func-name-focused predefined_func_name_focused\n'
addition = anchor + 'start_gate static-aggregate-initializer-focused static_aggregate_initializer_focused\n'
if text.count(anchor) != 1:
    raise SystemExit(f'focused gate start anchor mismatch: {text.count(anchor)}')
gate.write_text(text.replace(anchor, addition, 1))
