#include "target/common/core_object_intervals.h"
#include "core/value_uses.h"
#include "target/data_layout.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* One optional analysis per frame. No target-wide static index pointers. */
/* CORE_OBJECT_SLOT_REUSE_V0: conservatively reuse stack storage for small
   scalar CoreObjects whose complete address lifetime is proven to stay inside
   one basic block.  An OBJECT_ADDRESS result must be consumed only as the
   direct address operand of LOAD/STORE; derived pointers, calls, asm, returns,
   cross-block uses, records, arrays and over-aligned objects remain permanent.
   Candidate intervals are colored in 8-byte units and different basic blocks
   share the same pool because no candidate survives a block boundary. */
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
typedef struct CoreObjectAddressUseBuild {
    CoreObjectAddressUse *entries;
    size_t count;
} CoreObjectAddressUseBuild;


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

/* One checked function-wide address-use index for every object count. */
static bool core_object_address_build_use_index(
    const MinicCoreFunction *function, MinicCoreObjectIntervalAnalysis *analysis) {
    CoreObjectAddressUseBuild build;
    size_t block_index;
    if (function == NULL ||
        function->value_count > SIZE_MAX / sizeof(*build.entries)) {
        return false;
    }
    build.count = function->value_count;
    build.entries = (CoreObjectAddressUse *)calloc(
        build.count != 0U ? build.count : 1U, sizeof(*build.entries));
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
    analysis->address_use_index = build.entries;
    return true;
unavailable:
    free(build.entries);
    return false;
}


/* CORE_OBJECT_INTERVAL_INDEX_V1: one pass over function instructions, not
 * N passes for N scalar objects. Only used with a valid P14 Value-use map. */

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

/* Indexing is optional, compilation is not. Allocation failure disables
 * stack-slot reuse instead of restarting the obsolete O(objects * IR)
 * compatibility scan or treating an unproven lifetime as reusable. */
static bool core_object_interval_build_index(
    const MinicCoreFunction *function, MinicCoreObjectIntervalAnalysis *analysis) {
    CoreScalarObjectInterval *intervals;
    unsigned char *disqualified;
    size_t block_index;
    size_t object_count;
    size_t i;

    if (function == NULL || analysis->address_use_index == NULL ||
        function->object_count == 0U ||
        function->object_count > SIZE_MAX / sizeof(*intervals)) {
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
                if (instruction->value.indirect_call.result_object < object_count) {
                    disqualified[instruction->value.indirect_call.result_object]=1U;
                }
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
                use=&analysis->address_use_index[address];
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
    analysis->interval_index=intervals;
    return true;

unavailable:
    free(disqualified);
    free(intervals);
    return false;
}

bool minic_core_object_interval(
    const MinicC0Program *program, const MinicCoreFunction *function,
    MinicCoreObjectId object_id, const MinicDataLayout *layout,
    size_t unit_bytes, const MinicCoreObjectIntervalAnalysis *analysis,
    CoreScalarObjectInterval *interval) {
    size_t object_size;
    size_t object_alignment;

    if (program == NULL || function == NULL || layout == NULL ||
        (unit_bytes != 4U && unit_bytes != 8U) || interval == NULL ||
        object_id >= function->object_count) {
        return false;
    }
    interval->reusable = false;
    interval->can_share_across_blocks = false;
    interval->block_index = SIZE_MAX;
    interval->first_position = SIZE_MAX;
    interval->last_position = 0U;

    if (!(minic_type_is_integer(function->objects[object_id].type) ||
           minic_type_is_pointer(function->objects[object_id].type) ||
           minic_type_is_float(function->objects[object_id].type) ||
           minic_type_is_double(function->objects[object_id].type) ||
           minic_type_is_long_double(function->objects[object_id].type)) ||
        function->objects[object_id].element_count != 1U ||
        !minic_data_layout_type(layout,
                                program,
                                function->objects[object_id].type,
                                &object_size,
                                &object_alignment) ||
        object_size == 0U || object_size > unit_bytes || object_alignment == 0U ||
        object_alignment > unit_bytes ||
        function->objects[object_id].explicit_alignment > unit_bytes) {
        return true;
    }
    /* The function-wide indexed proof is the sole interval analysis path.
     * Allocation or index failure forfeits sharing, never object storage. */
    if (analysis == NULL || analysis->interval_index == NULL)
        return true;
    *interval = analysis->interval_index[object_id];
    if (interval->reusable) {
        interval->can_share_across_blocks =
            core_scalar_object_first_access_defines(
                function, object_id, interval->block_index);
    }
    return true;
}


void minic_core_object_intervals_index(
    const MinicCoreFunction *function,
    MinicCoreObjectIntervalAnalysis *analysis) {
    if (function == NULL || analysis == NULL ||
        analysis->address_use_index != NULL || analysis->interval_index != NULL ||
        function->object_count == 0U)
        return;
    if (core_object_address_build_use_index(function, analysis))
        (void)core_object_interval_build_index(function, analysis);
}
void minic_core_object_intervals_destroy(
    MinicCoreObjectIntervalAnalysis *analysis) {
    if (analysis == NULL) return;
    free(analysis->address_use_index);
    free(analysis->interval_index);
    analysis->address_use_index = NULL;
    analysis->interval_index = NULL;
}
