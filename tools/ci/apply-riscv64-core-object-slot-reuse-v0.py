#!/usr/bin/env python3
from pathlib import Path

p = Path("src/target/riscv64/core_codegen.c")
text = p.read_text()

if "IRQ_FRAME_REUSE_V0" not in text:
    raise SystemExit("value-slot reuse prerequisite not applied")
if "RECORD_CALL_RESULT_SNAPSHOT_REUSE_V0" not in text:
    raise SystemExit("record snapshot reuse prerequisite not applied")
if "CORE_OBJECT_SLOT_REUSE_V0" in text:
    raise SystemExit("Core object slot reuse already applied")

frame_marker = "static bool core_frame_initialize(const MinicC0Program *program,"
if frame_marker not in text:
    raise SystemExit("core_frame_initialize marker not found")

helper = r'''/* CORE_OBJECT_SLOT_REUSE_V0: conservatively reuse stack storage for small
   scalar CoreObjects whose complete address lifetime is proven to stay inside
   one basic block.  An OBJECT_ADDRESS result must be consumed only as the
   direct address operand of LOAD/STORE; derived pointers, calls, asm, returns,
   cross-block uses, records, arrays and over-aligned objects remain permanent.
   Candidate intervals are colored in 8-byte units and different basic blocks
   share the same pool because no candidate survives a block boundary. */
typedef struct CoreScalarObjectInterval {
    bool reusable;
    size_t block_index;
    size_t first_position;
    size_t last_position;
} CoreScalarObjectInterval;

typedef struct CoreTargetValueUseCount {
    MinicCoreValueId target;
    size_t count;
} CoreTargetValueUseCount;

static bool core_count_target_value_use(MinicCoreValueId value_id, void *opaque) {
    CoreTargetValueUseCount *context = (CoreTargetValueUseCount *)opaque;
    if (context == NULL) {
        return false;
    }
    if (value_id == context->target) {
        if (context->count == SIZE_MAX) {
            return false;
        }
        ++context->count;
    }
    return true;
}

static bool core_scalar_object_note_position(CoreScalarObjectInterval *interval,
                                             size_t block_index,
                                             size_t position) {
    if (interval == NULL) {
        return false;
    }
    if (!interval->reusable) {
        interval->reusable = true;
        interval->block_index = block_index;
        interval->first_position = position;
        interval->last_position = position;
        return true;
    }
    if (interval->block_index != block_index) {
        return false;
    }
    if (position < interval->first_position) {
        interval->first_position = position;
    }
    if (position > interval->last_position) {
        interval->last_position = position;
    }
    return true;
}

static bool core_scalar_object_interval(const MinicC0Program *program,
                                        const MinicCoreFunction *function,
                                        MinicCoreObjectId object_id,
                                        CoreScalarObjectInterval *interval) {
    size_t object_size;
    size_t object_alignment;
    size_t block_index;

    if (program == NULL || function == NULL || interval == NULL ||
        object_id >= function->object_count) {
        return false;
    }
    interval->reusable = false;
    interval->block_index = SIZE_MAX;
    interval->first_position = SIZE_MAX;
    interval->last_position = 0U;

    if (!core_scalar_type(function->objects[object_id].type) ||
        function->objects[object_id].element_count != 1U ||
        !minic_data_layout_type(minic_default_data_layout(),
                                program,
                                function->objects[object_id].type,
                                &object_size,
                                &object_alignment) ||
        object_size == 0U || object_size > 8U || object_alignment == 0U ||
        object_alignment > 8U ||
        function->objects[object_id].explicit_alignment > 8U) {
        return true;
    }

    /* Aggregate-only direct object uses must never be folded into the scalar
       reuse pool even if malformed Core reaches this backend. */
    for (block_index = 0U; block_index < function->block_count; ++block_index) {
        const MinicCoreBlock *block = &function->blocks[block_index];
        size_t position;

        if (block->has_terminator &&
            block->terminator.kind == MINIC_CORE_TERMINATOR_RETURN &&
            block->terminator.return_object == object_id) {
            interval->reusable = false;
            return true;
        }
        for (position = 0U; position < block->instruction_count; ++position) {
            MinicCoreInstructionId instruction_id = block->instructions[position];
            const MinicCoreInstruction *instruction;
            size_t argument_begin = 0U;
            size_t argument_count = 0U;
            size_t argument_index;

            if (instruction_id >= function->instruction_count) {
                return false;
            }
            instruction = &function->instructions[instruction_id];
            if ((instruction->kind == MINIC_CORE_INSTRUCTION_RECORD_LOAD &&
                 instruction->value.record_load.destination_object == object_id) ||
                (instruction->kind == MINIC_CORE_INSTRUCTION_CALL &&
                 instruction->value.call.result_object == object_id)) {
                interval->reusable = false;
                return true;
            }
            if (instruction->kind == MINIC_CORE_INSTRUCTION_CALL) {
                argument_begin = instruction->value.call.argument_begin;
                argument_count = instruction->value.call.argument_count;
            } else if (instruction->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL) {
                argument_begin = instruction->value.indirect_call.argument_begin;
                argument_count = instruction->value.indirect_call.argument_count;
            }
            if (argument_count != 0U) {
                if (argument_begin > function->call_argument_count ||
                    argument_count > function->call_argument_count - argument_begin) {
                    return false;
                }
                for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
                    const MinicCoreCallArgument *argument =
                        &function->call_arguments[argument_begin + argument_index];
                    if (argument->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT &&
                        argument->value.object_id == object_id) {
                        interval->reusable = false;
                        return true;
                    }
                }
            }
        }
    }

    for (block_index = 0U; block_index < function->block_count; ++block_index) {
        const MinicCoreBlock *block = &function->blocks[block_index];
        size_t position;

        for (position = 0U; position < block->instruction_count; ++position) {
            MinicCoreInstructionId instruction_id = block->instructions[position];
            const MinicCoreInstruction *instruction;

            if (instruction_id >= function->instruction_count) {
                return false;
            }
            instruction = &function->instructions[instruction_id];
            if (instruction->kind == MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT &&
                instruction->value.parameter_object.object_id == object_id) {
                if (!core_scalar_object_note_position(interval, block_index, position)) {
                    interval->reusable = false;
                    return true;
                }
            }
            if (instruction->kind == MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS &&
                instruction->value.object_id == object_id) {
                MinicCoreValueId address_value = instruction->result;
                CoreTargetValueUseCount total_uses;
                size_t allowed_uses = 0U;
                size_t use_block;

                if (address_value == MINIC_CORE_VALUE_INVALID ||
                    address_value >= function->value_count ||
                    !core_scalar_object_note_position(interval, block_index, position)) {
                    interval->reusable = false;
                    return true;
                }
                total_uses.target = address_value;
                total_uses.count = 0U;
                for (use_block = 0U; use_block < function->block_count; ++use_block) {
                    const MinicCoreBlock *candidate_block = &function->blocks[use_block];
                    size_t use_position;
                    for (use_position = 0U;
                         use_position < candidate_block->instruction_count;
                         ++use_position) {
                        MinicCoreInstructionId use_id = candidate_block->instructions[use_position];
                        const MinicCoreInstruction *use_instruction;
                        size_t direct_count = 0U;

                        if (use_id >= function->instruction_count) {
                            return false;
                        }
                        use_instruction = &function->instructions[use_id];
                        if (!core_visit_instruction_value_uses(function,
                                                               use_instruction,
                                                               core_count_target_value_use,
                                                               &total_uses)) {
                            return false;
                        }
                        if (use_instruction->kind == MINIC_CORE_INSTRUCTION_LOAD &&
                            use_instruction->value.load.address == address_value) {
                            ++direct_count;
                        }
                        if (use_instruction->kind == MINIC_CORE_INSTRUCTION_STORE &&
                            use_instruction->value.store.address == address_value) {
                            ++direct_count;
                        }
                        if (direct_count != 0U) {
                            if (use_block != block_index ||
                                allowed_uses > SIZE_MAX - direct_count ||
                                !core_scalar_object_note_position(
                                    interval, use_block, use_position)) {
                                interval->reusable = false;
                                return true;
                            }
                            allowed_uses += direct_count;
                        }
                    }
                    if (candidate_block->has_terminator &&
                        !core_visit_terminator_value_uses(&candidate_block->terminator,
                                                         core_count_target_value_use,
                                                         &total_uses)) {
                        return false;
                    }
                }
                if (total_uses.count != allowed_uses) {
                    interval->reusable = false;
                    return true;
                }
            }
        }
    }
    return true;
}

static bool core_scalar_object_intervals_overlap(const CoreScalarObjectInterval *left,
                                                 const CoreScalarObjectInterval *right) {
    return left != NULL && right != NULL && left->reusable && right->reusable &&
           left->block_index == right->block_index &&
           !(left->last_position < right->first_position ||
             right->last_position < left->first_position);
}

typedef struct CoreScalarObjectReuseCache {
    const MinicC0Program *program;
    const MinicCoreFunction *function;
    CoreScalarObjectInterval *intervals;
    size_t *slots;
    size_t peak_slots;
} CoreScalarObjectReuseCache;

static CoreScalarObjectReuseCache core_scalar_object_reuse_cache;

static void core_scalar_object_reuse_cache_clear(void) {
    free(core_scalar_object_reuse_cache.slots);
    free(core_scalar_object_reuse_cache.intervals);
    core_scalar_object_reuse_cache.program = NULL;
    core_scalar_object_reuse_cache.function = NULL;
    core_scalar_object_reuse_cache.intervals = NULL;
    core_scalar_object_reuse_cache.slots = NULL;
    core_scalar_object_reuse_cache.peak_slots = 0U;
}

static bool core_scalar_object_reuse_layout(const MinicC0Program *program,
                                            const MinicCoreFunction *function,
                                            MinicCoreObjectId target,
                                            bool *target_reusable,
                                            size_t *target_slot,
                                            size_t *peak_slots) {
    size_t object_index;

    if (program == NULL || function == NULL || peak_slots == NULL ||
        (target != MINIC_CORE_OBJECT_INVALID && target >= function->object_count)) {
        return false;
    }

    if (core_scalar_object_reuse_cache.program != program ||
        core_scalar_object_reuse_cache.function != function) {
        size_t peak = 0U;

        core_scalar_object_reuse_cache_clear();
        core_scalar_object_reuse_cache.program = program;
        core_scalar_object_reuse_cache.function = function;

        if (getenv("MINIC_BOOTSTRAP_TRACE") != NULL) {
            (void)fprintf(stderr,
                          "MINIC_BOOTSTRAP_TRACE stage=core-object-reuse-cache state=begin "
                          "objects=%zu instructions=%zu values=%zu\n",
                          function->object_count,
                          function->instruction_count,
                          function->value_count);
            (void)fflush(stderr);
        }

        if (function->object_count != 0U) {
            if (function->object_count > SIZE_MAX / sizeof(*core_scalar_object_reuse_cache.intervals) ||
                function->object_count > SIZE_MAX / sizeof(*core_scalar_object_reuse_cache.slots)) {
                core_scalar_object_reuse_cache_clear();
                return false;
            }
            core_scalar_object_reuse_cache.intervals =
                (CoreScalarObjectInterval *)malloc(
                    function->object_count * sizeof(*core_scalar_object_reuse_cache.intervals));
            core_scalar_object_reuse_cache.slots =
                (size_t *)malloc(function->object_count *
                                 sizeof(*core_scalar_object_reuse_cache.slots));
            if (core_scalar_object_reuse_cache.intervals == NULL ||
                core_scalar_object_reuse_cache.slots == NULL) {
                core_scalar_object_reuse_cache_clear();
                return false;
            }

            for (object_index = 0U; object_index < function->object_count; ++object_index) {
                core_scalar_object_reuse_cache.slots[object_index] = SIZE_MAX;
                if (object_index > UINT32_MAX ||
                    !core_scalar_object_interval(
                        program,
                        function,
                        (MinicCoreObjectId)object_index,
                        &core_scalar_object_reuse_cache.intervals[object_index])) {
                    core_scalar_object_reuse_cache_clear();
                    return false;
                }
            }

            for (object_index = 0U; object_index < function->object_count; ++object_index) {
                size_t slot;
                if (!core_scalar_object_reuse_cache.intervals[object_index].reusable) {
                    continue;
                }
                for (slot = 0U;; ++slot) {
                    size_t prior;
                    bool occupied = false;
                    for (prior = 0U; prior < object_index; ++prior) {
                        if (core_scalar_object_reuse_cache.slots[prior] == slot &&
                            core_scalar_object_intervals_overlap(
                                &core_scalar_object_reuse_cache.intervals[object_index],
                                &core_scalar_object_reuse_cache.intervals[prior])) {
                            occupied = true;
                            break;
                        }
                    }
                    if (!occupied) {
                        core_scalar_object_reuse_cache.slots[object_index] = slot;
                        if (slot == SIZE_MAX) {
                            core_scalar_object_reuse_cache_clear();
                            return false;
                        }
                        if (slot + 1U > peak) {
                            peak = slot + 1U;
                        }
                        break;
                    }
                }
            }
        }

        core_scalar_object_reuse_cache.peak_slots = peak;
        if (getenv("MINIC_BOOTSTRAP_TRACE") != NULL) {
            (void)fprintf(stderr,
                          "MINIC_BOOTSTRAP_TRACE stage=core-object-reuse-cache state=end "
                          "objects=%zu peak_slots=%zu\n",
                          function->object_count,
                          peak);
            (void)fflush(stderr);
        }
    }

    *peak_slots = core_scalar_object_reuse_cache.peak_slots;
    if (target != MINIC_CORE_OBJECT_INVALID) {
        if (target_reusable != NULL) {
            *target_reusable =
                core_scalar_object_reuse_cache.intervals != NULL &&
                core_scalar_object_reuse_cache.intervals[target].reusable;
        }
        if (target_slot != NULL) {
            *target_slot = core_scalar_object_reuse_cache.slots != NULL
                               ? core_scalar_object_reuse_cache.slots[target]
                               : SIZE_MAX;
        }
    } else {
        if (target_reusable != NULL) {
            *target_reusable = false;
        }
        if (target_slot != NULL) {
            *target_slot = SIZE_MAX;
        }
    }
    return true;
}
'''
text = text.replace(frame_marker, helper + frame_marker, 1)

