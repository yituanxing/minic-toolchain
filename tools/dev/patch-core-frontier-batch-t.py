#!/usr/bin/env python3
from pathlib import Path

path = Path("src/core/core_lower.c")
text = path.read_text()

old = '''    } else if (minic_type_is_pointer(left_type) && minic_type_is_pointer(right_type)) {
        if (!minic_type_pointer_equality_compatible(left_type, right_type) ||
            !minic_type_conditional_pointer_common(left_type, right_type, &comparison_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        pointer_comparison = true;
'''
new = '''    } else if (minic_type_is_pointer(left_type) && minic_type_is_pointer(right_type)) {
        /* BATCH_T_FRONTEND_OWNED_POINTER_EQUALITY: legality belongs to the
           source-language semantic layer.  In particular GNU C accepts the
           established function-pointer <-> void-pointer equality extension.
           Once frontend semantics accept the expression, Core only needs one
           common pointer representation for SCALAR_EQUAL.  Prefer the normal
           C conditional common pointer type; when the GNU extension has no C
           common type, use the left representation and bitcast both operands. */
        if (!minic_c0_pointer_equality_compatible(
                context->body->program, left_id, right_id)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        if (!minic_type_conditional_pointer_common(
                left_type, right_type, &comparison_type)) {
            comparison_type = left_type;
        }
        pointer_comparison = true;
'''

if "BATCH_T_FRONTEND_OWNED_POINTER_EQUALITY" in text:
    print("CORE_BATCH_T_ALREADY_PATCHED")
    raise SystemExit(0)
if old not in text:
    raise SystemExit("Batch T pointer-equality anchor not found")
text = text.replace(old, new, 1)
path.write_text(text)
print("CORE_BATCH_T_PATCHED frontend-owned pointer equality")
