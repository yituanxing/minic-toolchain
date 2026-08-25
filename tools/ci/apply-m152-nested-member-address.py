#!/usr/bin/env python3
from pathlib import Path

marker = "M156_STRUCTURAL_POINTER_RELATIONAL_OWNER"
path = Path("src/core/core_lower.c")
text = path.read_text()

if marker in text:
    print("M156 structural pointer relational owner already staged")
    raise SystemExit(0)
if "M155_EXTERN_VOID_SYMBOL_ADDRESS_OWNER" not in text:
    raise SystemExit("M156 requires productized M155")

old = r'''            if (!minic_c0_pointer_relational_compatible(
                    context->body->program, left_type, right_type) ||
                !minic_type_conditional_pointer_common(left_type, right_type, &common_type)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
'''
new = r'''            /* M156_STRUCTURAL_POINTER_RELATIONAL_OWNER: relational
               legality is already decided by frontend/Sema, including GNU void
               pointers and structurally compatible pointer-to-array shapes.
               Core only needs one bit representation for POINTER_LESS.  The
               ordinary conditional-pointer common type remains preferred; if
               side-table identity prevents one despite accepted relational
               compatibility, use the left representation and bitcast both
               operands, matching the established equality owner. */
            if (!minic_c0_pointer_relational_compatible(
                    context->body->program, left_type, right_type)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            if (!minic_type_conditional_pointer_common(
                    left_type, right_type, &common_type)) {
                common_type = left_type;
            }
'''
if text.count(old) != 1:
    raise SystemExit("M156 could not locate pointer relational common-type gate")
text = text.replace(old, new, 1)
path.write_text(text)

Path("tests/compiler/c0/m156_structural_pointer_relational.c").write_text(r'''struct item { int value; };
extern struct item __start_items[];
extern struct item __stop_items[];

int has_items(void) {
    return &__stop_items > &__start_items;
}

extern const void __start_blob;
extern const void __stop_blob;
int has_blob(void) {
    return &__stop_blob > &__start_blob;
}
''')
print("staged M156 structural pointer relational owner")
