#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower_asm.c")
text = p.read_text()
start_marker = "static bool core_inline_asm_rewind_dead_local_assignment("
end_marker = "\n\nMinicCoreLowerStatus minic_core_lower_inline_asm("
start = text.find(start_marker)
if start < 0:
    raise SystemExit("rK rewind helper start not found")
end = text.find(end_marker, start)
if end < 0:
    raise SystemExit("rK rewind helper end not found")

helper = r'''static bool core_inline_asm_rewind_dead_local_assignment(
    MinicCoreLowerContext *context,
    MinicLocalId local_id) {
    MinicCoreBlock *block;
    size_t local_index;
    MinicCoreObjectId local_object;
    const size_t candidate_counts[2] = {8U, 7U};
    size_t candidate_index;

    if (context == NULL || context->function == NULL || context->source_function == NULL ||
        context->local_objects == NULL || context->block_id >= context->function->block_count ||
        local_id < context->source_function->local_begin) {
        return false;
    }
    local_index = local_id - context->source_function->local_begin;
    if (local_index >= context->source_function->local_count) {
        return false;
    }
    local_object = context->local_objects[local_index];
    if (local_object == MINIC_CORE_OBJECT_INVALID) {
        return false;
    }
    block = &context->function->blocks[context->block_id];
    if (block->has_terminator) {
        return false;
    }

    /* Scalar local initialization is normally materialized through a temporary
       object.  When the source integer type differs from the local type, Core
       retains one INTEGER_CONVERSION between the constant and temporary store.
       Linux's csr_swap(CSR_SATP, 0ULL) has exactly that shape: unsigned long
       __v = (unsigned long)0ULL.  Accept both the converted eight-instruction
       tail and the same-type seven-instruction tail.  This remains fail-closed:
       the candidate must be both the block tail and exact global instruction /
       value tail before the transaction is rolled back. */
    for (candidate_index = 0U; candidate_index < 2U; ++candidate_index) {
        size_t remove_count = candidate_counts[candidate_index];
        MinicCoreInstructionId ids[8];
        size_t offset = remove_count == 8U ? 1U : 0U;
        const MinicCoreInstruction *constant;
        const MinicCoreInstruction *conversion = NULL;
        const MinicCoreInstruction *temp_address_0;
        const MinicCoreInstruction *temp_store;
        const MinicCoreInstruction *local_address;
        const MinicCoreInstruction *temp_address_1;
        const MinicCoreInstruction *temp_load;
        const MinicCoreInstruction *local_store;
        MinicCoreValueId materialized_value;
        size_t index;
        size_t global_begin;
        size_t produced_values = 0U;
        size_t expected_value;
        bool shape_ok = true;

        if (block->instruction_count < remove_count ||
            context->function->instruction_count < remove_count) {
            continue;
        }
        for (index = 0U; index < remove_count; ++index) {
            ids[index] = block->instructions[block->instruction_count - remove_count + index];
            if (ids[index] >= context->function->instruction_count) {
                shape_ok = false;
                break;
            }
        }
        if (!shape_ok) {
            continue;
        }
        constant = &context->function->instructions[ids[0]];
        if (constant->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT ||
            constant->result == MINIC_CORE_VALUE_INVALID) {
            continue;
        }
        materialized_value = constant->result;
        if (remove_count == 8U) {
            conversion = &context->function->instructions[ids[1]];
            if (conversion->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION ||
                conversion->result == MINIC_CORE_VALUE_INVALID ||
                conversion->value.operand != constant->result) {
                continue;
            }
            materialized_value = conversion->result;
        }
        temp_address_0 = &context->function->instructions[ids[1U + offset]];
        temp_store = &context->function->instructions[ids[2U + offset]];
        local_address = &context->function->instructions[ids[3U + offset]];
        temp_address_1 = &context->function->instructions[ids[4U + offset]];
        temp_load = &context->function->instructions[ids[5U + offset]];
        local_store = &context->function->instructions[ids[6U + offset]];

        if (temp_address_0->kind != MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS ||
            temp_address_0->result == MINIC_CORE_VALUE_INVALID ||
            temp_address_0->value.object_id == local_object ||
            temp_store->kind != MINIC_CORE_INSTRUCTION_STORE ||
            temp_store->value.store.is_volatile ||
            temp_store->value.store.address != temp_address_0->result ||
            temp_store->value.store.stored_value != materialized_value ||
            local_address->kind != MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS ||
            local_address->result == MINIC_CORE_VALUE_INVALID ||
            local_address->value.object_id != local_object ||
            temp_address_1->kind != MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS ||
            temp_address_1->result == MINIC_CORE_VALUE_INVALID ||
            temp_address_1->value.object_id != temp_address_0->value.object_id ||
            temp_load->kind != MINIC_CORE_INSTRUCTION_LOAD ||
            temp_load->result == MINIC_CORE_VALUE_INVALID ||
            temp_load->value.load.is_volatile ||
            temp_load->value.load.address != temp_address_1->result ||
            local_store->kind != MINIC_CORE_INSTRUCTION_STORE ||
            local_store->value.store.is_volatile ||
            local_store->value.store.address != local_address->result ||
            local_store->value.store.stored_value != temp_load->result) {
            continue;
        }
        for (index = 1U; index < remove_count; ++index) {
            const MinicCoreInstruction *candidate = &context->function->instructions[ids[index]];
            if (candidate->span.begin.line != constant->span.begin.line) {
                shape_ok = false;
                break;
            }
        }
        if (!shape_ok) {
            continue;
        }

        global_begin = context->function->instruction_count - remove_count;
        for (index = 0U; index < remove_count; ++index) {
            const MinicCoreInstruction *candidate;
            if ((size_t)ids[index] != global_begin + index) {
                shape_ok = false;
                break;
            }
            candidate = &context->function->instructions[ids[index]];
            if (candidate->result != MINIC_CORE_VALUE_INVALID) {
                produced_values += 1U;
            }
        }
        if (!shape_ok || produced_values > context->function->value_count) {
            continue;
        }
        expected_value = context->function->value_count - produced_values;
        for (index = 0U; index < remove_count; ++index) {
            const MinicCoreInstruction *candidate = &context->function->instructions[ids[index]];
            if (candidate->result != MINIC_CORE_VALUE_INVALID) {
                if ((size_t)candidate->result != expected_value ||
                    context->function->values[candidate->result].definition != ids[index]) {
                    shape_ok = false;
                    break;
                }
                expected_value += 1U;
            }
        }
        if (!shape_ok || expected_value != context->function->value_count) {
            continue;
        }

        block->instruction_count -= remove_count;
        context->function->instruction_count -= remove_count;
        context->function->value_count -= produced_values;
        if (getenv("MINIC_LOCAL_FACT_TRACE") != NULL) {
            (void)fprintf(stderr,
                          "RK_EARLY_GLOBAL_ROLLBACK function=%s removed_insts=%zu removed_values=%zu converted=%d\n",
                          context->source_function->name != NULL ? context->source_function->name : "?",
                          remove_count,
                          produced_values,
                          conversion != NULL ? 1 : 0);
            (void)fprintf(stderr,
                          "RK_EARLY_REWIND function=%s local=%zu object=%u removed=%zu\n",
                          context->source_function->name != NULL ? context->source_function->name : "?",
                          (size_t)local_id,
                          (unsigned int)local_object,
                          remove_count);
        }
        return true;
    }
    return false;
}'''

text = text[:start] + helper + text[end:]
p.write_text(text)
print("MINIC_INLINE_ASM_RK_CAST_TAIL_V0=APPLIED")
