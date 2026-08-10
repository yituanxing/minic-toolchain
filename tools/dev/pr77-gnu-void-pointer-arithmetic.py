#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:120]!r}")
    target.write_text(text.replace(old, new, 1))


# GNU C defines arithmetic on void* and function pointers with byte stride 1.
# Keep ordinary incomplete object pointers rejected. LanguageOptions will own
# this GNU-vs-ISO policy after discovery semantics are materialized.
path = Path("src/frontend/parser_expression.c")
text = path.read_text()
anchor = '''static bool pointer_arithmetic_shape(MinicTokenKind kind,
                                     MinicType left,
                                     MinicType right,
                                     MinicType *pointer_type) {
'''
helper = r'''static bool pointer_arithmetic_pointee_allowed(const MinicC0Program *program,
                                                MinicType pointee_type) {
    return minic_type_is_void(pointee_type) || minic_type_is_function(pointee_type) ||
           type_is_complete_object(program, pointee_type);
}

'''
if text.count(anchor) != 1:
    raise SystemExit(f"pointer arithmetic helper anchor: expected one match, found {text.count(anchor)}")
text = text.replace(anchor, helper + anchor, 1)
old = '''    if (!pointer_arithmetic_shape(kind, left, right, &pointer_type) ||
        !minic_type_pointee(pointer_type, &pointee_type) ||
        !type_is_complete_object(program, pointee_type)) {
        return false;
    }
'''
new = '''    if (!pointer_arithmetic_shape(kind, left, right, &pointer_type) ||
        !minic_type_pointee(pointer_type, &pointee_type) ||
        !pointer_arithmetic_pointee_allowed(program, pointee_type)) {
        return false;
    }
'''
if text.count(old) != 1:
    raise SystemExit(f"binary pointer validation anchor: expected one match, found {text.count(old)}")
text = text.replace(old, new, 1)
old = '''            } else if (has_pointer_arithmetic_shape &&
                       minic_type_pointee(pointer_type, &pointee_type) &&
                       !type_is_complete_object(parser->program, pointee_type)) {
                minic_parser_error(parser, "pointer arithmetic requires a complete object type");
'''
new = '''            } else if (has_pointer_arithmetic_shape &&
                       minic_type_pointee(pointer_type, &pointee_type) &&
                       !pointer_arithmetic_pointee_allowed(parser->program, pointee_type)) {
                minic_parser_error(parser,
                                   "pointer arithmetic requires a complete object type or GNU byte-sized void/function pointee");
'''
if text.count(old) != 1:
    raise SystemExit(f"pointer diagnostic anchor: expected one match, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))

# The target-side element-size query is the single lowering choke point used by
# binary pointer +/- and pointer difference. Teach it the same GNU byte-stride
# rule so the frontend acceptance cannot diverge from generated code.
path = Path("src/target/riscv64/codegen_expression.c")
text = path.read_text()
old = '''    return element_size != NULL && minic_type_pointee(pointer_type, &pointee) &&
           minic_riscv64_type_layout(program, pointee, element_size, &element_alignment);
'''
new = '''    if (element_size == NULL || !minic_type_pointee(pointer_type, &pointee)) {
        return false;
    }
    if (minic_type_is_void(pointee) || minic_type_is_function(pointee)) {
        *element_size = 1U;
        return true;
    }
    return minic_riscv64_type_layout(program, pointee, element_size, &element_alignment);
'''
if text.count(old) != 1:
    raise SystemExit(f"RV64 pointer element-size anchor: expected one match, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))

print("staged GNU void/function pointer arithmetic with byte stride 1")
