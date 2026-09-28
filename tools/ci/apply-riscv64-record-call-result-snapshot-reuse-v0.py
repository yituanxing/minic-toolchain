#!/usr/bin/env python3
from pathlib import Path

p = Path("src/target/riscv64/core_codegen.c")
text = p.read_text()

if "IRQ_FRAME_REUSE_V0" not in text:
    raise SystemExit("value-slot reuse prerequisite not applied")
if "RECORD_CALL_RESULT_SNAPSHOT_REUSE_V0" in text:
    raise SystemExit("record call result snapshot reuse already applied")

frame_marker = "static bool core_frame_initialize(const MinicC0Program *program,"
if frame_marker not in text:
    raise SystemExit("core_frame_initialize marker not found")

helper = r'''/* RECORD_CALL_RESULT_SNAPSHOT_REUSE_V0: Core by-value record arguments may
   be immutable RECORD_LOAD snapshots.  When such a snapshot is created and
   consumed exactly once by a direct call in the same basic block, the call's
   record result may reuse that snapshot's stack storage after the call returns.
   Reject address-taken, cross-block, multiply-used, and otherwise ambiguous
   objects so ordinary C locals keep their existing lifetime semantics. */
static bool core_instruction_block_position(const MinicCoreFunction *function,
                                            MinicCoreInstructionId instruction_id,
                                            size_t *block_index,
                                            size_t *position) {
    size_t bi;
    bool found = false;

    if (function == NULL || block_index == NULL || position == NULL) {
        return false;
    }
    for (bi = 0U; bi < function->block_count; ++bi) {
        const MinicCoreBlock *block = &function->blocks[bi];
        size_t pi;
        for (pi = 0U; pi < block->instruction_count; ++pi) {
            if (block->instructions[pi] == instruction_id) {
                if (found) {
                    return false;
                }
                *block_index = bi;
                *position = pi;
                found = true;
            }
        }
    }
    return found;
}

static bool core_call_argument_object_use_count(const MinicCoreFunction *function,
                                                MinicCoreObjectId object_id,
                                                MinicCoreInstructionId target_call,
                                                size_t *total_uses,
                                                size_t *target_uses) {
    size_t instruction_index;
    size_t total = 0U;
    size_t target = 0U;

    if (function == NULL || total_uses == NULL || target_uses == NULL) {
        return false;
    }
    for (instruction_index = 0U; instruction_index < function->instruction_count;
         ++instruction_index) {
        const MinicCoreInstruction *instruction = &function->instructions[instruction_index];
        size_t begin;
        size_t count;
        size_t i;

        if (instruction->kind == MINIC_CORE_INSTRUCTION_CALL) {
            begin = instruction->value.call.argument_begin;
            count = instruction->value.call.argument_count;
        } else if (instruction->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL) {
            begin = instruction->value.indirect_call.argument_begin;
            count = instruction->value.indirect_call.argument_count;
        } else {
            continue;
        }
        if (begin > function->call_argument_count ||
            count > function->call_argument_count - begin) {
            return false;
        }
        for (i = 0U; i < count; ++i) {
            const MinicCoreCallArgument *argument = &function->call_arguments[begin + i];
            if (argument->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT &&
                argument->value.object_id == object_id) {
                ++total;
                if (instruction_index == target_call) {
                    ++target;
                }
            }
        }
    }
    *total_uses = total;
    *target_uses = target;
    return true;
}

static bool core_record_snapshot_single_call_candidate(
    const MinicCoreFunction *function,
    MinicCoreObjectId candidate,
    MinicCoreInstructionId target_call) {
    MinicCoreInstructionId producer = MINIC_CORE_INSTRUCTION_INVALID;
    size_t producer_count = 0U;
    size_t instruction_index;
    size_t total_call_uses;
    size_t target_call_uses;
    size_t producer_block;
    size_t producer_position;
    size_t call_block;
    size_t call_position;

    if (function == NULL || candidate >= function->object_count ||
        target_call >= function->instruction_count ||
        !core_call_argument_object_use_count(function,
                                             candidate,
                                             target_call,
                                             &total_call_uses,
                                             &target_call_uses) ||
        total_call_uses != 1U || target_call_uses != 1U) {
        return false;
    }
    for (instruction_index = 0U; instruction_index < function->instruction_count;
         ++instruction_index) {
        const MinicCoreInstruction *instruction = &function->instructions[instruction_index];

        if (instruction->kind == MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT &&
            instruction->value.parameter_object.object_id == candidate) {
            return false;
        }
        if (instruction->kind == MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS &&
            instruction->value.object_id == candidate) {
            return false;
        }
        if (instruction->kind == MINIC_CORE_INSTRUCTION_RECORD_LOAD &&
            instruction->value.record_load.destination_object == candidate) {
            producer = (MinicCoreInstructionId)instruction_index;
            ++producer_count;
        }
        if (instruction->kind == MINIC_CORE_INSTRUCTION_CALL &&
            instruction->value.call.result_object == candidate) {
            return false;
        }
    }
    if (producer_count != 1U || producer == MINIC_CORE_INSTRUCTION_INVALID ||
        !core_instruction_block_position(function,
                                         producer,
                                         &producer_block,
                                         &producer_position) ||
        !core_instruction_block_position(function,
                                         target_call,
                                         &call_block,
                                         &call_position) ||
        producer_block != call_block || producer_position >= call_position) {
        return false;
    }
    return true;
}

static bool core_record_call_result_snapshot_reuse_candidate(
    const MinicCoreFunction *function,
    MinicCoreObjectId result_object,
    MinicCoreObjectId *candidate_out) {
    MinicCoreInstructionId defining_call = MINIC_CORE_INSTRUCTION_INVALID;
    size_t defining_count = 0U;
    size_t instruction_index;
    const MinicCoreInstruction *call;
    size_t i;

    if (function == NULL || candidate_out == NULL ||
        result_object >= function->object_count ||
        !minic_type_is_record(function->objects[result_object].type) ||
        function->objects[result_object].element_count != 1U) {
        return false;
    }
    for (instruction_index = 0U; instruction_index < function->instruction_count;
         ++instruction_index) {
        const MinicCoreInstruction *instruction = &function->instructions[instruction_index];
        if (instruction->kind == MINIC_CORE_INSTRUCTION_CALL &&
            instruction->value.call.result_object == result_object) {
            defining_call = (MinicCoreInstructionId)instruction_index;
            ++defining_count;
        }
    }
    if (defining_count != 1U || defining_call == MINIC_CORE_INSTRUCTION_INVALID) {
        return false;
    }
    call = &function->instructions[defining_call];
    if (call->value.call.argument_begin > function->call_argument_count ||
        call->value.call.argument_count >
            function->call_argument_count - call->value.call.argument_begin) {
        return false;
    }
    for (i = 0U; i < call->value.call.argument_count; ++i) {
        const MinicCoreCallArgument *argument =
            &function->call_arguments[call->value.call.argument_begin + i];
        MinicCoreObjectId candidate;
        const MinicCoreObject *candidate_object;
        const MinicCoreObject *result;

        if (argument->kind != MINIC_CORE_CALL_ARGUMENT_OBJECT) {
            continue;
        }
        candidate = argument->value.object_id;
        if (candidate >= result_object || candidate >= function->object_count ||
            !core_record_snapshot_single_call_candidate(function, candidate, defining_call)) {
            continue;
        }
        candidate_object = &function->objects[candidate];
        result = &function->objects[result_object];
        if (candidate_object->element_count != result->element_count ||
            candidate_object->explicit_alignment != result->explicit_alignment ||
            !minic_type_equal(candidate_object->type, result->type)) {
            continue;
        }
        *candidate_out = candidate;
        return true;
    }
    return false;
}

'''
text = text.replace(frame_marker, helper + frame_marker, 1)

