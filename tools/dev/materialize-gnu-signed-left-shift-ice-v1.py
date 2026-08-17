#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / "src/frontend/const_eval.c"
text = path.read_text()

old = '''        if (expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_LEFT) {
            if (minic_type_is_signed_integer(operation_type)) {
                int64_t signed_left;
                int64_t minimum;
                int64_t maximum;

                if (!value_signed(program, target, &converted_left, &signed_left) ||
                    signed_left < 0 ||
                    !signed_range(program, target, operation_type, &minimum, &maximum) ||
                    (count != 0U && signed_left > (maximum >> count))) {
                    return false;
                }
            }
            return normalize_bits(
                program, target, operation_type, left_bits << count, &value->bits);
        }
'''
new = '''        if (expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_LEFT) {
            if (minic_type_is_signed_integer(operation_type)) {
                int64_t signed_left;

                /* GNU C folds nonnegative signed left shifts in integer constant
                   expressions using target-width bits even when the result sets
                   the sign bit (for example 1 << 31 on a 32-bit int). Keep
                   negative operands and out-of-width counts rejected above, but
                   preserve the target bit pattern instead of rejecting this GNU
                   extension as signed overflow. */
                if (!value_signed(program, target, &converted_left, &signed_left) ||
                    signed_left < 0) {
                    return false;
                }
            }
            return normalize_bits(
                program, target, operation_type, left_bits << count, &value->bits);
        }
'''
if text.count(old) != 1:
    raise SystemExit(f"signed left-shift const-eval block count={text.count(old)}")
path.write_text(text.replace(old, new, 1))
