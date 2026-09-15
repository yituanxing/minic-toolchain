#!/usr/bin/env python3
from pathlib import Path

p = Path("src/target/riscv64/core_codegen.c")
text = p.read_text()

frame_marker = "static bool core_frame_initialize(const MinicC0Program *program,"
if frame_marker not in text:
    raise SystemExit("core_frame_initialize marker not found")

helper = r'''/* IRQ_FRAME_PACK_V0: Core scalar spill values are block-local. Keep the
   existing conservative no-liveness-reuse contract, but do not spend 16 bytes
   on every <= XLEN value merely because int128 needs a two-XLEN pair. */
static bool core_value_spill_slot_size(const MinicC0Program *program,
                                       const MinicCoreFunction *function,
                                       MinicCoreValueId value_id,
                                       size_t *slot_size) {
    size_t value_size;
    size_t value_alignment;

    if (program == NULL || function == NULL || slot_size == NULL ||
        value_id >= function->value_count ||
        !minic_data_layout_type(minic_default_data_layout(),
                                program,
                                function->values[value_id].type,
                                &value_size,
                                &value_alignment) ||
        value_size == 0U || value_size > 16U) {
        return false;
    }
    (void)value_alignment;
    *slot_size = value_size > 8U ? 16U : 8U;
    return true;
}

'''
text = text.replace(frame_marker, helper + frame_marker, 1)

first_start_marker = "    /*\n    ** Core v0 intentionally keeps SSA values block-local:"
first_end_marker = "    frame->saves_return_address = core_function_needs_saved_return_address(function);"
first_start = text.find(first_start_marker)
first_end = text.find(first_end_marker, first_start)
if first_start < 0 or first_end < 0:
    raise SystemExit("Core value storage sizing block not found")

new_sizing = r'''    /*
    ** Core values remain block-local and values from distinct blocks reuse the
    ** same spill region. Within a block, keep every result distinct for now,
    ** but pack ordinary <=64-bit values into one 8-byte XLEN slot. Only values
    ** whose data-layout size is 9..16 bytes reserve the old 16-byte pair.
    ** value_slot_count counts 8-byte physical spill units in this tier.
    */
    {
        size_t block_index;
        size_t maximum_block_bytes = 0U;

        for (block_index = 0U; block_index < function->block_count; ++block_index) {
            const MinicCoreBlock *block = &function->blocks[block_index];
            size_t local_bytes = 0U;
            size_t instruction_index;

            for (instruction_index = 0U; instruction_index < block->instruction_count;
                 ++instruction_index) {
                MinicCoreInstructionId instruction_id = block->instructions[instruction_index];
                const MinicCoreInstruction *instruction;
                size_t slot_size;

                if (instruction_id >= function->instruction_count) {
                    return false;
                }
                instruction = &function->instructions[instruction_id];
                if (instruction->result == MINIC_CORE_VALUE_INVALID) {
                    continue;
                }
                if (instruction->result >= function->value_count ||
                    !core_value_spill_slot_size(
                        program, function, instruction->result, &slot_size) ||
                    local_bytes > SIZE_MAX - slot_size) {
                    return false;
                }
                local_bytes += slot_size;
            }
            if (local_bytes > maximum_block_bytes) {
                maximum_block_bytes = local_bytes;
            }
        }
        if ((maximum_block_bytes & 7U) != 0U ||
            maximum_block_bytes > SIZE_MAX - frame->value_base_offset) {
            return false;
        }
        frame->value_slot_count = maximum_block_bytes / 8U;
        storage_size = frame->value_base_offset + maximum_block_bytes;
    }
'''
text = text[:first_start] + new_sizing + text[first_end:]

alloc_anchor = "        for (value_index = 0U; value_index < function->value_count; ++value_index) {\n            frame->value_offsets[value_index] = SIZE_MAX;\n        }\n\n"
anchor_pos = text.find(alloc_anchor)
if anchor_pos < 0:
    raise SystemExit("value offset initialization anchor not found")
assign_start_marker = "        for (block_index = 0U; block_index < function->block_count; ++block_index) {"
assign_start = text.find(assign_start_marker, anchor_pos + len(alloc_anchor))
assign_end_marker = "        for (value_index = 0U; value_index < function->value_count; ++value_index) {\n            if (frame->value_offsets[value_index] == SIZE_MAX) {"
assign_end = text.find(assign_end_marker, assign_start)
if assign_start < 0 or assign_end < 0:
    raise SystemExit("Core value offset assignment block not found")

new_assignment = r'''        for (block_index = 0U; block_index < function->block_count; ++block_index) {
            const MinicCoreBlock *block = &function->blocks[block_index];
            size_t local_bytes = 0U;
            size_t instruction_index;

            for (instruction_index = 0U; instruction_index < block->instruction_count;
                 ++instruction_index) {
                MinicCoreInstructionId instruction_id = block->instructions[instruction_index];
                const MinicCoreInstruction *instruction;
                size_t slot_size;
                size_t value_storage_size;

                if (instruction_id >= function->instruction_count) {
                    free(frame->value_offsets);
                    frame->value_offsets = NULL;
                    free(frame->object_offsets);
                    frame->object_offsets = NULL;
                    return false;
                }
                instruction = &function->instructions[instruction_id];
                if (instruction->result == MINIC_CORE_VALUE_INVALID) {
                    continue;
                }
                if (instruction->result >= function->value_count ||
                    !core_value_spill_slot_size(
                        program, function, instruction->result, &slot_size) ||
                    frame->value_slot_count > SIZE_MAX / 8U) {
                    free(frame->value_offsets);
                    frame->value_offsets = NULL;
                    free(frame->object_offsets);
                    frame->object_offsets = NULL;
                    return false;
                }
                value_storage_size = frame->value_slot_count * 8U;
                if (local_bytes > value_storage_size ||
                    slot_size > value_storage_size - local_bytes ||
                    frame->value_base_offset > SIZE_MAX - local_bytes ||
                    frame->value_offsets[instruction->result] != SIZE_MAX) {
                    free(frame->value_offsets);
                    frame->value_offsets = NULL;
                    free(frame->object_offsets);
                    frame->object_offsets = NULL;
                    return false;
                }
                frame->value_offsets[instruction->result] =
                    frame->value_base_offset + local_bytes;
                local_bytes += slot_size;
            }
        }
'''
text = text[:assign_start] + new_assignment + text[assign_end:]

return_marker = "    return true;\n}\n\nstatic void core_frame_destroy(MinicRiscv64CoreFrame *frame)"
if return_marker not in text:
    raise SystemExit("core_frame_initialize return marker not found")
trace = r'''    if (getenv("MINIC_CORE_FRAME_TRACE") != NULL) {
        (void)fprintf(stderr,
                      "CORE_FRAME function=%s frame=%zu value_bytes=%zu values=%zu objects=%zu\n",
                      function->name != NULL ? function->name : "?",
                      frame->frame_size,
                      frame->value_slot_count * 8U,
                      function->value_count,
                      function->object_count);
    }
    return true;
}

static void core_frame_destroy(MinicRiscv64CoreFrame *frame)'''
text = text.replace(return_marker, trace, 1)

p.write_text(text)
print("MINIC_RISCV64_CORE_VALUE_SLOT_PACK_V0=APPLIED")