sizing_anchor = r'''        object_size *= function->objects[object_index].element_count;
        if (!align_up(storage_size, object_alignment, &storage_size) ||
            storage_size > SIZE_MAX - object_size) {
            return false;
        }
        storage_size += object_size;'''
sizing_new = r'''        object_size *= function->objects[object_index].element_count;
        {
            MinicCoreObjectId reuse_candidate;
            if (object_index <= UINT32_MAX &&
                core_record_call_result_snapshot_reuse_candidate(
                    function, (MinicCoreObjectId)object_index, &reuse_candidate)) {
                continue;
            }
        }
        if (!align_up(storage_size, object_alignment, &storage_size) ||
            storage_size > SIZE_MAX - object_size) {
            return false;
        }
        storage_size += object_size;'''
if sizing_anchor not in text:
    raise SystemExit("object sizing anchor not found")
text = text.replace(sizing_anchor, sizing_new, 1)

assign_anchor = r'''            if (function->objects[cached_index].explicit_alignment != 0U &&
                function->objects[cached_index].explicit_alignment > object_alignment) {
                object_alignment = function->objects[cached_index].explicit_alignment;
            }
            if (!align_up(cached_offset, object_alignment, &cached_offset)) {
                free(frame->object_offsets);
                frame->object_offsets = NULL;
                return false;
            }
            frame->object_offsets[cached_index] = cached_offset;
            object_size *= function->objects[cached_index].element_count;'''
assign_new = r'''            if (function->objects[cached_index].explicit_alignment != 0U &&
                function->objects[cached_index].explicit_alignment > object_alignment) {
                object_alignment = function->objects[cached_index].explicit_alignment;
            }
            {
                MinicCoreObjectId reuse_candidate;
                if (cached_index <= UINT32_MAX &&
                    core_record_call_result_snapshot_reuse_candidate(
                        function, (MinicCoreObjectId)cached_index, &reuse_candidate)) {
                    if ((size_t)reuse_candidate >= cached_index) {
                        free(frame->object_offsets);
                        frame->object_offsets = NULL;
                        return false;
                    }
                    frame->object_offsets[cached_index] =
                        frame->object_offsets[(size_t)reuse_candidate];
                    continue;
                }
            }
            if (!align_up(cached_offset, object_alignment, &cached_offset)) {
                free(frame->object_offsets);
                frame->object_offsets = NULL;
                return false;
            }
            frame->object_offsets[cached_index] = cached_offset;
            object_size *= function->objects[cached_index].element_count;'''
if assign_anchor not in text:
    raise SystemExit("object assignment anchor not found")
text = text.replace(assign_anchor, assign_new, 1)

p.write_text(text)
print("MINIC_RISCV64_RECORD_CALL_RESULT_SNAPSHOT_REUSE_V0=APPLIED")
