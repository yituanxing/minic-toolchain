#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / "src/frontend/ast_verifier.c"
text = path.read_text()

old = """        array_type = &program->array_types[index];
        if ((array_type->element_count == 0U &&
             !incomplete_array_has_semantic_owner(program, index)) ||
            !type_is_valid(program, target, array_type->element_type) ||
            minic_type_is_function(array_type->element_type)) {
            return false;
        }
"""
new = """        array_type = &program->array_types[index];
        {
            bool has_owner;
            bool element_valid;
            bool element_function;

            has_owner = incomplete_array_has_semantic_owner(program, index);
            element_valid = type_is_valid(program, target, array_type->element_type);
            element_function = minic_type_is_function(array_type->element_type);
            if ((array_type->element_count == 0U && !has_owner) || !element_valid ||
                element_function) {
                (void)fprintf(stderr,
                              \"VERIFY_ARRAY_FAIL index=%zu count=%zu zero_length=%d owner=%d \"
                              \"element_valid=%d element_function=%d base_kind=%d pointer_depth=%u \"
                              \"array_id=%zu function_id=%zu record_id=%zu enum_id=%zu\\n\",
                              index,
                              array_type->element_count,
                              array_type->is_zero_length ? 1 : 0,
                              has_owner ? 1 : 0,
                              element_valid ? 1 : 0,
                              element_function ? 1 : 0,
                              (int)array_type->element_type.base_kind,
                              array_type->element_type.pointer_depth,
                              array_type->element_type.array_type_id,
                              array_type->element_type.function_type_id,
                              array_type->element_type.record_id,
                              array_type->element_type.enum_id);
                return false;
            }
        }
"""

count = text.count(old)
if count != 1:
    raise SystemExit(f"array verifier failure anchor count={count}")
path.write_text(text.replace(old, new, 1))