decl_anchor = r'''    size_t outgoing_argument_size;
    size_t maximum_object_alignment;'''
decl_new = r'''    size_t outgoing_argument_size;
    size_t maximum_object_alignment;
    size_t scalar_reuse_pool_slots;'''
if decl_anchor not in text:
    raise SystemExit("frame declaration anchor not found")
text = text.replace(decl_anchor, decl_new, 1)

init_anchor = r'''    storage_size = outgoing_argument_size;
    maximum_object_alignment = 16U;'''
init_new = r'''    storage_size = outgoing_argument_size;
    maximum_object_alignment = 16U;
    if (!core_scalar_object_reuse_layout(program,
                                         function,
                                         MINIC_CORE_OBJECT_INVALID,
                                         NULL,
                                         NULL,
                                         &scalar_reuse_pool_slots)) {
        return false;
    }'''
if init_anchor not in text:
    raise SystemExit("object layout initialization anchor not found")
text = text.replace(init_anchor, init_new, 1)

size_anchor = r'''        object_size *= function->objects[object_index].element_count;
        {
            MinicCoreObjectId reuse_candidate;
            if (object_index <= UINT32_MAX &&
                core_record_call_result_snapshot_reuse_candidate(
                    function, (MinicCoreObjectId)object_index, &reuse_candidate)) {
                continue;
            }
        }'''
