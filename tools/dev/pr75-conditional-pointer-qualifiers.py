#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    target.write_text(text.replace(old, new, 1))


def replace_region(path: str, start_marker: str, end_marker: str, replacement: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    start = text.find(start_marker)
    end = text.find(end_marker, start + len(start_marker)) if start >= 0 else -1
    if start < 0 or end < 0:
        raise SystemExit(f"{label}: cannot locate replacement region")
    target.write_text(text[:start] + replacement + text[end:])


replace_once(
    "src/frontend/type.h",
    "bool minic_type_assignment_compatible(MinicType target, MinicType source);\n",
    "bool minic_type_assignment_compatible(MinicType target, MinicType source);\n"
    "bool minic_type_conditional_pointer_common(MinicType left, MinicType right, MinicType *result);\n",
    "conditional pointer common declaration",
)

replace_once(
    "src/frontend/type.c",
    '''bool minic_type_pointer_equality_compatible(MinicType left, MinicType right) {\n''',
    r'''bool minic_type_conditional_pointer_common(MinicType left, MinicType right, MinicType *result) {
    MinicType left_pointer;
    MinicType right_pointer;
    MinicType left_pointee;
    MinicType right_pointee;
    MinicType left_unqualified;
    MinicType right_unqualified;
    MinicType composite_pointee;
    bool merge_const;
    bool merge_volatile;

    if (result == NULL || !minic_type_is_pointer(left) || !minic_type_is_pointer(right) ||
        !minic_type_unqualified(left, &left_pointer) ||
        !minic_type_unqualified(right, &right_pointer) ||
        !minic_type_pointee(left_pointer, &left_pointee) ||
        !minic_type_pointee(right_pointer, &right_pointee) ||
        !minic_type_unqualified(left_pointee, &left_unqualified) ||
        !minic_type_unqualified(right_pointee, &right_unqualified)) {
        return false;
    }

    merge_const = minic_type_is_const(left_pointee) || minic_type_is_const(right_pointee);
    merge_volatile = minic_type_is_volatile(left_pointee) || minic_type_is_volatile(right_pointee);

    if (minic_type_equal(left_unqualified, right_unqualified)) {
        composite_pointee = left_unqualified;
    } else if (minic_type_is_void(left_unqualified) &&
               !minic_type_is_function(right_unqualified)) {
        composite_pointee = minic_type_void();
    } else if (minic_type_is_void(right_unqualified) &&
               !minic_type_is_function(left_unqualified)) {
        composite_pointee = minic_type_void();
    } else {
        return false;
    }

    if (merge_const && !minic_type_add_const(composite_pointee, &composite_pointee)) {
        return false;
    }
    if (merge_volatile && !minic_type_add_volatile(composite_pointee, &composite_pointee)) {
        return false;
    }
    return minic_type_pointer_to(composite_pointee, result);
}

bool minic_type_pointer_equality_compatible(MinicType left, MinicType right) {
''',
    "conditional pointer common implementation",
)

conditional_fn = r'''static bool conditional_result_type(MinicType when_true, MinicType when_false, MinicType *result) {
    bool has_double_operand;
    bool has_numeric_operands;

    if (result == NULL) {
        return false;
    }
    if (minic_type_equal(when_true, when_false)) {
        *result = when_true;
        return true;
    }
    if (minic_type_conditional_pointer_common(when_true, when_false, result)) {
        return true;
    }
    if (minic_type_is_integer(when_true) && minic_type_is_integer(when_false)) {
        return minic_type_integer_common(when_true, when_false, result);
    }
    has_double_operand = minic_type_is_double(when_true) || minic_type_is_double(when_false);
    has_numeric_operands = (minic_type_is_double(when_true) || minic_type_is_integer(when_true)) &&
                           (minic_type_is_double(when_false) || minic_type_is_integer(when_false));
    if (has_double_operand && has_numeric_operands) {
        *result = minic_type_double();
        return true;
    }
    return false;
}

'''

replace_region(
    "src/frontend/parser_expression.c",
    "static bool conditional_result_type(MinicType when_true, MinicType when_false, MinicType *result) {\n",
    "static bool binary_result_type(",
    conditional_fn,
    "parser conditional result type",
)

replace_region(
    "src/frontend/ast_verifier.c",
    "static bool conditional_result_type(MinicType when_true, MinicType when_false, MinicType *result) {\n",
    "static bool is_normalized_integer_cast_add(",
    conditional_fn,
    "verifier conditional result type",
)

print("staged conditional pointer composite types with const/volatile qualifier union")
