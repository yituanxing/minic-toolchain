#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower_asm.c")
text = p.read_text()

old_add = r'''                    if (!added) {
                        return MINIC_CORE_LOWER_ERROR;
                    }
'''
new_add = r'''                    if (!added) {
                        if (getenv("MINIC_ASM_BATCH_TRACE") != NULL) {
                            (void)fprintf(stderr,
                                          "ASM_OUTPUT_STAGE function=%s stage=add-opaque status=ERROR\n",
                                          context->source_function != NULL &&
                                                  context->source_function->name != NULL
                                              ? context->source_function->name
                                              : "?");
                        }
                        return MINIC_CORE_LOWER_ERROR;
                    }
'''
# Two generated fast paths contain this exact block. Instrument both.
count_add = text.count(old_add)
if count_add < 1:
    raise SystemExit(f"expected at least one add-opaque error block, found {count_add}")
text = text.replace(old_add, new_add)

old_value = r'''                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &instruction, &output_value)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                if (lower_address(context, output->expression, &address_id) !=
                    MINIC_CORE_LOWER_OK) {
                    return MINIC_CORE_LOWER_ERROR;
                }
'''
new_value = r'''                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &instruction, &output_value)) {
                    if (getenv("MINIC_ASM_BATCH_TRACE") != NULL) {
                        (void)fprintf(stderr,
                                      "ASM_OUTPUT_STAGE function=%s stage=append-output-value status=ERROR "
                                      "block=%u instructions=%zu values=%zu objects=%zu\n",
                                      context->source_function != NULL &&
                                              context->source_function->name != NULL
                                          ? context->source_function->name
                                          : "?",
                                      (unsigned int)context->block_id,
                                      context->function->instruction_count,
                                      context->function->value_count,
                                      context->function->object_count);
                    }
                    return MINIC_CORE_LOWER_ERROR;
                }
                {
                    MinicCoreLowerStatus address_status =
                        lower_address(context, output->expression, &address_id);
                    if (getenv("MINIC_ASM_BATCH_TRACE") != NULL) {
                        (void)fprintf(stderr,
                                      "ASM_OUTPUT_STAGE function=%s stage=lower-output-address status=%d "
                                      "address=%u block=%u instructions=%zu values=%zu objects=%zu\n",
                                      context->source_function != NULL &&
                                              context->source_function->name != NULL
                                          ? context->source_function->name
                                          : "?",
                                      (int)address_status,
                                      (unsigned int)address_id,
                                      (unsigned int)context->block_id,
                                      context->function->instruction_count,
                                      context->function->value_count,
                                      context->function->object_count);
                    }
                    if (address_status != MINIC_CORE_LOWER_OK) {
                        return address_status;
                    }
                }
'''
count_value = text.count(old_value)
if count_value < 1:
    raise SystemExit(f"expected at least one output-value/address block, found {count_value}")
text = text.replace(old_value, new_value)

old_store = r'''                return minic_core_function_append_effect_instruction(
                           context->function, context->block_id, &instruction)
                           ? MINIC_CORE_LOWER_OK
                           : MINIC_CORE_LOWER_ERROR;
'''
new_store = r'''                {
                    bool stored = minic_core_function_append_effect_instruction(
                        context->function, context->block_id, &instruction);
                    if (getenv("MINIC_ASM_BATCH_TRACE") != NULL) {
                        (void)fprintf(stderr,
                                      "ASM_OUTPUT_STAGE function=%s stage=store-output status=%s "
                                      "block=%u instructions=%zu values=%zu objects=%zu\n",
                                      context->source_function != NULL &&
                                              context->source_function->name != NULL
                                          ? context->source_function->name
                                          : "?",
                                      stored ? "OK" : "ERROR",
                                      (unsigned int)context->block_id,
                                      context->function->instruction_count,
                                      context->function->value_count,
                                      context->function->object_count);
                    }
                    return stored ? MINIC_CORE_LOWER_OK : MINIC_CORE_LOWER_ERROR;
                }
'''
count_store = text.count(old_store)
if count_store < 1:
    raise SystemExit(f"expected at least one output-store block, found {count_store}")
text = text.replace(old_store, new_store)

p.write_text(text)
print(
    f"MINIC_INLINE_ASM_WRITE_OUTPUT_TRACE_V0=APPLIED "
    f"add_blocks={count_add} value_blocks={count_value} store_blocks={count_store}"
)