size_new = r'''        object_size *= function->objects[object_index].element_count;
        if (object_index <= UINT32_MAX) {
            bool scalar_reusable;
            size_t scalar_slot;
            size_t scalar_peak;
            if (!core_scalar_object_reuse_layout(program,
                                                 function,
                                                 (MinicCoreObjectId)object_index,
                                                 &scalar_reusable,
                                                 &scalar_slot,
                                                 &scalar_peak)) {
                return false;
            }
            if (scalar_peak != scalar_reuse_pool_slots) {
                return false;
            }
            if (scalar_reusable) {
                if (scalar_slot >= scalar_reuse_pool_slots) {
                    return false;
                }
                continue;
            }
        }
        {
            MinicCoreObjectId reuse_candidate;
            if (object_index <= UINT32_MAX &&
                core_record_call_result_snapshot_reuse_candidate(
                    function, (MinicCoreObjectId)object_index, &reuse_candidate)) {
                continue;
            }
        }'''
if size_anchor not in text:
    raise SystemExit("object sizing anchor after record reuse not found")
text = text.replace(size_anchor, size_new, 1)

value_base_anchor = r'''    if (!align_up(storage_size, 8U, &frame->value_base_offset)) {
        return false;
    }'''
value_base_new = r'''    if (!align_up(storage_size, 8U, &storage_size) ||
        scalar_reuse_pool_slots > (SIZE_MAX - storage_size) / 8U) {
        return false;
    }
    storage_size += scalar_reuse_pool_slots * 8U;
    if (!align_up(storage_size, 8U, &frame->value_base_offset)) {
        return false;
    }'''
