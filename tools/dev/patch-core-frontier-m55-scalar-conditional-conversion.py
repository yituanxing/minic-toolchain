#!/usr/bin/env python3
"""Stage M55: apply the conditional expression result conversion to each scalar arm.

The frontend already computes the C conditional-expression result type. Core used
to require each arm's pre-conversion value type to equal that result type, which
rejects valid cases such as `unsigned int` versus `unsigned long`. Reuse the
ordinary scalar assignment-conversion seam for each selected arm before storing
into the conditional result object.
"""

from pathlib import Path

PATH = Path("src/core/core_lower.c")
MARKER = "M55_SCALAR_CONDITIONAL_ARM_CONVERSION"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one replacement, found {count}")
    return text.replace(old, new, 1)


def main() -> int:
    text = PATH.read_text()
    if MARKER in text:
        print("M55 scalar conditional arm conversion already applied")
        return 0

    old_guard = r'''        if (!core_scalar_expression_value_type(context->body, true_expression, &true_type) ||
            !core_scalar_expression_value_type(context->body, false_expression, &false_type) ||
            !minic_type_equal(true_type, expression->type) ||
            !minic_type_equal(false_type, expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
'''
    new_guard = r'''        /* M55_SCALAR_CONDITIONAL_ARM_CONVERSION: the frontend owns the
           conditional result type. The selected arm undergoes the same scalar
           conversion as assignment to that type; its source type need not
           already be identical. */
        if (!core_memory_scalar_type(expression->type) ||
            !core_scalar_expression_value_type(context->body, true_expression, &true_type) ||
            !core_scalar_expression_value_type(context->body, false_expression, &false_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
'''
    text = replace_once(text, old_guard, new_guard, "conditional-type-guard")

    old_true = r'''        status = lower_expression(context, expression->value.conditional.when_true, &arm_value);
'''
    new_true = r'''        status = lower_scalar_assignment_value(context,
                                               expression->type,
                                               expression->value.conditional.when_true,
                                               &arm_value);
'''
    text = replace_once(text, old_true, new_true, "conditional-true-arm")

    old_false = r'''        status = lower_expression(context, expression->value.conditional.when_false, &arm_value);
'''
    new_false = r'''        status = lower_scalar_assignment_value(context,
                                               expression->type,
                                               expression->value.conditional.when_false,
                                               &arm_value);
'''
    text = replace_once(text, old_false, new_false, "conditional-false-arm")

    PATH.write_text(text)
    print("M55 scalar conditional arm conversion applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
