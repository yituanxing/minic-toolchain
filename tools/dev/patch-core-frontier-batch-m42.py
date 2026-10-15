#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M42 {label}: expected one anchor, found {count}")
    p.write_text(text.replace(old, new, 1))


old = '''        } else if (minic_type_is_pointer(context->source_function->return_type)) {\n            status = lower_expression(context, statement->expression, &terminator.return_value);\n            if (status == MINIC_CORE_LOWER_OK &&\n                (terminator.return_value >= context->function->value_count ||\n                 !minic_type_equal(context->function->values[terminator.return_value].type,\n                                   context->source_function->return_type))) {\n                return MINIC_CORE_LOWER_UNSUPPORTED;\n            }\n'''
new = '''        } else if (minic_type_is_pointer(context->source_function->return_type)) {\n            MinicCoreValueId source_value;\n            MinicType source_type;\n\n            status = lower_expression(context, statement->expression, &source_value);\n            if (status != MINIC_CORE_LOWER_OK) {\n                return status;\n            }\n            if (source_value >= context->function->value_count) {\n                return MINIC_CORE_LOWER_ERROR;\n            }\n            source_type = context->function->values[source_value].type;\n            if (minic_type_equal(source_type, context->source_function->return_type)) {\n                terminator.return_value = source_value;\n            } else if (minic_type_is_pointer(source_type)) {\n                /* Return assignment performs the same pointer value conversion\n                 * as an ordinary C assignment.  In particular, adding pointee\n                 * qualification (T * -> const/volatile T *) must not require\n                 * the source expression to have the destination type already. */\n                status = append_scalar_bitcast(context,\n                                               statement->span,\n                                               context->source_function->return_type,\n                                               source_value,\n                                               &terminator.return_value);\n                if (status != MINIC_CORE_LOWER_OK) {\n                    return status;\n                }\n            } else {\n                return MINIC_CORE_LOWER_UNSUPPORTED;\n            }\n'''
replace_once("src/core/core_lower.c", old, new, "pointer return assignment conversion")
print("M42_PATCH_APPLIED")