if value_base_anchor not in text:
    raise SystemExit("value base anchor not found")
text = text.replace(value_base_anchor, value_base_new, 1)

assign_anchor = r'''            if (function->objects[cached_index].explicit_alignment != 0U &&
                function->objects[cached_index].explicit_alignment > object_alignment) {
                object_alignment = function->objects[cached_index].explicit_alignment;
            }
            {
                MinicCoreObjectId reuse_candidate;'''
assign_new = r'''            if (function->objects[cached_index].explicit_alignment != 0U &&
                function->objects[cached_index].explicit_alignment > object_alignment) {
                object_alignment = function->objects[cached_index].explicit_alignment;
            }
            if (cached_index <= UINT32_MAX) {
                bool scalar_reusable;
                size_t scalar_slot;
                size_t scalar_peak;
                if (!core_scalar_object_reuse_layout(program,
                                                     function,
                                                     (MinicCoreObjectId)cached_index,
                                                     &scalar_reusable,
                                                     &scalar_slot,
                                                     &scalar_peak) ||
                    scalar_peak != scalar_reuse_pool_slots) {
                    free(frame->object_offsets);
                    frame->object_offsets = NULL;
                    return false;
                }
                if (scalar_reusable) {
                    size_t pool_bytes;
                    size_t pool_base;
                    if (scalar_slot >= scalar_reuse_pool_slots ||
                        scalar_reuse_pool_slots > frame->value_base_offset / 8U) {
                        free(frame->object_offsets);
                        frame->object_offsets = NULL;
                        return false;
                    }
                    pool_bytes = scalar_reuse_pool_slots * 8U;
                    if (pool_bytes > frame->value_base_offset) {
                        free(frame->object_offsets);
                        frame->object_offsets = NULL;
                        return false;
                    }
                    pool_base = frame->value_base_offset - pool_bytes;
                    frame->object_offsets[cached_index] = pool_base + scalar_slot * 8U;
                    continue;
                }
            }
            {
                MinicCoreObjectId reuse_candidate;'''
if assign_anchor not in text:
    raise SystemExit("object assignment anchor after record reuse not found")
text = text.replace(assign_anchor, assign_new, 1)

p.write_text(text)
print("MINIC_RISCV64_CORE_OBJECT_SLOT_REUSE_V0=APPLIED")
