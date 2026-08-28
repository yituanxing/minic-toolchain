#!/usr/bin/env python3
from pathlib import Path

path = Path("src/core/core_lower.c")
text = path.read_text()
old = '''    /* M83B_CALL_STATEMENT_DISPATCH: CALL ownership lives in lower_expression().
       Statement context only discards the produced value; it must not force an
       indirect call back through the legacy direct-call helper. */
    if (expression->kind == MINIC_EXPRESSION_CALL) {
        MinicCoreValueId discarded_value;

        return lower_expression(context, statement->expression, &discarded_value);
    }
'''
new = '''    /* BATCH_Q_DISCARDED_RECORD_CALL: a direct call returning an aggregate still
       executes when its value is discarded by an expression statement. Core
       already models the returned aggregate as an address-backed result object;
       statement context simply does not consume that object. Keep indirect
       record returns fail-closed until their ABI/object result seam exists. */
    if (expression->kind == MINIC_EXPRESSION_CALL) {
        if (minic_type_is_record(expression->type) &&
            expression->value.call.function_id != MINIC_FUNCTION_INVALID) {
            MinicCoreObjectId discarded_object;

            return lower_direct_record_call_object(context, expression, &discarded_object);
        }
        {
            MinicCoreValueId discarded_value;

            return lower_expression(context, statement->expression, &discarded_value);
        }
    }
'''
if old not in text:
    if "BATCH_Q_DISCARDED_RECORD_CALL" in text:
        print("CORE_BATCH_Q_ALREADY_PATCHED")
        raise SystemExit(0)
    raise SystemExit("Batch Q anchor not found")
text = text.replace(old, new, 1)
path.write_text(text)
print("CORE_BATCH_Q_PATCHED discarded direct record-call result")
