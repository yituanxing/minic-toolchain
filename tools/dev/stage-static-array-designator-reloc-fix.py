#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:140]!r}")
    p.write_text(text.replace(old, new, 1))


ast_anchor = """bool minic_c0_global_relocation_object_target_compatible(const MinicC0Program *program,
                                                         const MinicGlobalRelocation *relocation,
                                                         MinicType slot_type);
"""
ast_add = ast_anchor + """bool minic_c0_global_relocation_function_target_compatible(
    const MinicC0Program *program,
    MinicType slot_type,
    MinicFunctionId function_id,
    bool has_explicit_pointer_cast);
"""
replace_once("src/frontend/ast.h", ast_anchor, ast_add)

global_anchor = """bool minic_c0_global_relocation_object_target_compatible(const MinicC0Program *program,
                                                         const MinicGlobalRelocation *relocation,
                                                         MinicType slot_type) {
    MinicType target_type;

    return minic_c0_global_relocation_object_target_type(program, relocation, &target_type) &&
           global_relocation_object_target_type_compatible(
               program, slot_type, target_type, relocation->has_explicit_pointer_cast);
}
"""
global_add = global_anchor + """
bool minic_c0_global_relocation_function_target_compatible(
    const MinicC0Program *program,
    MinicType slot_type,
    MinicFunctionId function_id,
    bool has_explicit_pointer_cast) {
    MinicType slot_pointee;

    return program != NULL && function_id < program->function_count &&
           minic_type_is_pointer(slot_type) && minic_type_pointee(slot_type, &slot_pointee) &&
           (has_explicit_pointer_cast || minic_type_is_function(slot_pointee) ||
            minic_type_is_void(slot_pointee));
}
"""
replace_once("src/frontend/ast_global.c", global_anchor, global_add)

old_add_check = """        (target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION && !minic_type_is_function(slot_pointee) &&
         !has_explicit_pointer_cast) ||
"""
new_add_check = """        (target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION &&
         !minic_c0_global_relocation_function_target_compatible(
             program, slot_type, (MinicFunctionId)target_id, has_explicit_pointer_cast)) ||
"""
replace_once("src/frontend/ast_global.c", old_add_check, new_add_check)

old_verify = """                    (relocation->target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION &&
                     (relocation->target_id >= program->function_count ||
                      (!minic_type_is_function(slot_pointee) &&
                       !relocation->has_explicit_pointer_cast))) ||
"""
new_verify = """                    (relocation->target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION &&
                     !minic_c0_global_relocation_function_target_compatible(
                         program,
                         slot_type,
                         (MinicFunctionId)relocation->target_id,
                         relocation->has_explicit_pointer_cast)) ||
"""
replace_once("src/frontend/ast_verifier.c", old_verify, new_verify)

print("staged canonical function-relocation void-pointer bridge")
