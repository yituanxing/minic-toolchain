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
old_count = text.count(old)
canonical_count = text.count(new)
if old_count == 0 and canonical_count == 1:
    print("MINIC_INLINE_SPECIALIZATION_LOCAL_FACTS_V0=ALREADY_CANONICAL")
    raise SystemExit(0)
# The tested stable-parameter stage extends (rather than deletes) this
# historical parameter ingress patch. It is a valid canonical successor.
if old_count == 0 and canonical_count == 0:
    required_successor = (
        "context->source_function->specialization_integer_known[parameter_index]",
        "core_local_constant_set(context, local_id, &specialized_value);",
        "fact->specialization_value = fact->value;",
        "fact->specialization_known = true;",
        "fact->specialization_modified = false;",
        "INLINE_SPEC_PARAMETER_FACT function=%s parameter=%zu local=%zu bits=%",
    )
    expected_counts = (2, 1, 1, 1, 1, 1)
    if all(text.count(token) == count
           for token, count in zip(required_successor, expected_counts)):
        print("MINIC_INLINE_SPECIALIZATION_LOCAL_FACTS_V0=ALREADY_CANONICAL successor=stable-parameter")
        raise SystemExit(0)
if old_count != 1 or canonical_count != 0:
    raise SystemExit(f"MINIC_INLINE_SPECIALIZATION_LOCAL_FACTS_V0=ERROR partial canonical old={old_count} new={canonical_count}")
p.write_text(text.replace(old, new, 1))
print("MINIC_INLINE_SPECIALIZATION_LOCAL_FACTS_V0=APPLIED")
