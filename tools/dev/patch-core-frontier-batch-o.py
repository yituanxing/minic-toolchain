#!/usr/bin/env python3
from pathlib import Path

path = Path("src/core/core_lower.c")
text = path.read_text()
old = '''        if (minic_type_is_integer(context->source_function->return_type)) {
            status = lower_integer_assignment_value(context,
                                                    context->source_function->return_type,
                                                    statement->expression,
                                                    &terminator.return_value);
        } else if (minic_type_is_pointer(context->source_function->return_type)) {
'''
new = '''        if (minic_type_is_integer(context->source_function->return_type)) {
            /* BATCH_O_SCALAR_RETURN_ASSIGNMENT_CONVERSION: integer return
               contexts use ordinary C assignment conversion too.  Reuse the
               scalar seam so pointer truth values can return as _Bool while
               ordinary integer returns keep the established integer path. */
            status = lower_scalar_assignment_value(context,
                                                   context->source_function->return_type,
                                                   statement->expression,
                                                   &terminator.return_value);
        } else if (minic_type_is_pointer(context->source_function->return_type)) {
'''
count = text.count(old)
if count != 1:
    raise SystemExit(f"Batch O return anchor count={count}")
path.write_text(text.replace(old, new, 1))
print("CORE_BATCH_O_PATCHED scalar return assignment conversion")
