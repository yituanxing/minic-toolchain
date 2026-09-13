#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower.c")
text = p.read_text()

old = '''            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &instruction, &parameter_value)) {
                return MINIC_CORE_LOWER_ERROR;
            }

            if (!minic_type_pointer_to(parameter->type, &pointer_type)) {
'''
new = '''            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &instruction, &parameter_value)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (context->source_function->is_integer_specialization &&
                context->source_function->specialization_integer_known[parameter_index] &&
                minic_type_is_integer(parameter_value_type)) {
                MinicConstValue specialized_value;
                specialized_value.type = parameter_value_type;
                specialized_value.bits =
                    context->source_function->specialization_integer_bits[parameter_index];
                core_local_constant_set(context, local_id, &specialized_value);
                if (getenv("MINIC_LOCAL_FACT_TRACE") != NULL) {
                    (void)fprintf(stderr,
                                  "INLINE_SPEC_PARAMETER_FACT function=%s parameter=%zu local=%zu bits=%" PRIu64 "\\n",
                                  context->source_function->name != NULL
                                      ? context->source_function->name
                                      : "?",
                                  parameter_index,
                                  (size_t)local_id,
                                  specialized_value.bits);
                }
            }

            if (!minic_type_pointer_to(parameter->type, &pointer_type)) {
'''
count = text.count(old)
if count != 1:
    raise SystemExit(f"expected one parameter ingress fact anchor, found {count}")
p.write_text(text.replace(old, new, 1))
print("MINIC_INLINE_SPECIALIZATION_LOCAL_FACTS_V0=APPLIED")
