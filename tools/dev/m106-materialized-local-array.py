#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    p = Path(path)
    s = p.read_text()
    count = s.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, got {count}")
    p.write_text(s.replace(old, new, 1))


p = Path('src/core/core_lower.c')
if 'M106_MATERIALIZED_LOCAL_ARRAY_OBJECT' in p.read_text():
    print('M106 already applied')
    raise SystemExit(0)

replace_once(
    'src/core/core_lower.c',
    '''    if (!core_memory_scalar_type(local->type) && !minic_type_is_record(local->type)) {\n        return MINIC_CORE_LOWER_UNSUPPORTED;\n    }\n    if (local->is_array) {\n''',
    '''    /* M106_MATERIALIZED_LOCAL_ARRAY_OBJECT: frontend array convergence has\n       two local-object forms. Legacy locals keep element type + is_array/count;\n       typedef/materialized locals carry one complete array MinicType directly.\n       A materialized array is one Core object whose DataLayout already owns the\n       full extent, so its address is naturally pointer-to-array. */\n    if (minic_type_is_array(local->type)) {\n        const MinicArrayType *array_type;\n\n        array_type = minic_c0_program_array_type(\n            context->body->program, local->type.array_type_id);\n        if (local->is_array || array_type == NULL || array_type->element_count == 0U ||\n            array_type->is_zero_length) {\n            return MINIC_CORE_LOWER_UNSUPPORTED;\n        }\n        if (!minic_core_function_add_object(\n                context->function, local->name_span, local->type, object_id)) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        context->local_objects[local_index] = *object_id;\n        return MINIC_CORE_LOWER_OK;\n    }\n    if (!core_memory_scalar_type(local->type) && !minic_type_is_record(local->type)) {\n        return MINIC_CORE_LOWER_UNSUPPORTED;\n    }\n    if (local->is_array) {\n''',
    'materialized local array object')

t = Path('tests/core/run-core-ir-shadow.sh')
ts = t.read_text()
anchor = '''cat >"$work_dir/fixed-register-sbi-ecall.i" <<'EOF'\n'''
case = '''cat >"$work_dir/materialized-local-array-address.i" <<'EOF'\nstruct core_local_mask { unsigned long bits[1]; };\ntypedef struct core_local_mask core_local_mask_var_t[1];\nint core_consume_local_mask(core_local_mask_var_t *mask);\n\nint materialized_local_array_address(void) {\n    core_local_mask_var_t mask;\n    return core_consume_local_mask(&mask);\n}\nEOF\ncheck_strict_case materialized-local-array-address\n\n'''
if ts.count(anchor) != 1:
    raise SystemExit(f'core shadow insertion anchor count={ts.count(anchor)}')
t.write_text(ts.replace(anchor, case + anchor, 1))

print('M106 materialized local array Core support applied')
