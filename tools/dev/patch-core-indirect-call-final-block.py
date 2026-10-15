#!/usr/bin/env python3
from pathlib import Path

path = Path("src/core/core_lower.c")
text = path.read_text()

old = '''    for (argument_index = 0U; argument_index < signature->parameter_count; ++argument_index) {
        status = reload_scalar_value(context,
                                     expression->span,
                                     signature->parameter_types[argument_index],
                                     argument_objects[argument_index],
                                     &arguments[argument_index].value.value_id);
        if (status != MINIC_CORE_LOWER_OK) {
            free(arguments);
            return status;
        }
    }
    status = lower_expression(context, expression->value.call.callee, &callee_value);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr,
                      "CORE_LOWER_DETAIL marker=M92_INDIRECT_CALL_HOT_DETAIL function=%s "
                      "stage=indirect-call reason=callee-lower status=%d callee_kind=%d\\n",
                      context->source_function != NULL ? context->source_function->name : "?",
                      (int)status,
                      callee_expression != NULL ? (int)callee_expression->kind : -1);
        free(arguments);
        return status;
    }
'''

new = '''    /* M112_INDIRECT_CALL_FINAL_BLOCK_ARGUMENTS: argument expressions may
       create control flow, and so may the indirect callee expression (for
       example an address-backed/statement-expression function-pointer load).
       Keep argument values in Core objects until the callee has been evaluated;
       then reload them in the final call block so the verifier sees both the
       callee SSA value and every call argument as block-local available values. */
    status = lower_expression(context, expression->value.call.callee, &callee_value);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr,
                      "CORE_LOWER_DETAIL marker=M92_INDIRECT_CALL_HOT_DETAIL function=%s "
                      "stage=indirect-call reason=callee-lower status=%d callee_kind=%d\\n",
                      context->source_function != NULL ? context->source_function->name : "?",
                      (int)status,
                      callee_expression != NULL ? (int)callee_expression->kind : -1);
        free(arguments);
        return status;
    }
    for (argument_index = 0U; argument_index < signature->parameter_count; ++argument_index) {
        status = reload_scalar_value(context,
                                     expression->span,
                                     signature->parameter_types[argument_index],
                                     argument_objects[argument_index],
                                     &arguments[argument_index].value.value_id);
        if (status != MINIC_CORE_LOWER_OK) {
            free(arguments);
            return status;
        }
    }
'''

if text.count(old) != 1:
    raise SystemExit(f"M112 anchor: expected one match, found {text.count(old)}")

path.write_text(text.replace(old, new, 1))
print("M112 indirect-call final-block argument reload staged")
