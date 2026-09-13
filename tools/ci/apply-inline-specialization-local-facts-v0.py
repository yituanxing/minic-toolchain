#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}: {old[:160]!r}")
    p.write_text(text.replace(old, new, 1))


# The specialization prototype materializes a known parameter as a Core integer
# constant, but local-fact propagation intentionally derives facts from source
# object semantics rather than arbitrary Core values.  Seed the parameter local
# explicitly after its normal ingress store so compile-time control-flow sees
# the same constant that the specialized ABI ingress materialized.
replace_once(
    "src/core/core_lower.c",
    "            instruction.value.store.address = address_id;\n"
    "            instruction.value.store.stored_value = parameter_value;\n"
    "            instruction.value.store.is_volatile = minic_type_is_volatile(parameter->type);\n"
    "            if (!minic_core_function_append_effect_instruction(\n"
    "                    context->function, context->block_id, &instruction)) {\n"
    "                return MINIC_CORE_LOWER_ERROR;\n"
    "            }\n",
    "            instruction.value.store.address = address_id;\n"
    "            instruction.value.store.stored_value = parameter_value;\n"
    "            instruction.value.store.is_volatile = minic_type_is_volatile(parameter->type);\n"
    "            if (!minic_core_function_append_effect_instruction(\n"
    "                    context->function, context->block_id, &instruction)) {\n"
    "                return MINIC_CORE_LOWER_ERROR;\n"
    "            }\n"
    "            if (context->source_function->is_integer_specialization &&\n"
    "                context->source_function->specialization_integer_known[parameter_index] &&\n"
    "                minic_type_is_integer(parameter_value_type)) {\n"
    "                MinicConstValue specialized_value;\n"
    "                specialized_value.type = parameter_value_type;\n"
    "                specialized_value.bits =\n"
    "                    context->source_function->specialization_integer_bits[parameter_index];\n"
    "                core_local_constant_set(context, local_id, &specialized_value);\n"
    "            }\n",
)

print("MINIC_INLINE_SPECIALIZATION_LOCAL_FACTS_V0=APPLIED")
