#!/usr/bin/env python3
from pathlib import Path

path = Path('src/frontend/ast_verifier.c')
text = path.read_text()

repls = [
("""        if ((array_type->element_count == 0U &&
             !incomplete_array_has_semantic_owner(program, index)) ||
            !type_is_valid(program, array_type->element_type) ||
            minic_type_is_function(array_type->element_type)) {
            return false;
        }
""",
"""        if ((array_type->element_count == 0U &&
             !incomplete_array_has_semantic_owner(program, index)) ||
            !type_is_valid(program, array_type->element_type) ||
            minic_type_is_function(array_type->element_type)) {
            fprintf(stderr,
                    "VERIFY_STAGE array_type=%zu count=%zu base=%d ptr=%u owner=%d valid=%d function=%d\\n",
                    index,
                    array_type->element_count,
                    (int)array_type->element_type.base_kind,
                    array_type->element_type.pointer_depth,
                    incomplete_array_has_semantic_owner(program, index) ? 1 : 0,
                    type_is_valid(program, array_type->element_type) ? 1 : 0,
                    minic_type_is_function(array_type->element_type) ? 1 : 0);
            return false;
        }
"""),
("""        if (program->type_aliases[index].name == NULL ||
            !type_is_valid(program, program->type_aliases[index].type)) {
            return false;
        }
""",
"""        if (program->type_aliases[index].name == NULL ||
            !type_is_valid(program, program->type_aliases[index].type)) {
            fprintf(stderr,
                    "VERIFY_STAGE type_alias=%zu name=%s base=%d ptr=%u valid=%d\\n",
                    index,
                    program->type_aliases[index].name == NULL ? "<null>" : program->type_aliases[index].name,
                    (int)program->type_aliases[index].type.base_kind,
                    program->type_aliases[index].type.pointer_depth,
                    type_is_valid(program, program->type_aliases[index].type) ? 1 : 0);
            return false;
        }
"""),
("""        if (object->name == NULL || !type_is_valid(program, object->type) ||
            minic_type_is_function(object->type) ||
            (minic_type_is_void(object->type) && !object->is_extern) ||
            (object->is_extern &&
             (object->is_tentative || object->is_internal || object->is_zero_initialized ||
              object->initializer_count != 0U || object->relocation_count != 0U)) ||
            (object->is_tentative &&
             (object->is_extern || object->is_zero_initialized || object->initializer_count != 0U ||
              object->relocation_count != 0U)) ||
            (object->is_zero_initialized && object->initializer_count != 0U) ||
            (object->relocation_count != 0U && !object->is_zero_initialized &&
             (!minic_type_is_record(object->type) || object->initializer_count == 0U)) ||
            !storage_is_valid(object->initializer_values,
                              object->initializer_count,
                              object->initializer_capacity) ||
            !storage_is_valid(
                object->relocations, object->relocation_count, object->relocation_capacity)) {
            return false;
        }
""",
"""        if (object->name == NULL || !type_is_valid(program, object->type) ||
            minic_type_is_function(object->type) ||
            (minic_type_is_void(object->type) && !object->is_extern) ||
            (object->is_extern &&
             (object->is_tentative || object->is_internal || object->is_zero_initialized ||
              object->initializer_count != 0U || object->relocation_count != 0U)) ||
            (object->is_tentative &&
             (object->is_extern || object->is_zero_initialized || object->initializer_count != 0U ||
              object->relocation_count != 0U)) ||
            (object->is_zero_initialized && object->initializer_count != 0U) ||
            (object->relocation_count != 0U && !object->is_zero_initialized &&
             (!minic_type_is_record(object->type) || object->initializer_count == 0U)) ||
            !storage_is_valid(object->initializer_values,
                              object->initializer_count,
                              object->initializer_capacity) ||
            !storage_is_valid(
                object->relocations, object->relocation_count, object->relocation_capacity)) {
            fprintf(stderr,
                    "VERIFY_STAGE global=%zu name=%s base=%d ptr=%u extern=%d tentative=%d internal=%d zero=%d init=%zu reloc=%zu valid=%d\\n",
                    index,
                    object->name == NULL ? "<null>" : object->name,
                    (int)object->type.base_kind,
                    object->type.pointer_depth,
                    object->is_extern ? 1 : 0,
                    object->is_tentative ? 1 : 0,
                    object->is_internal ? 1 : 0,
                    object->is_zero_initialized ? 1 : 0,
                    object->initializer_count,
                    object->relocation_count,
                    type_is_valid(program, object->type) ? 1 : 0);
            return false;
        }
"""),
("""                if (!minic_c0_global_relocation_slot_type(program,
                                                          object,
                                                          relocation->location_kind,
                                                          relocation->location_index,
                                                          &slot_type)) {
                    return false;
                }
""",
"""                if (!minic_c0_global_relocation_slot_type(program,
                                                          object,
                                                          relocation->location_kind,
                                                          relocation->location_index,
                                                          &slot_type)) {
                    fprintf(stderr,
                            "VERIFY_STAGE relocation-slot global=%zu name=%s reloc=%zu kind=%d index=%zu\\n",
                            index,
                            object->name,
                            relocation_index,
                            (int)relocation->location_kind,
                            relocation->location_index);
                    return false;
                }
"""),
("""                    return false;
                }
                if (relocation->target_kind == MINIC_GLOBAL_RELOCATION_OBJECT &&
                    !minic_c0_global_relocation_object_target_compatible(
                        program, relocation, slot_type)) {
                    return false;
                }
""",
"""                    fprintf(stderr,
                            "VERIFY_STAGE relocation-contract global=%zu name=%s reloc=%zu kind=%d index=%zu target_kind=%d target=%zu slot_base=%d slot_ptr=%u pointee_base=%d pointee_ptr=%u\\n",
                            index,
                            object->name,
                            relocation_index,
                            (int)relocation->location_kind,
                            relocation->location_index,
                            (int)relocation->target_kind,
                            relocation->target_id,
                            (int)slot_type.base_kind,
                            slot_type.pointer_depth,
                            (int)slot_pointee.base_kind,
                            slot_pointee.pointer_depth);
                    return false;
                }
                if (relocation->target_kind == MINIC_GLOBAL_RELOCATION_OBJECT &&
                    !minic_c0_global_relocation_object_target_compatible(
                        program, relocation, slot_type)) {
                    fprintf(stderr,
                            "VERIFY_STAGE relocation-target global=%zu name=%s reloc=%zu target=%zu slot_base=%d slot_ptr=%u\\n",
                            index,
                            object->name,
                            relocation_index,
                            relocation->target_id,
                            (int)slot_type.base_kind,
                            slot_type.pointer_depth);
                    return false;
                }
""")
]

for old, new in repls:
    if old not in text:
        raise SystemExit('debug anchor missing')
    text = text.replace(old, new, 1)

path.write_text(text)
