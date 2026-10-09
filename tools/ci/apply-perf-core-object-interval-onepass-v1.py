#!/usr/bin/env python3
"""P15: compute Core scalar object liveness/eligibility once per function.

Apply AFTER P14 (CoreObject address-use index) in the canonical RV64 profile.
Per-object type/layout eligibility is intentionally left in the original
function. The expensive scans to find object references/intervals are
replaced by one conservative IR traversal, only if the P14 value use index
already exists. All allocation/invalid-IR failures fall back to P14's
per-object scanning implementation. No slot-coloring policy changes.
"""
from pathlib import Path

p=Path("src/target/riscv64/core_codegen.c")
s=p.read_text()
def once(old,new):
    global s
    n=s.count(old)
    if n!=1:
        raise SystemExit(f"P15 anchor count {n}: {old[:105]!r}")
    s=s.replace(old,new,1)

helper=r'''
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

'''
once("static bool core_scalar_object_interval(const MinicC0Program *program,\n",helper+"static bool core_scalar_object_interval(const MinicC0Program *program,\n")

begin=s.find("static bool core_scalar_object_interval(const MinicC0Program *program,")
end=s.find("static bool core_scalar_object_intervals_overlap(",begin)
if begin<0 or end<begin:raise SystemExit("cannot isolate object interval")
f=s[begin:end]
old="""        function->objects[object_id].explicit_alignment > 8U) {
        return true;
    }

    /* Aggregate-only direct object uses must never be folded into the scalar"""
new="""        function->objects[object_id].explicit_alignment > 8U) {
        return true;
    }
    /* Preserve all legacy type and layout eligibility conditions. */
    if (core_object_interval_index != NULL) {
        *interval = core_object_interval_index[object_id];
        return true;
    }

    /* Aggregate-only direct object uses must never be folded into the scalar"""
if f.count(old)!=1:raise SystemExit(f"eligibility anchor count={f.count(old)}")
f=f.replace(old,new,1)
s=s[:begin]+f+s[end:]

once("""static void core_scalar_object_reuse_cache_clear(void) {
    free(core_object_address_use_index);""",
"""static void core_scalar_object_reuse_cache_clear(void) {
    free(core_object_interval_index);
    core_object_interval_index = NULL;
    free(core_object_address_use_index);""")

begin=s.find("static bool core_scalar_object_reuse_layout(const MinicC0Program *program,")
end=s.find("static bool core_frame_initialize(",begin)
if begin<0 or end<begin:raise SystemExit("cannot isolate layout")
f=s[begin:end]
old="""            if (function->object_count >= 8U &&
                function->instruction_count >= 100U) {
                (void)core_object_address_build_use_index(function);
            }
            for (object_index = 0U; object_index < function->object_count; ++object_index) {"""
new="""            if (function->object_count >= 8U &&
                function->instruction_count >= 100U) {
                (void)core_object_address_build_use_index(function);
                /* Optional one-pass object analysis; safe legacy fallback. */
                (void)core_object_interval_build_index(function);
            }
            for (object_index = 0U; object_index < function->object_count; ++object_index) {"""
if f.count(old)!=1:raise SystemExit(f"build init anchor count={f.count(old)}")
f=f.replace(old,new,1)
s=s[:begin]+f+s[end:]
p.write_text(s)
print("CORE_OBJECT_INTERVAL_INDEX_V1=APPLIED")

# CI 2026-10-09: non-executable routing probe; the P15 transform above is unchanged.
# CI 2026-10-09: check that P-only source edits no longer launch generic RV64/AR/LD T1.
