#include "target/riscv64/object_liveness.h"
#include "target/riscv64/legalize.h"
#include "core/value_uses.h"
#include "target/data_layout.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Reuse the original optional cache and same per-function liveness
 * heuristics; no new machine representation or changed ABI constraints. */
/* RECORD_CALL_RESULT_SNAPSHOT_REUSE_V0: Core by-value record arguments may
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

bool minic_riscv64_record_call_result_snapshot_reuse_candidate(
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

/* CORE_OBJECT_SLOT_REUSE_V0: conservatively reuse stack storage for small
   scalar CoreObjects whose complete address lifetime is proven to stay inside
   one basic block.  An OBJECT_ADDRESS result must be consumed only as the
   direct address operand of LOAD/STORE; derived pointers, calls, asm, returns,
   cross-block uses, records, arrays and over-aligned objects remain permanent.
   Candidate intervals are colored in 8-byte units and different basic blocks
   share the same pool because no candidate survives a block boundary. */
typedef struct CoreScalarObjectInterval {
    bool reusable;
    bool can_share_across_blocks;
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

/* P1_DEFINED_BEFORE_USE_CROSSBLOCK_V1: prove the first real read/write
   of a scalar object in its *sole* owner block is a write. Reaching this
   predicate never weakens the existing no-address-escape analysis.
   The IR verifier already requires a Core SSA value to be available only
   within its defining basic block. A direct LOAD/STORE using an
   OBJECT_ADDRESS result therefore identifies a local access. */
static bool core_scalar_object_first_access_defines(
    const MinicCoreFunction *function,
    MinicCoreObjectId object_id, size_t block_index) {
    const MinicCoreBlock *block;
    size_t *first_producer_plus_one;
    size_t position;
    bool first_access_defines = false;

    if (function == NULL || block_index >= function->block_count ||
        function->value_count > SIZE_MAX / sizeof(size_t)) {
        return false;
    }
    block = &function->blocks[block_index];

    /* PERF: the original implementation searched the entire block for
       OBJECT_ADDRESS on EVERY LOAD/STORE of EVERY eligible CoreObject.
       For large, unrolled Linux crypto functions this is quadratic in block
       length per object (lib/crypto/blake2s-generic.c: 33m for one TU).
       Index matching local address producers once, then inspect each
       memory access exactly once. A value id is a dense Core IR index.
       The stored position is 1-based so calloc's 0 means "unseen". */
    first_producer_plus_one = (size_t *)calloc(
        function->value_count != 0U ? function->value_count : 1U,
        sizeof(*first_producer_plus_one));
    if (first_producer_plus_one == NULL) {
        /* Failing to allocate an optimization-only index must never
           license sharing a live object across basic blocks. */
        return false;
    }
    for (position = 0U; position < block->instruction_count; ++position) {
        MinicCoreInstructionId id = block->instructions[position];
        const MinicCoreInstruction *inst;
        MinicCoreValueId result;
        if (id >= function->instruction_count || position == SIZE_MAX - 1U) {
            goto finish;
        }
        inst = &function->instructions[id];
        if (inst->kind != MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS ||
            inst->value.object_id != object_id) {
            continue;
        }
        result = inst->result;
        if (result != MINIC_CORE_VALUE_INVALID &&
            result < function->value_count &&
            first_producer_plus_one[result] == 0U) {
            /* Preserve the original helper's earliest producer semantics
               even if malformed IR repeats the same result id. */
            first_producer_plus_one[result] = position + 1U;
        }
    }
    for (position = 0U; position < block->instruction_count; ++position) {
        MinicCoreInstructionId id = block->instructions[position];
        const MinicCoreInstruction *inst = &function->instructions[id];
        MinicCoreValueId address = MINIC_CORE_VALUE_INVALID;
        if (inst->kind == MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT &&
            inst->value.parameter_object.object_id == object_id) {
            first_access_defines = true;
            break;
        }
        if (inst->kind == MINIC_CORE_INSTRUCTION_LOAD) {
            address = inst->value.load.address;
        } else if (inst->kind == MINIC_CORE_INSTRUCTION_STORE) {
            address = inst->value.store.address;
        } else {
            continue;
        }
        if (address < function->value_count &&
            first_producer_plus_one[address] != 0U) {
            first_access_defines =
                inst->kind == MINIC_CORE_INSTRUCTION_STORE &&
                first_producer_plus_one[address] - 1U < position;
            break;
        }
    }
finish:
    free(first_producer_plus_one);
    return first_access_defines;
}


/* CORE_OBJECT_ADDRESS_USE_INDEX_V1:
 * Whole-function ValueId use counts and direct address ranges. */
typedef struct CoreObjectAddressUse {
    size_t total;
    size_t direct;
    size_t block;
    size_t first;
    size_t last;
    bool cross_block;
} CoreObjectAddressUse;

typedef struct CoreObjectAddressUseBuild {
    CoreObjectAddressUse *entries;
    size_t count;
} CoreObjectAddressUseBuild;

static CoreObjectAddressUse *core_object_address_use_index;

static bool core_object_address_note_use(MinicCoreValueId id, void *opaque) {
    CoreObjectAddressUseBuild *build = (CoreObjectAddressUseBuild *)opaque;
    if (build == NULL || id >= build->count ||
        build->entries[id].total == SIZE_MAX) {
        return false;
    }
    ++build->entries[id].total;
    return true;
}

static bool core_object_address_record_direct(
    CoreObjectAddressUseBuild *build, MinicCoreValueId id,
    size_t block, size_t position) {
    CoreObjectAddressUse *use;
    if (id >= build->count) {
        return false;
    }
    use = &build->entries[id];
    if (use->direct == SIZE_MAX) {
        return false;
    }
    if (use->direct == 0U) {
        use->block = block;
        use->first = position;
        use->last = position;
    } else if (use->block != block) {
        use->cross_block = true;
    } else {
        if (position < use->first) use->first = position;
        if (position > use->last) use->last = position;
    }
    ++use->direct;
    return true;
}

/* Called once per sufficiently large function.  NULL means original scan. */
static bool core_object_address_build_use_index(const MinicCoreFunction *function) {
    CoreObjectAddressUseBuild build;
    size_t block_index;
    if (function == NULL || function->value_count == 0U ||
        function->value_count > SIZE_MAX / sizeof(*build.entries)) {
        return false;
    }
    build.count = function->value_count;
    build.entries = (CoreObjectAddressUse *)calloc(
        build.count, sizeof(*build.entries));
    if (build.entries == NULL) {
        return false;
    }
    for (block_index = 0U; block_index < function->block_count; ++block_index) {
        const MinicCoreBlock *block = &function->blocks[block_index];
        size_t position;
        for (position = 0U; position < block->instruction_count; ++position) {
            MinicCoreInstructionId id = block->instructions[position];
            const MinicCoreInstruction *instruction;
            if (id >= function->instruction_count) goto unavailable;
            instruction = &function->instructions[id];
            if (!minic_core_visit_instruction_value_uses(
                    function, instruction, core_object_address_note_use, &build)) {
                goto unavailable;
            }
            if (instruction->kind == MINIC_CORE_INSTRUCTION_LOAD &&
                !core_object_address_record_direct(
                    &build, instruction->value.load.address, block_index, position)) {
                goto unavailable;
            }
            if (instruction->kind == MINIC_CORE_INSTRUCTION_STORE &&
                !core_object_address_record_direct(
                    &build, instruction->value.store.address, block_index, position)) {
                goto unavailable;
            }
        }
        if (block->has_terminator &&
            !minic_core_visit_terminator_value_uses(
                &block->terminator, core_object_address_note_use, &build)) {
            goto unavailable;
        }
    }
    core_object_address_use_index = build.entries;
    return true;
unavailable:
    free(build.entries);
    return false;
}


/* CORE_OBJECT_INTERVAL_INDEX_V1: one pass over function instructions, not
 * N passes for N scalar objects. Only used with a valid P14 Value-use map. */
static CoreScalarObjectInterval *core_object_interval_index;

static bool core_object_interval_mark(
    CoreScalarObjectInterval *intervals, unsigned char *disqualified,
    size_t object_count, MinicCoreObjectId id, size_t block, size_t position) {
    if (id >= object_count) {
        return true;  /* Irrelevant to any of the real objects. */
    }
    if (disqualified[id]) {
        return true;
    }
    if (!core_scalar_object_note_position(&intervals[id], block, position)) {
        disqualified[id] = 1U;
        intervals[id].reusable = false;
    }
    return true;
}

/* This is an optional index: failure MUST return false and keep the original
 * scans available. In particular, allocation pressure must not make valid
 * compilation fail. */
static bool core_object_interval_build_index(const MinicCoreFunction *function) {
    CoreScalarObjectInterval *intervals;
    unsigned char *disqualified;
    size_t block_index;
    size_t object_count;
    size_t i;

    if (function == NULL || core_object_address_use_index == NULL ||
        function->object_count == 0U ||
        function->object_count > SIZE_MAX / sizeof(*intervals)) {
        return false;
    }
    if (getenv("MINIC_TEST_FORCE_CORE_OBJECT_INTERVAL_FALLBACK") != NULL) {
        return false;
    }
    object_count=function->object_count;
    intervals=(CoreScalarObjectInterval *)malloc(object_count * sizeof(*intervals));
    disqualified=(unsigned char *)calloc(object_count,sizeof(*disqualified));
    if (intervals == NULL || disqualified == NULL) {
        free(intervals);
        free(disqualified);
        return false;
    }
    for (i=0U;i<object_count;++i) {
        intervals[i].reusable=false;
        intervals[i].can_share_across_blocks=false;
        intervals[i].block_index=SIZE_MAX;
        intervals[i].first_position=SIZE_MAX;
        intervals[i].last_position=0U;
    }
    for (block_index=0U;block_index<function->block_count;++block_index) {
        const MinicCoreBlock *block=&function->blocks[block_index];
        size_t position;
        if (block->has_terminator &&
            block->terminator.kind == MINIC_CORE_TERMINATOR_RETURN &&
            block->terminator.return_object < object_count) {
            disqualified[block->terminator.return_object] = 1U;
        }
        for (position=0U;position<block->instruction_count;++position) {
            MinicCoreInstructionId id=block->instructions[position];
            const MinicCoreInstruction *instruction;
            size_t begin=0U,count=0U,k;
            if (id >= function->instruction_count) {
                goto unavailable;
            }
            instruction=&function->instructions[id];
            if (instruction->kind == MINIC_CORE_INSTRUCTION_RECORD_LOAD &&
                instruction->value.record_load.destination_object < object_count) {
                disqualified[instruction->value.record_load.destination_object]=1U;
            }
            if (instruction->kind == MINIC_CORE_INSTRUCTION_CALL) {
                if (instruction->value.call.result_object < object_count) {
                    disqualified[instruction->value.call.result_object]=1U;
                }
                begin=instruction->value.call.argument_begin;
                count=instruction->value.call.argument_count;
            } else if (instruction->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL) {
                begin=instruction->value.indirect_call.argument_begin;
                count=instruction->value.indirect_call.argument_count;
            }
            if (count != 0U) {
                if (begin > function->call_argument_count ||
                    count > function->call_argument_count-begin) {
                    goto unavailable;
                }
                for (k=0U;k<count;++k) {
                    const MinicCoreCallArgument *argument=&function->call_arguments[begin+k];
                    if (argument->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT &&
                        argument->value.object_id < object_count) {
                        disqualified[argument->value.object_id]=1U;
                    }
                }
            }
            if (instruction->kind == MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT) {
                (void)core_object_interval_mark(
                    intervals, disqualified, object_count,
                    instruction->value.parameter_object.object_id,block_index,position);
            }
            if (instruction->kind == MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS &&
                instruction->value.object_id < object_count) {
                MinicCoreObjectId oid=instruction->value.object_id;
                MinicCoreValueId address=instruction->result;
                const CoreObjectAddressUse *use;
                if (disqualified[oid]) continue;
                if (address == MINIC_CORE_VALUE_INVALID ||
                    address >= function->value_count) {
                    disqualified[oid]=1U;
                    continue;
                }
                (void)core_object_interval_mark(
                    intervals, disqualified, object_count,oid,block_index,position);
                if (disqualified[oid]) continue;
                use=&core_object_address_use_index[address];
                if (use->cross_block ||
                    (use->direct != 0U && use->block != block_index) ||
                    use->total != use->direct) {
                    disqualified[oid]=1U;
                    continue;
                }
                if (use->direct != 0U) {
                    (void)core_object_interval_mark(
                        intervals,disqualified,object_count,oid,block_index,use->first);
                    (void)core_object_interval_mark(
                        intervals,disqualified,object_count,oid,block_index,use->last);
                }
            }
        }
    }
    for (i=0U;i<object_count;++i) {
        if (disqualified[i]) intervals[i].reusable=false;
    }
    free(disqualified);
    core_object_interval_index=intervals;
    return true;

unavailable:
    free(disqualified);
    free(intervals);
    return false;
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
    interval->can_share_across_blocks = false;
    interval->block_index = SIZE_MAX;
    interval->first_position = SIZE_MAX;
    interval->last_position = 0U;

    if (!minic_riscv64_scalar_type(function->objects[object_id].type) ||
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
    /* Preserve existing type/layout eligibility and P1 cross-block proof.
       Indexed objects take the same first-access predicate as the old scan. */
    if (core_object_interval_index != NULL) {
        *interval = core_object_interval_index[object_id];
        if (interval->reusable) {
            interval->can_share_across_blocks =
                core_scalar_object_first_access_defines(
                    function, object_id, interval->block_index);
        }
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
                if (core_object_address_use_index != NULL) {
                    const CoreObjectAddressUse *use =
                        &core_object_address_use_index[address_value];
                    if (use->cross_block ||
                        (use->direct != 0U &&
                         (use->block != block_index ||
                          !core_scalar_object_note_position(
                              interval, block_index, use->first) ||
                          !core_scalar_object_note_position(
                              interval, block_index, use->last))) ||
                        use->total != use->direct) {
                        interval->reusable = false;
                        return true;
                    }
                    continue;
                }
                /* Allocation or malformed-index fallback: original full scan. */
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
                        if (!minic_core_visit_instruction_value_uses(function,
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
                        !minic_core_visit_terminator_value_uses(&candidate_block->terminator,
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
    if (interval->reusable) {
        interval->can_share_across_blocks =
            core_scalar_object_first_access_defines(
                function, object_id, interval->block_index);
    }
    return true;
}

static bool p1_crossblock_current_function_enabled = true;

void minic_riscv64_object_liveness_set_crossblock_enabled(bool enabled) {
    p1_crossblock_current_function_enabled = enabled;
}

static bool core_scalar_object_intervals_overlap(const CoreScalarObjectInterval *left,
                                                 const CoreScalarObjectInterval *right) {
    if (left == NULL || right == NULL || !left->reusable || !right->reusable) {
        return false;
    }
    if (left->block_index != right->block_index) {
        return !(p1_crossblock_current_function_enabled &&
                 left->can_share_across_blocks &&
                 right->can_share_across_blocks);
    }
    return !(left->last_position < right->first_position ||
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
    free(core_object_interval_index);
    core_object_interval_index = NULL;
    free(core_object_address_use_index);
    core_object_address_use_index = NULL;
    free(core_scalar_object_reuse_cache.slots);
    free(core_scalar_object_reuse_cache.intervals);
    core_scalar_object_reuse_cache.program = NULL;
    core_scalar_object_reuse_cache.function = NULL;
    core_scalar_object_reuse_cache.intervals = NULL;
    core_scalar_object_reuse_cache.slots = NULL;
    core_scalar_object_reuse_cache.peak_slots = 0U;
}

void minic_riscv64_object_liveness_begin_function(void) {
    core_scalar_object_reuse_cache_clear();
}
void minic_riscv64_object_liveness_end_function(void) {
    core_scalar_object_reuse_cache_clear();
}

bool minic_riscv64_scalar_object_reuse_layout(const MinicC0Program *program,
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

            /* Building the index costs O(I); avoid it on small Core functions
             * where a few legacy scans are usually cheaper.  Never fail
             * compilation merely because the optional index is unavailable. */
            if (function->object_count >= 8U &&
                function->instruction_count >= 100U) {
                (void)core_object_address_build_use_index(function);
                /* Optional one-pass object analysis; legacy fallback remains. */
                (void)core_object_interval_build_index(function);
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
